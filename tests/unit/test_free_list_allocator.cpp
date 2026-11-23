#include "allocator/free_list_allocator.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

using cma::AllocResult;
using cma::DeallocResult;
using cma::FitPolicy;
using cma::FreeListAllocator;

namespace {
bool is_aligned(const void *ptr, std::size_t alignment) {
    return (reinterpret_cast<std::uintptr_t>(ptr) % alignment) == 0;
}
}  // namespace

TEST(FreeListAllocator, AllocatesAndInvariantsHold) {
    FreeListAllocator alloc(4096, FitPolicy::FirstFit);
    void *p = alloc.allocate(100, 8, nullptr);
    ASSERT_NE(p, nullptr);
    EXPECT_TRUE(alloc.check_invariants());
    EXPECT_EQ(alloc.stats().allocation_count, 1u);
}

TEST(FreeListAllocator, SupportsEveryPowerOfTwoAlignmentUpToMax) {
    FreeListAllocator alloc(1u << 16, FitPolicy::FirstFit);
    for (std::size_t alignment = 1; alignment <= FreeListAllocator::kMaxSupportedAlignment; alignment *= 2) {
        AllocResult result = AllocResult::Ok;
        void *p = alloc.allocate(23, alignment, &result);
        ASSERT_NE(p, nullptr) << "alignment=" << alignment;
        EXPECT_EQ(result, AllocResult::Ok);
        EXPECT_TRUE(is_aligned(p, alignment)) << "alignment=" << alignment;
    }
    EXPECT_TRUE(alloc.check_invariants());
}

TEST(FreeListAllocator, RejectsAlignmentBeyondMaxSupported) {
    FreeListAllocator alloc(4096, FitPolicy::FirstFit);
    AllocResult result = AllocResult::Ok;
    void *p = alloc.allocate(16, FreeListAllocator::kMaxSupportedAlignment * 2, &result);
    EXPECT_EQ(p, nullptr);
    EXPECT_EQ(result, AllocResult::InvalidAlignment);
}

TEST(FreeListAllocator, RejectsZeroSizeAndNonPowerOfTwoAlignment) {
    FreeListAllocator alloc(4096, FitPolicy::FirstFit);
    AllocResult result = AllocResult::Ok;

    EXPECT_EQ(alloc.allocate(0, 8, &result), nullptr);
    EXPECT_EQ(result, AllocResult::InvalidSize);

    EXPECT_EQ(alloc.allocate(16, 6, &result), nullptr);
    EXPECT_EQ(result, AllocResult::InvalidAlignment);
}

TEST(FreeListAllocator, SplittingLeavesARemainderFreeBlock) {
    FreeListAllocator alloc(4096, FitPolicy::FirstFit);
    cma::AllocatorStats before = alloc.stats();
    EXPECT_EQ(before.free_block_count, 1u);

    void *p = alloc.allocate(64, 8, nullptr);
    ASSERT_NE(p, nullptr);

    cma::AllocatorStats after = alloc.stats();
    // The 4096-byte arena is far larger than one 64-byte request plus
    // header/footer overhead, so the free block must have been split:
    // one free block remains, but it's smaller than the original.
    EXPECT_EQ(after.free_block_count, 1u);
    EXPECT_LT(after.largest_free_block, before.largest_free_block);
    EXPECT_TRUE(alloc.check_invariants());
}

TEST(FreeListAllocator, FreeingAllAllocationsCoalescesBackToOneRegion) {
    FreeListAllocator alloc(4096, FitPolicy::FirstFit);
    std::vector<void *> ptrs;
    for (int i = 0; i < 6; ++i) {
        void *p = alloc.allocate(100, 8, nullptr);
        ASSERT_NE(p, nullptr);
        ptrs.push_back(p);
    }
    EXPECT_TRUE(alloc.check_invariants());

    // Free in a deliberately non-sequential order so both
    // forward-and-backward coalescing paths are exercised.
    std::vector<int> order = {2, 0, 4, 1, 5, 3};
    for (int idx : order) {
        EXPECT_EQ(alloc.deallocate(ptrs[static_cast<std::size_t>(idx)]), DeallocResult::Ok);
        EXPECT_TRUE(alloc.check_invariants());
    }

    cma::AllocatorStats stats = alloc.stats();
    EXPECT_EQ(stats.free_block_count, 1u);
    EXPECT_EQ(stats.largest_free_block, stats.capacity_bytes);
    EXPECT_EQ(stats.bytes_in_use, 0u);
    EXPECT_EQ(stats.allocation_count, 0u);
}

TEST(FreeListAllocator, ExhaustionReportsOutOfMemoryThenRecoversAfterFree) {
    FreeListAllocator alloc(512, FitPolicy::FirstFit);
    void *p1 = alloc.allocate(400, 8, nullptr);
    ASSERT_NE(p1, nullptr);

    AllocResult result = AllocResult::Ok;
    void *p2 = alloc.allocate(400, 8, &result);
    EXPECT_EQ(p2, nullptr);
    EXPECT_EQ(result, AllocResult::OutOfMemory);

    EXPECT_EQ(alloc.deallocate(p1), DeallocResult::Ok);
    void *p3 = alloc.allocate(400, 8, &result);
    EXPECT_NE(p3, nullptr);
    EXPECT_EQ(result, AllocResult::Ok);
}

