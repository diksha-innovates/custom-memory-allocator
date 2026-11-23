#ifndef CUSTOM_MEMORY_ALLOCATOR_ARENA_ALLOCATOR_HPP
#define CUSTOM_MEMORY_ALLOCATOR_ARENA_ALLOCATOR_HPP

#include <cstddef>
#include <memory>

#include "allocator/allocator_stats.hpp"
#include "allocator/allocator_strategy.hpp"

namespace cma {

// A bump (arena) allocator: owns one contiguous buffer and hands out
// slices of it by advancing an offset. Individual deallocate() calls
// are intentionally a documented no-op (see deallocate() below) - the
// entire point of this strategy is O(1) allocation with zero
// per-allocation bookkeeping, which is incompatible with freeing
// anything other than "everything, all at once" via reset().
//
// Thread-unsafe by design, like every strategy in this project (see
// README "Limitations"): no internal locking.
class ArenaAllocator final : public AllocatorStrategy {
public:
    explicit ArenaAllocator(std::size_t capacity_bytes);

    ArenaAllocator(const ArenaAllocator &) = delete;
    ArenaAllocator &operator=(const ArenaAllocator &) = delete;
    ArenaAllocator(ArenaAllocator &&) = delete;
    ArenaAllocator &operator=(ArenaAllocator &&) = delete;
    ~ArenaAllocator() override = default;

    void *allocate(std::size_t size, std::size_t alignment, AllocResult *out_result) noexcept override;

    // Always a no-op that returns DeallocResult::Ok if `ptr` is owned
    // by this arena (even though the bytes are not actually reclaimed
    // until reset()), or DeallocResult::NotOwned otherwise. This is
    // documented, not silently wrong: a bump allocator cannot reclaim
    // one allocation out of many without a free list, which is exactly
    // what FreeListAllocator is for.
    DeallocResult deallocate(void *ptr) noexcept override;

    void reset() noexcept override;

    AllocatorStats stats() const noexcept override;

    bool owns(const void *ptr) const noexcept override;

private:
    std::unique_ptr<std::byte[]> storage_;
    std::size_t capacity_bytes_;
    std::size_t offset_ = 0;
    std::size_t peak_bytes_in_use_ = 0;
    std::size_t allocation_count_ = 0;  // allocations made since last reset(), never decremented by deallocate()
};

}  // namespace cma

#endif  // CUSTOM_MEMORY_ALLOCATOR_ARENA_ALLOCATOR_HPP
