#include "allocator/free_list_allocator.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>

namespace cma {

namespace {
bool is_power_of_two(std::size_t v) { return v != 0 && (v & (v - 1)) == 0; }
std::size_t round_up(std::size_t value, std::size_t multiple) { return ((value + multiple - 1) / multiple) * multiple; }

constexpr std::uint32_t kFreeMagic = 0xF4EEF4EEu;
constexpr std::uint32_t kAllocMagic = 0xA110CA11u;
}  // namespace

// Every block's address, and every block's total size, is a multiple
// of kMaxSupportedAlignment (enforced by alignas() here and by the
// rounding performed in the constructor / allocate() / split_block()).
// That single invariant is what makes the payload pointer
// (block_start + sizeof(BlockHeader)) automatically aligned to any
// power-of-two alignment up to kMaxSupportedAlignment, with no
// per-allocation back-pointer/offset bookkeeping needed.
struct alignas(FreeListAllocator::kMaxSupportedAlignment) FreeListAllocator::BlockHeader {
    std::size_t block_size;  // total span of this block: header + payload + internal padding + footer
    std::uint32_t magic;
    bool is_free;
    BlockHeader *prev_free = nullptr;  // valid only while is_free
    BlockHeader *next_free = nullptr;  // valid only while is_free
};

struct FreeListAllocator::BlockFooter {
    std::size_t block_size;  // mirrors the owning header's block_size; lets prev_physical() walk backward
    std::uint32_t magic;
};

FreeListAllocator::FreeListAllocator(std::size_t capacity_bytes, FitPolicy policy) : policy_(policy) {
    // Over-allocate by one alignment unit so the usable arena can start
    // at a kMaxSupportedAlignment-aligned address inside raw_storage_,
    // regardless of what alignment operator new[] itself happened to
    // give us for the raw std::byte[] allocation.
    std::size_t raw_size = capacity_bytes + kMaxSupportedAlignment;
    raw_storage_ = std::make_unique<std::byte[]>(raw_size);

    auto addr = reinterpret_cast<std::uintptr_t>(raw_storage_.get());
    std::uintptr_t aligned_addr = (addr + (kMaxSupportedAlignment - 1)) & ~(kMaxSupportedAlignment - 1);
    arena_base_ = reinterpret_cast<std::byte *>(aligned_addr);

    std::size_t usable = raw_size - static_cast<std::size_t>(aligned_addr - addr);
    capacity_bytes_ = (usable / kMaxSupportedAlignment) * kMaxSupportedAlignment;

    reset();
}

bool FreeListAllocator::is_last_physical(const BlockHeader *block) const noexcept {
    const auto *end = reinterpret_cast<const std::byte *>(block) + block->block_size;
    return end == arena_base_ + capacity_bytes_;
}

FreeListAllocator::BlockHeader *FreeListAllocator::first_block() const noexcept {
    return reinterpret_cast<BlockHeader *>(arena_base_);
}

FreeListAllocator::BlockHeader *FreeListAllocator::next_physical(BlockHeader *block) const noexcept {
    if (is_last_physical(block)) {
        return nullptr;
    }
    return reinterpret_cast<BlockHeader *>(reinterpret_cast<std::byte *>(block) + block->block_size);
}

FreeListAllocator::BlockHeader *FreeListAllocator::prev_physical(BlockHeader *block) const noexcept {
    if (reinterpret_cast<std::byte *>(block) == arena_base_) {
        return nullptr;
    }
    const auto *footer = reinterpret_cast<BlockFooter *>(reinterpret_cast<std::byte *>(block) - sizeof(BlockFooter));
    return reinterpret_cast<BlockHeader *>(reinterpret_cast<std::byte *>(block) - footer->block_size);
}

void FreeListAllocator::write_footer(BlockHeader *block) noexcept {
    auto *footer =
        reinterpret_cast<BlockFooter *>(reinterpret_cast<std::byte *>(block) + block->block_size - sizeof(BlockFooter));
    footer->block_size = block->block_size;
    footer->magic = block->magic;
}

void FreeListAllocator::free_list_insert(BlockHeader *block) noexcept {
    block->is_free = true;
    block->magic = kFreeMagic;
    block->prev_free = nullptr;
    block->next_free = free_list_head_;
    if (free_list_head_ != nullptr) {
        free_list_head_->prev_free = block;
    }
    free_list_head_ = block;
    write_footer(block);
}

void FreeListAllocator::free_list_remove(BlockHeader *block) noexcept {
    if (block->prev_free != nullptr) {
        block->prev_free->next_free = block->next_free;
    } else {
        free_list_head_ = block->next_free;
    }
    if (block->next_free != nullptr) {
        block->next_free->prev_free = block->prev_free;
    }
    block->prev_free = nullptr;
    block->next_free = nullptr;
}

FreeListAllocator::BlockHeader *FreeListAllocator::find_fit(std::size_t needed_size) const noexcept {
    BlockHeader *best = nullptr;
    for (BlockHeader *b = free_list_head_; b != nullptr; b = b->next_free) {
        if (b->block_size < needed_size) {
            continue;
        }
        if (policy_ == FitPolicy::FirstFit) {
            return b;
        }
        // BestFit: smallest block that still fits.
        if (best == nullptr || b->block_size < best->block_size) {
            best = b;
        }
    }
    return best;
}

void FreeListAllocator::split_block(BlockHeader *block, std::size_t needed_size) noexcept {
    std::size_t min_block_size = round_up(sizeof(BlockHeader) + sizeof(BlockFooter), kMaxSupportedAlignment);
    std::size_t remainder = block->block_size - needed_size;
    if (remainder < min_block_size) {
        // Not worth splitting: the leftover couldn't even hold an empty
        // block's own header+footer. The whole block is handed to the
        // caller as-is (documented internal fragmentation).
        return;
    }
    block->block_size = needed_size;
    auto *remainder_block = reinterpret_cast<BlockHeader *>(reinterpret_cast<std::byte *>(block) + needed_size);
    remainder_block->block_size = remainder;
    free_list_insert(remainder_block);
}

void *FreeListAllocator::allocate(std::size_t size, std::size_t alignment, AllocResult *out_result) noexcept {
    auto set_result = [&](AllocResult r) {
        if (out_result != nullptr) {
            *out_result = r;
        }
    };

    if (size == 0) {
        set_result(AllocResult::InvalidSize);
        return nullptr;
    }
    if (!is_power_of_two(alignment) || alignment > kMaxSupportedAlignment) {
        set_result(AllocResult::InvalidAlignment);
        return nullptr;
    }

    std::size_t min_block_size = round_up(sizeof(BlockHeader) + sizeof(BlockFooter), kMaxSupportedAlignment);
    std::size_t needed_size =
        std::max(min_block_size, round_up(sizeof(BlockHeader) + size + sizeof(BlockFooter), kMaxSupportedAlignment));

    BlockHeader *block = find_fit(needed_size);
    if (block == nullptr) {
        set_result(AllocResult::OutOfMemory);
        return nullptr;
    }

    free_list_remove(block);
    split_block(block, needed_size);

    block->is_free = false;
    block->magic = kAllocMagic;
    write_footer(block);

    bytes_in_use_ += block->block_size;
    peak_bytes_in_use_ = std::max(peak_bytes_in_use_, bytes_in_use_);
    ++allocation_count_;

    assert(check_invariants());
    set_result(AllocResult::Ok);
    return reinterpret_cast<std::byte *>(block) + sizeof(BlockHeader);
}

DeallocResult FreeListAllocator::deallocate(void *ptr) noexcept {
    if (ptr == nullptr) {
        return DeallocResult::NullPointer;
    }
    if (!owns(ptr)) {
        return DeallocResult::NotOwned;
    }

    auto *block = reinterpret_cast<BlockHeader *>(reinterpret_cast<std::byte *>(ptr) - sizeof(BlockHeader));

    if (block->magic == kFreeMagic) {
        return DeallocResult::DoubleFree;
    }
    if (block->magic != kAllocMagic) {
        return DeallocResult::CorruptedHeader;
    }
    if (block->block_size == 0 || block->block_size % kMaxSupportedAlignment != 0 ||
        reinterpret_cast<std::byte *>(block) + block->block_size > arena_base_ + capacity_bytes_) {
        return DeallocResult::CorruptedHeader;
    }

    bytes_in_use_ -= block->block_size;
    --allocation_count_;

    BlockHeader *merged = block;

    BlockHeader *next = next_physical(merged);
    if (next != nullptr && next->magic == kFreeMagic) {
        free_list_remove(next);
        merged->block_size += next->block_size;
    }

    BlockHeader *prev = prev_physical(merged);
    if (prev != nullptr && prev->magic == kFreeMagic) {
        free_list_remove(prev);
        prev->block_size += merged->block_size;
        merged = prev;
    }

    free_list_insert(merged);

    assert(check_invariants());
    return DeallocResult::Ok;
}

void FreeListAllocator::reset() noexcept {
    free_list_head_ = nullptr;
    bytes_in_use_ = 0;
    peak_bytes_in_use_ = 0;
    allocation_count_ = 0;

    BlockHeader *block = first_block();
    block->block_size = capacity_bytes_;
    block->prev_free = nullptr;
    block->next_free = nullptr;
    free_list_insert(block);
}

AllocatorStats FreeListAllocator::stats() const noexcept {
    AllocatorStats s;
    s.capacity_bytes = capacity_bytes_;
    s.bytes_in_use = bytes_in_use_;
    s.peak_bytes_in_use = peak_bytes_in_use_;
    s.allocation_count = allocation_count_;

    std::size_t count = 0;
    std::size_t largest = 0;
    for (BlockHeader *b = free_list_head_; b != nullptr; b = b->next_free) {
        ++count;
        largest = std::max(largest, b->block_size);
    }
    s.free_block_count = count;
    s.largest_free_block = largest;
    return s;
}

bool FreeListAllocator::owns(const void *ptr) const noexcept {
    const auto *byte_ptr = static_cast<const std::byte *>(ptr);
    return byte_ptr >= arena_base_ + sizeof(BlockHeader) && byte_ptr < arena_base_ + capacity_bytes_;
}

bool FreeListAllocator::check_invariants() const noexcept {
    if (capacity_bytes_ == 0) {
        return free_list_head_ == nullptr;
    }

    std::size_t total_seen = 0;
    std::size_t free_blocks_seen = 0;
    bool prev_was_free = false;

    for (BlockHeader *b = first_block();;) {
        if (b->block_size == 0 || b->block_size % kMaxSupportedAlignment != 0) {
            return false;
        }
        if (b->magic != kFreeMagic && b->magic != kAllocMagic) {
            return false;
        }
        const auto *footer =
            reinterpret_cast<const BlockFooter *>(reinterpret_cast<const std::byte *>(b) + b->block_size - sizeof(BlockFooter));
        if (footer->block_size != b->block_size || footer->magic != b->magic) {
            return false;
        }
        bool is_free_now = (b->magic == kFreeMagic);
        if (is_free_now != b->is_free) {
            return false;
        }
        if (is_free_now && prev_was_free) {
            return false;  // two physically-adjacent free blocks: should have been coalesced
        }
        prev_was_free = is_free_now;
        if (is_free_now) {
            ++free_blocks_seen;
        }

        total_seen += b->block_size;
        BlockHeader *next = next_physical(b);
        if (next == nullptr) {
            break;
        }
        b = next;
    }

    if (total_seen != capacity_bytes_) {
        return false;
    }

    // Free list must contain exactly the free blocks found above, each
    // with consistent prev/next symmetry.
    std::size_t list_count = 0;
    for (BlockHeader *b = free_list_head_; b != nullptr; b = b->next_free) {
        if (b->magic != kFreeMagic) {
            return false;
        }
        if (b->next_free != nullptr && b->next_free->prev_free != b) {
            return false;
        }
        if (b->prev_free != nullptr && b->prev_free->next_free != b) {
            return false;
        }
        if (b->prev_free == nullptr && free_list_head_ != b) {
            return false;
        }
        ++list_count;
    }

    return list_count == free_blocks_seen;
}

}  // namespace cma
