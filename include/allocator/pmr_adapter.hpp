#ifndef CUSTOM_MEMORY_ALLOCATOR_PMR_ADAPTER_HPP
#define CUSTOM_MEMORY_ALLOCATOR_PMR_ADAPTER_HPP

#include <cassert>
#include <memory_resource>
#include <new>

#include "allocator/allocator_stats.hpp"
#include "allocator/memory_allocator.hpp"

namespace cma {

// Adapts a MemoryAllocator facade to the std::pmr::memory_resource
// interface, so any std::pmr container (std::pmr::vector,
// std::pmr::map, ...) can be backed by either of this project's
// allocation strategies.
//
// The std::pmr::memory_resource contract and this project's own
// AllocatorStrategy contract disagree on one thing: do_allocate() is
// required to THROW std::bad_alloc on failure (never return nullptr -
// callers like std::pmr::vector do not check for a null return), while
// AllocatorStrategy::allocate() reports failure by returning nullptr
// and never throws. This adapter is the single, explicit place that
// bridges the two: it translates a null/failed allocate() into a
// thrown std::bad_alloc, and nowhere else in this codebase has to
// know about exceptions at all.
class PmrAdapter final : public std::pmr::memory_resource {
public:
    explicit PmrAdapter(MemoryAllocator &allocator) : allocator_(allocator) {}

private:
    void *do_allocate(std::size_t bytes, std::size_t alignment) override {
        AllocResult result = AllocResult::Ok;
        void *ptr = allocator_.allocate(bytes, alignment, &result);
        if (ptr == nullptr) {
            throw std::bad_alloc();
        }
        return ptr;
    }

    void do_deallocate(void *ptr, std::size_t /*bytes*/, std::size_t /*alignment*/) override {
        DeallocResult result = allocator_.deallocate(ptr);
        // A well-behaved std::pmr container only ever passes back a
        // pointer (and matching size/alignment) it previously received
        // from do_allocate() on this exact resource, so `result` should
        // always be Ok here. The assert exists purely as a debug-build
        // tripwire against a pmr-container-side bug or memory
        // corruption; the pmr interface itself gives this function no
        // way to report failure to the caller (it returns void and is
        // not permitted to throw), so in a release build a non-Ok
        // result is silently ignored - documented in README
        // "Limitations".
        (void)result;
        assert(result == DeallocResult::Ok);
    }

    bool do_is_equal(const std::pmr::memory_resource &other) const noexcept override { return this == &other; }

    MemoryAllocator &allocator_;
};

}  // namespace cma

#endif  // CUSTOM_MEMORY_ALLOCATOR_PMR_ADAPTER_HPP
