#ifndef CUSTOM_MEMORY_ALLOCATOR_ALLOCATOR_STATS_HPP
#define CUSTOM_MEMORY_ALLOCATOR_ALLOCATOR_STATS_HPP

#include <cstddef>

namespace cma {

// Snapshot of an allocator's bookkeeping at one point in time. Every
// strategy (arena, free-list) fills this in the same shape so the
// facade and the benchmarks can report on either one uniformly.
struct AllocatorStats {
    std::size_t capacity_bytes = 0;       // total backing storage
    std::size_t bytes_in_use = 0;         // currently allocated, not yet freed
    std::size_t peak_bytes_in_use = 0;    // high-water mark since construction/reset
    std::size_t free_block_count = 0;     // number of distinct free regions
    std::size_t largest_free_block = 0;   // bytes in the single largest free region
    std::size_t allocation_count = 0;     // live (not yet freed) allocation count
};

// Why a request could not be satisfied, or why a deallocate() was
// rejected. Reported by value instead of via exception or abort()
// specifically so tests (and a host embedding this allocator) can
// observe and react to corruption/misuse instead of crashing the
// process, per the "safe invalid/double-free diagnosis" requirement.
enum class AllocResult {
    Ok,
    OutOfMemory,          // request does not fit in any free region
    InvalidAlignment,     // alignment is zero or not a power of two
    InvalidSize,          // size is zero
};

enum class DeallocResult {
    Ok,
    NullPointer,          // ptr == nullptr (a no-op, reported distinctly for diagnostics)
    NotOwned,              // ptr does not fall within this allocator's backing storage
    DoubleFree,            // ptr falls within range but its block is already free
    CorruptedHeader,       // ptr's block header failed a magic-number/invariant check
};

}  // namespace cma

#endif  // CUSTOM_MEMORY_ALLOCATOR_ALLOCATOR_STATS_HPP
