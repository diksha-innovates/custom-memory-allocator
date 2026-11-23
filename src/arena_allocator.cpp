#include "allocator/arena_allocator.hpp"

#include <cstdint>

namespace cma {

namespace {
bool is_power_of_two(std::size_t v) { return v != 0 && (v & (v - 1)) == 0; }
}  // namespace

ArenaAllocator::ArenaAllocator(std::size_t capacity_bytes)
    : storage_(std::make_unique<std::byte[]>(capacity_bytes)), capacity_bytes_(capacity_bytes) {}

void *ArenaAllocator::allocate(std::size_t size, std::size_t alignment, AllocResult *out_result) noexcept {
    auto set_result = [&](AllocResult r) {
        if (out_result != nullptr) {
            *out_result = r;
        }
    };

    if (size == 0) {
        set_result(AllocResult::InvalidSize);
        return nullptr;
    }
    if (!is_power_of_two(alignment)) {
        set_result(AllocResult::InvalidAlignment);
        return nullptr;
    }

    std::byte *current = storage_.get() + offset_;
    auto addr = reinterpret_cast<std::uintptr_t>(current);
    std::uintptr_t aligned_addr = (addr + (alignment - 1)) & ~(alignment - 1);
    std::size_t padding = static_cast<std::size_t>(aligned_addr - addr);

    if (padding > capacity_bytes_ - offset_ || size > capacity_bytes_ - offset_ - padding) {
        set_result(AllocResult::OutOfMemory);
        return nullptr;
    }

    offset_ += padding + size;
    ++allocation_count_;
    if (offset_ > peak_bytes_in_use_) {
        peak_bytes_in_use_ = offset_;
    }

    set_result(AllocResult::Ok);
    return reinterpret_cast<void *>(aligned_addr);
}

DeallocResult ArenaAllocator::deallocate(void *ptr) noexcept {
    if (ptr == nullptr) {
        return DeallocResult::NullPointer;
    }
    if (!owns(ptr)) {
        return DeallocResult::NotOwned;
    }
    // Documented no-op: a bump allocator cannot reclaim one allocation
    // among many. The byte range stays counted in bytes_in_use until
    // reset() - see class comment.
    return DeallocResult::Ok;
}

void ArenaAllocator::reset() noexcept {
    offset_ = 0;
    allocation_count_ = 0;
    peak_bytes_in_use_ = 0;
}

AllocatorStats ArenaAllocator::stats() const noexcept {
    AllocatorStats s;
    s.capacity_bytes = capacity_bytes_;
    s.bytes_in_use = offset_;
    s.peak_bytes_in_use = peak_bytes_in_use_;
    s.free_block_count = (capacity_bytes_ - offset_) > 0 ? 1 : 0;
    s.largest_free_block = capacity_bytes_ - offset_;
    s.allocation_count = allocation_count_;
    return s;
}

bool ArenaAllocator::owns(const void *ptr) const noexcept {
    const auto *byte_ptr = static_cast<const std::byte *>(ptr);
    return byte_ptr >= storage_.get() && byte_ptr < storage_.get() + capacity_bytes_;
}

}  // namespace cma