TEST(FreeListAllocator, DoubleFreeIsDetectedAndDoesNotCorruptState) {
    FreeListAllocator alloc(4096, FitPolicy::FirstFit);
    void *p = alloc.allocate(64, 8, nullptr);
    ASSERT_NE(p, nullptr);

    EXPECT_EQ(alloc.deallocate(p), DeallocResult::Ok);
    EXPECT_EQ(alloc.deallocate(p), DeallocResult::DoubleFree);
    EXPECT_TRUE(alloc.check_invariants());

    // The allocator must still be fully usable afterward.
    void *p2 = alloc.allocate(64, 8, nullptr);
    EXPECT_NE(p2, nullptr);
}

TEST(FreeListAllocator, InvalidPointerIsRejectedCleanly) {
    FreeListAllocator alloc(4096, FitPolicy::FirstFit);

    int stack_variable = 0;
    EXPECT_EQ(alloc.deallocate(&stack_variable), DeallocResult::NotOwned);
    EXPECT_EQ(alloc.deallocate(nullptr), DeallocResult::NullPointer);

    void *p = alloc.allocate(64, 8, nullptr);
    ASSERT_NE(p, nullptr);
    // A pointer inside the arena but not a payload address we ever
    // handed out (e.g. a few bytes into the payload) must not be
    // treated as a valid, freeable allocation.
    void *bogus = static_cast<std::byte *>(p) + 4;
    EXPECT_NE(alloc.deallocate(bogus), DeallocResult::Ok);
    EXPECT_TRUE(alloc.check_invariants());
}

TEST(FreeListAllocator, FirstFitPicksTheMostRecentlyFreedEligibleBlockBestFitPicksTheSmallest) {
    // Layout, left to right: [small][guard][large][guard]. Freeing
    // small then large (in that order) makes `large` the free-list
    // head (most recently freed) while `small` stays reachable later
    // in the list - exactly the scenario where first-fit and best-fit
    // disagree.
    std::uintptr_t first_fit_addr = 0;
    std::uintptr_t best_fit_addr = 0;
    {
        FreeListAllocator alloc(8192, FitPolicy::FirstFit);
        void *small = alloc.allocate(64, 8, nullptr);
        void *guard1 = alloc.allocate(32, 8, nullptr);
        void *large = alloc.allocate(2048, 8, nullptr);
        void *guard2 = alloc.allocate(32, 8, nullptr);
        (void)guard1;
        (void)guard2;
        auto small_addr = reinterpret_cast<std::uintptr_t>(small);
        alloc.deallocate(small);
        alloc.deallocate(large);
        void *result = alloc.allocate(48, 8, nullptr);
        first_fit_addr = reinterpret_cast<std::uintptr_t>(result);
        // First-fit must pick `large` (the free-list head), not `small`.
        EXPECT_NE(first_fit_addr, small_addr);
    }
    {
        FreeListAllocator alloc(8192, FitPolicy::BestFit);
        void *small = alloc.allocate(64, 8, nullptr);
        void *guard1 = alloc.allocate(32, 8, nullptr);
        void *large = alloc.allocate(2048, 8, nullptr);
        void *guard2 = alloc.allocate(32, 8, nullptr);
        (void)guard1;
        (void)guard2;
        auto small_addr = reinterpret_cast<std::uintptr_t>(small);
        alloc.deallocate(small);
        alloc.deallocate(large);
        void *result = alloc.allocate(48, 8, nullptr);
        best_fit_addr = reinterpret_cast<std::uintptr_t>(result);
        // Best-fit must pick `small`, the smallest block that still fits.
        EXPECT_EQ(best_fit_addr, small_addr);
    }
    EXPECT_NE(first_fit_addr, best_fit_addr);
}

TEST(FreeListAllocator, ResetReclaimsEverythingAndIsReusable) {
    FreeListAllocator alloc(2048, FitPolicy::FirstFit);
    for (int i = 0; i < 5; ++i) {
        ASSERT_NE(alloc.allocate(64, 8, nullptr), nullptr);
    }
    alloc.reset();
    cma::AllocatorStats stats = alloc.stats();
    EXPECT_EQ(stats.bytes_in_use, 0u);
    EXPECT_EQ(stats.allocation_count, 0u);
    EXPECT_EQ(stats.free_block_count, 1u);
    EXPECT_EQ(stats.largest_free_block, stats.capacity_bytes);
    EXPECT_TRUE(alloc.check_invariants());

    void *p = alloc.allocate(stats.capacity_bytes > 64 ? 64 : 1, 8, nullptr);
    EXPECT_NE(p, nullptr);
}
