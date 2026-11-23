// Randomized property test: a long, deterministic (fixed-seed, so
// never flaky) sequence of random allocate/deallocate calls against
// FreeListAllocator must never produce two live allocations whose
// byte ranges overlap, must never return a misaligned pointer, must
// never return a pointer outside the allocator's owned range, and
// must leave check_invariants() satisfied after every single
// operation.
#include "allocator/free_list_allocator.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <random>
#include <vector>

using cma::FitPolicy;
using cma::FreeListAllocator;

namespace {

struct LiveAllocation {
    std::uintptr_t start;
    std::size_t size;
    std::size_t alignment;
};

bool ranges_overlap(std::uintptr_t a_start, std::size_t a_size, std::uintptr_t b_start, std::size_t b_size) {
    return a_start < b_start + b_size && b_start < a_start + a_size;
}

void run_property_test(FitPolicy policy, unsigned seed) {
    constexpr std::size_t kCapacity = 1 << 16;
    FreeListAllocator alloc(kCapacity, policy);

    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> size_dist(1, 300);
    std::uniform_int_distribution<int> alignment_pow_dist(0, 6);  // 2^0..2^6 == 1..64
    std::uniform_int_distribution<int> action_dist(0, 1);

    std::vector<LiveAllocation> live;

    for (int iteration = 0; iteration < 5000; ++iteration) {
        bool do_allocate = live.empty() || action_dist(rng) == 0;

        if (do_allocate) {
            std::size_t size = static_cast<std::size_t>(size_dist(rng));
            std::size_t alignment = std::size_t{1} << alignment_pow_dist(rng);

            void *p = alloc.allocate(size, alignment, nullptr);
            if (p == nullptr) {
                // Legitimate OOM is allowed (the arena is finite); just
                // skip this iteration, don't treat it as a failure.
                continue;
            }

            auto start = reinterpret_cast<std::uintptr_t>(p);
            ASSERT_EQ(start % alignment, 0u) << "misaligned pointer for alignment=" << alignment;
            ASSERT_TRUE(alloc.owns(p));

            for (const auto &other : live) {
                ASSERT_FALSE(ranges_overlap(start, size, other.start, other.size))
                    << "overlap detected between live allocations";
            }
            live.push_back({start, size, alignment});
        } else {
            std::uniform_int_distribution<std::size_t> pick(0, live.size() - 1);
            std::size_t index = pick(rng);
            void *p = reinterpret_cast<void *>(live[index].start);
            ASSERT_EQ(alloc.deallocate(p), cma::DeallocResult::Ok);
            live.erase(live.begin() + static_cast<std::ptrdiff_t>(index));
        }

        ASSERT_TRUE(alloc.check_invariants()) << "invariant violated at iteration " << iteration;
    }

    // Free everything that's left and confirm the arena fully coalesces.
    for (const auto &a : live) {
        ASSERT_EQ(alloc.deallocate(reinterpret_cast<void *>(a.start)), cma::DeallocResult::Ok);
    }
    cma::AllocatorStats stats = alloc.stats();
    EXPECT_EQ(stats.bytes_in_use, 0u);
    EXPECT_EQ(stats.free_block_count, 1u);
    EXPECT_EQ(stats.largest_free_block, stats.capacity_bytes);
}

}  // namespace

TEST(PropertyBounds, FirstFitSeed1) { run_property_test(FitPolicy::FirstFit, 1u); }
TEST(PropertyBounds, FirstFitSeed2) { run_property_test(FitPolicy::FirstFit, 2u); }
TEST(PropertyBounds, BestFitSeed1) { run_property_test(FitPolicy::BestFit, 1u); }
TEST(PropertyBounds, BestFitSeed2) { run_property_test(FitPolicy::BestFit, 2u); }
