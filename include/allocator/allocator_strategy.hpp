#ifndef CUSTOM_MEMORY_ALLOCATOR_ALLOCATOR_STRATEGY_HPP
#define CUSTOM_MEMORY_ALLOCATOR_ALLOCATOR_STRATEGY_HPP

#include <cstddef>

#include "allocator/allocator_stats.hpp"

namespace cma {

// Pure strategy interface. Every concrete allocation algorithm (arena
// bump allocator, explicit free-list allocator) implements exactly
// this, and nothing else - no strategy knows about std::pmr, no
// strategy knows about the other. The MemoryAllocator facade
// (memory_allocator.hpp) is the only thing that depends on this
// interface directly; everything else depends on a concrete strategy
// or on the facade, never both.
class AllocatorStrategy {
public:
    virtual ~AllocatorStrategy() = default;

    // Returns a pointer to a region of at least `size` bytes, aligned
    // to `alignment` (must be a power of two), or nullptr on failure
    // (see out_result for why). Never throws.
    virtual void *allocate(std::size_t size, std::size_t alignment, AllocResult *out_result) noexcept = 0;

    // Releases a region previously returned by allocate() on this same
    // object. Returns a DeallocResult rather than silently succeeding
    // or aborting so misuse (double free, invalid pointer) can be
    // observed and tested rather than corrupting memory or crashing
    // the process.
    virtual DeallocResult deallocate(void *ptr) noexcept = 0;

    // Releases every outstanding allocation at once and returns the
    // backing storage to its initial empty state. Not all strategies
    // support freeing individual allocations cheaply; every strategy
    // supports reset().
    virtual void reset() noexcept = 0;

    virtual AllocatorStats stats() const noexcept = 0;

    // True if `ptr` falls within this allocator's backing storage
    // range at all (not necessarily currently allocated). Used by the
    // facade and by tests to distinguish "not owned by this allocator"
    // from "owned but already free" without duplicating range checks.
    virtual bool owns(const void *ptr) const noexcept = 0;
};

}  // namespace cma

#endif  // CUSTOM_MEMORY_ALLOCATOR_ALLOCATOR_STRATEGY_HPP
