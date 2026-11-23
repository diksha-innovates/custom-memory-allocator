#ifndef CUSTOM_MEMORY_ALLOCATOR_MEMORY_ALLOCATOR_HPP
#define CUSTOM_MEMORY_ALLOCATOR_MEMORY_ALLOCATOR_HPP

#include <cstddef>
#include <memory>
#include <new>
#include <utility>

#include "allocator/allocator_stats.hpp"
#include "allocator/allocator_strategy.hpp"

namespace cma {

// Facade: the one type application code is meant to talk to. It owns
// a concrete AllocatorStrategy (arena or free-list) behind a
// unique_ptr and forwards every call to it, so callers never depend
// on which strategy is in play - swapping ArenaAllocator for
// FreeListAllocator (or vice versa) is a one-line change at
// construction, nothing else in calling code changes. This is the
// "facade separating API from strategy" required by the project spec:
// MemoryAllocator is the API, AllocatorStrategy's concrete subclasses
// are the strategy.
class MemoryAllocator {
public:
    explicit MemoryAllocator(std::unique_ptr<AllocatorStrategy> strategy) : strategy_(std::move(strategy)) {}

    MemoryAllocator(const MemoryAllocator &) = delete;
    MemoryAllocator &operator=(const MemoryAllocator &) = delete;
    MemoryAllocator(MemoryAllocator &&) = default;
    MemoryAllocator &operator=(MemoryAllocator &&) = default;

    void *allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t),
                    AllocResult *out_result = nullptr) noexcept {
        return strategy_->allocate(size, alignment, out_result);
    }

    DeallocResult deallocate(void *ptr) noexcept { return strategy_->deallocate(ptr); }

    void reset() noexcept { strategy_->reset(); }

    AllocatorStats stats() const noexcept { return strategy_->stats(); }

    bool owns(const void *ptr) const noexcept { return strategy_->owns(ptr); }

    AllocatorStrategy &strategy() noexcept { return *strategy_; }
    const AllocatorStrategy &strategy() const noexcept { return *strategy_; }

    // Convenience typed helpers: construct/destroy a single object of
    // type T through this allocator (placement-new over allocate(),
    // explicit destructor call before deallocate()). Not used for
    // arrays - callers needing arrays should call allocate() directly
    // and placement-new each element themselves, since this allocator
    // has no notion of array cookies the way ::operator new[] does.
    template <typename T, typename... Args>
    T *create(Args &&...args) {
        void *mem = allocate(sizeof(T), alignof(T));
        if (mem == nullptr) {
            return nullptr;
        }
        return ::new (mem) T(std::forward<Args>(args)...);
    }

    template <typename T>
    void destroy(T *obj) noexcept {
        if (obj == nullptr) {
            return;
        }
        obj->~T();
        deallocate(obj);
    }

private:
    std::unique_ptr<AllocatorStrategy> strategy_;
};

}  // namespace cma

#endif  // CUSTOM_MEMORY_ALLOCATOR_MEMORY_ALLOCATOR_HPP
