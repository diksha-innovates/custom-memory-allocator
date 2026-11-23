#ifndef CUSTOM_MEMORY_ALLOCATOR_FREE_LIST_ALLOCATOR_HPP
#define CUSTOM_MEMORY_ALLOCATOR_FREE_LIST_ALLOCATOR_HPP

#include <cstddef>
#include <cstdint>
#include <memory>

#include "allocator/allocator_stats.hpp"
#include "allocator/allocator_strategy.hpp"

namespace cma {

enum class FitPolicy {
    FirstFit,  // walk the free list, take the first block that fits
    BestFit,   // walk the whole free list, take the smallest block that fits
};

// Explicit free-list allocator with boundary-tag (header+footer)
// coalescing, first-fit/best-fit placement, and block splitting.
//
// Every block (free or allocated) in the arena has a BlockHeader at
// its start and a BlockFooter at its end; the footer lets a
// deallocate() find and inspect the *previous* physical block (to
// merge backward) without needing a separate "previous block" index -
// the classic boundary-tag technique. Free blocks additionally carry
// intrusive prev/next pointers in their header forming a doubly-linked
// free list, so insertion/removal during splitting and coalescing is
// O(1) once the block is known.
//
// Alignment: every block's address is a multiple of kMaxSupportedAlignment
// by construction (the arena's base address is rounded up to it, every
// BlockHeader is padded via alignas() to a multiple of it, and every
// block's total size is rounded up to a multiple of it) - so the
// payload pointer (immediately after the header) is automatically
// aligned to kMaxSupportedAlignment, and therefore to any smaller
// power-of-two alignment too. Requests for an alignment greater than
// kMaxSupportedAlignment are rejected cleanly (AllocResult::InvalidAlignment)
// rather than silently producing a misaligned pointer. See README
// "Design decisions" for why this trade-off (a fixed, generous cap
// instead of unbounded alignment support via a back-pointer trick) was
// made.
class FreeListAllocator final : public AllocatorStrategy {
public:
    static constexpr std::size_t kMaxSupportedAlignment = 64;

    FreeListAllocator(std::size_t capacity_bytes, FitPolicy policy);

    FreeListAllocator(const FreeListAllocator &) = delete;
    FreeListAllocator &operator=(const FreeListAllocator &) = delete;
    FreeListAllocator(FreeListAllocator &&) = delete;
    FreeListAllocator &operator=(FreeListAllocator &&) = delete;
    ~FreeListAllocator() override = default;

    void *allocate(std::size_t size, std::size_t alignment, AllocResult *out_result) noexcept override;
    DeallocResult deallocate(void *ptr) noexcept override;
    void reset() noexcept override;
    AllocatorStats stats() const noexcept override;
    bool owns(const void *ptr) const noexcept override;

    // Walks every block (not just free ones) and verifies every
    // boundary-tag / free-list invariant holds: header/footer sizes
    // agree, magic numbers are one of the two valid values, physical
    // blocks exactly tile the arena with no gaps or overlaps, no two
    // physically-adjacent blocks are both free (they should have been
    // coalesced), and the free list contains exactly the blocks marked
    // free (no more, no less), each with consistent prev/next
    // pointers. Returns true iff every invariant holds. Called after
    // every mutating operation in debug builds (see .cpp) and exposed
    // publicly so tests can call it explicitly too.
    bool check_invariants() const noexcept;

private:
    struct BlockHeader;
    struct BlockFooter;

    BlockHeader *first_block() const noexcept;
    BlockHeader *next_physical(BlockHeader *block) const noexcept;
    BlockHeader *prev_physical(BlockHeader *block) const noexcept;
    bool is_last_physical(const BlockHeader *block) const noexcept;

    void free_list_insert(BlockHeader *block) noexcept;
    void free_list_remove(BlockHeader *block) noexcept;
    BlockHeader *find_fit(std::size_t needed_size) const noexcept;
    void write_footer(BlockHeader *block) noexcept;
    void split_block(BlockHeader *block, std::size_t needed_size) noexcept;

    std::unique_ptr<std::byte[]> raw_storage_;  // the actual owned allocation, possibly over-aligned
    std::byte *arena_base_ = nullptr;           // aligned start of usable arena within raw_storage_
    std::size_t capacity_bytes_ = 0;            // usable arena size starting at arena_base_
    FitPolicy policy_;

    BlockHeader *free_list_head_ = nullptr;

    std::size_t bytes_in_use_ = 0;
    std::size_t peak_bytes_in_use_ = 0;
    std::size_t allocation_count_ = 0;
};

}  // namespace cma

#endif  // CUSTOM_MEMORY_ALLOCATOR_FREE_LIST_ALLOCATOR_HPP
