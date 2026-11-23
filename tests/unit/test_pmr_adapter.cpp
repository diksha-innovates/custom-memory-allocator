#include "allocator/pmr_adapter.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <memory_resource>
#include <vector>

#include "allocator/free_list_allocator.hpp"
#include "allocator/memory_allocator.hpp"

using cma::FitPolicy;
using cma::FreeListAllocator;
using cma::MemoryAllocator;
using cma::PmrAdapter;

TEST(PmrAdapter, PmrVectorAllocatesAndFreesThroughOurAllocator) {
    MemoryAllocator allocator(std::make_unique<FreeListAllocator>(1 << 16, FitPolicy::FirstFit));
    PmrAdapter resource(allocator);

    {
        std::pmr::vector<int> v(&resource);
        for (int i = 0; i < 2000; ++i) {
            v.push_back(i);
        }
        for (int i = 0; i < 2000; ++i) {
            EXPECT_EQ(v[static_cast<std::size_t>(i)], i);
        }
        EXPECT_GT(allocator.stats().bytes_in_use, 0u);
    }

    // The vector's destructor must have deallocated everything through
    // our resource - no leak, bytes_in_use back to zero.
    EXPECT_EQ(allocator.stats().bytes_in_use, 0u);
    EXPECT_EQ(allocator.stats().allocation_count, 0u);
}

TEST(PmrAdapter, ThrowsBadAllocWhenUnderlyingAllocatorIsExhausted) {
    MemoryAllocator allocator(std::make_unique<FreeListAllocator>(256, FitPolicy::FirstFit));
    PmrAdapter resource(allocator);

    std::pmr::vector<std::uint64_t> v(&resource);
    EXPECT_THROW(v.reserve(10000), std::bad_alloc);
}

TEST(PmrAdapter, TwoAdaptersOverDifferentAllocatorsAreNotEqual) {
    MemoryAllocator allocator_a(std::make_unique<FreeListAllocator>(4096, FitPolicy::FirstFit));
    MemoryAllocator allocator_b(std::make_unique<FreeListAllocator>(4096, FitPolicy::FirstFit));
    PmrAdapter resource_a(allocator_a);
    PmrAdapter resource_b(allocator_b);

    EXPECT_TRUE(resource_a.is_equal(resource_a));
    EXPECT_FALSE(resource_a.is_equal(resource_b));
}
