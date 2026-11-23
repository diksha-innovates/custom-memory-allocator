#include "allocator/arena_allocator.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

using cma::AllocResult;
using cma::ArenaAllocator;
using cma::DeallocResult;

namespace {
bool is_aligned(const void *ptr, std::size_t alignment) {
    return (reinterpret_cast<std::uintptr_t>(ptr) % alignment) == 0;
}
}  // namespace

TEST(ArenaAllocator, AllocatesNonNullAndAdvancesStats) {
    ArenaAllocator arena(4096);
    AllocResult result = AllocResult::Ok;
    void *p = arena.allocate(64, 8, &result);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(result, AllocResult::Ok);

    cma::AllocatorStats stats = arena.stats();
    EXPECT_EQ(stats.capacity_bytes, 4096u);
    EXPECT_GE(stats.bytes_in_use, 64u);
    EXPECT_EQ(stats.allocation_count, 1u);
}

TEST(ArenaAllocator, SupportsEveryPowerOfTwoAlignmentUpTo512) {
    ArenaAllocator arena(1u << 20);
    for (std::size_t alignment = 1; alignment <= 512; alignment *= 2) {
        AllocResult result = AllocResult::Ok;
        void *p = arena.allocate(37, alignment, &result);
        ASSERT_NE(p, nullptr) << "alignment=" << alignment;
        EXPECT_EQ(result, AllocResult::Ok);
        EXPECT_TRUE(is_aligned(p, alignment)) << "alignment=" << alignment;
    }
}

TEST(ArenaAllocator, RejectsZeroSizeAndNonPowerOfTwoAlignment) {
    ArenaAllocator arena(1024);
    AllocResult result = AllocResult::Ok;

    EXPECT_EQ(arena.allocate(0, 8, &result), nullptr);
    EXPECT_EQ(result, AllocResult::InvalidSize);

    EXPECT_EQ(arena.allocate(16, 3, &result), nullptr);
    EXPECT_EQ(result, AllocResult::InvalidAlignment);

    EXPECT_EQ(arena.allocate(16, 0, &result), nullptr);
    EXPECT_EQ(result, AllocResult::InvalidAlignment);
}

TEST(ArenaAllocator, ExhaustionReportsOutOfMemoryWithoutCrashing) {
    ArenaAllocator arena(128);
    AllocResult result = AllocResult::Ok;

    void *p1 = arena.allocate(100, 1, &result);
    ASSERT_NE(p1, nullptr);

    void *p2 = arena.allocate(100, 1, &result);
    EXPECT_EQ(p2, nullptr);
    EXPECT_EQ(result, AllocResult::OutOfMemory);

    // The allocator must still be usable for a smaller request.
    void *p3 = arena.allocate(16, 1, &result);
    EXPECT_NE(p3, nullptr);
    EXPECT_EQ(result, AllocResult::Ok);
}

TEST(ArenaAllocator, DeallocateIsDocumentedNoOpButRejectsForeignPointers) {
    ArenaAllocator arena(1024);
    void *p = arena.allocate(32, 8, nullptr);
    ASSERT_NE(p, nullptr);

    EXPECT_EQ(arena.deallocate(p), DeallocResult::Ok);
    // bytes_in_use is unaffected by the no-op deallocate: a bump
    // allocator cannot reclaim one allocation among many.
    EXPECT_GE(arena.stats().bytes_in_use, 32u);

    int stack_variable = 0;
    EXPECT_EQ(arena.deallocate(&stack_variable), DeallocResult::NotOwned);
    EXPECT_EQ(arena.deallocate(nullptr), DeallocResult::NullPointer);
}

TEST(ArenaAllocator, ResetReclaimsAllSpaceAndClearsStats) {
    ArenaAllocator arena(1024);
    for (int i = 0; i < 10; ++i) {
        ASSERT_NE(arena.allocate(64, 8, nullptr), nullptr);
    }
    EXPECT_GT(arena.stats().bytes_in_use, 0u);

    arena.reset();
    cma::AllocatorStats stats = arena.stats();
    EXPECT_EQ(stats.bytes_in_use, 0u);
    EXPECT_EQ(stats.allocation_count, 0u);
    EXPECT_EQ(stats.peak_bytes_in_use, 0u);
    EXPECT_EQ(stats.largest_free_block, 1024u);

    // And the space is genuinely reusable, not just reset on paper.
    void *p = arena.allocate(1024, 1, nullptr);
    EXPECT_NE(p, nullptr);
}

TEST(ArenaAllocator, SuccessiveAllocationsDoNotOverlap) {
    ArenaAllocator arena(4096);
    std::vector<std::pair<std::uintptr_t, std::size_t>> live;
    for (int i = 0; i < 20; ++i) {
        std::size_t size = static_cast<std::size_t>(16 + i * 7);
        void *p = arena.allocate(size, 8, nullptr);
        ASSERT_NE(p, nullptr);
        auto start = reinterpret_cast<std::uintptr_t>(p);
        for (auto &[other_start, other_size] : live) {
            bool overlap = start < other_start + other_size && other_start < start + size;
            EXPECT_FALSE(overlap);
        }
        live.emplace_back(start, size);
    }
}
