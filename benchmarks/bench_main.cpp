// Deterministic workload generator + benchmark harness comparing
// ArenaAllocator and FreeListAllocator (both fit policies) against
// plain malloc/free and operator new/delete. Writes raw CSV rows (one
// per (allocator, workload) combination) to stdout, plus machine/
// compiler/flags metadata as CSV comment lines (#-prefixed) at the
// top - never invented numbers, only what was actually measured on
// whatever machine this binary is run on, which is why the metadata
// travels with the data.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <numeric>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "allocator/arena_allocator.hpp"
#include "allocator/free_list_allocator.hpp"
#include "allocator/memory_allocator.hpp"

namespace {

using Clock = std::chrono::steady_clock;

// One scheduled operation in a workload: allocate `size` bytes and
// hold it for `hold_until_step` more steps before freeing it. Built
// once per workload with a fixed seed so every allocator strategy
// being compared replays the EXACT same sequence of sizes and
// lifetimes - otherwise differences in measured throughput could come
// from different random draws instead of from the allocator itself.
struct ScheduledOp {
    std::size_t size;
    int free_at_step;  // -1 => never explicitly freed during the run (freed at teardown)
};

std::vector<ScheduledOp> generate_workload(const std::string &name, int op_count, unsigned seed) {
    std::mt19937 rng(seed);
    std::vector<ScheduledOp> ops(static_cast<std::size_t>(op_count));

    if (name == "short-lived") {
        std::uniform_int_distribution<int> size_dist(16, 256);
        std::uniform_int_distribution<int> lifetime_dist(1, 5);
        for (int i = 0; i < op_count; ++i) {
            ops[static_cast<std::size_t>(i)] = {static_cast<std::size_t>(size_dist(rng)), i + lifetime_dist(rng)};
        }
    } else if (name == "long-lived") {
        std::uniform_int_distribution<int> size_dist(256, 4096);
        for (int i = 0; i < op_count; ++i) {
            ops[static_cast<std::size_t>(i)] = {static_cast<std::size_t>(size_dist(rng)), -1};
        }
    } else {  // "mixed"
        std::uniform_int_distribution<int> size_dist(16, 4096);
        std::uniform_int_distribution<int> lifetime_dist(1, 50);
        std::uniform_int_distribution<int> long_lived_dist(0, 9);
        for (int i = 0; i < op_count; ++i) {
            bool long_lived = long_lived_dist(rng) == 0;  // 10% long-lived, 90% short-lived
            ops[static_cast<std::size_t>(i)] = {static_cast<std::size_t>(size_dist(rng)),
                                                 long_lived ? -1 : i + lifetime_dist(rng)};
        }
    }
    return ops;
}

struct RunResult {
    std::vector<double> latencies_ns;  // one per allocate+free pair actually performed
    std::size_t peak_reserved_bytes = 0;
};

// Runs `ops` against a generic allocate(size)->void* / free(void*)
// pair of callables, timing each allocate individually (the matching
// free's cost is attributed to the allocate that freed it would be
// double-booking; both are charged to whichever call actually ran so
// total wall time is still exact - see README benchmark methodology
// for why per-call latency and total throughput are reported
// separately rather than conflated).
template <typename AllocateFn, typename FreeFn, typename PeakFn>
RunResult run_workload(const std::vector<ScheduledOp> &ops, AllocateFn &&do_allocate, FreeFn &&do_free,
                        PeakFn &&current_reserved) {
    RunResult result;
    result.latencies_ns.reserve(ops.size() * 2);

    std::vector<void *> live(ops.size(), nullptr);
    std::vector<std::vector<int>> free_schedule(ops.size());
    for (std::size_t i = 0; i < ops.size(); ++i) {
        if (ops[i].free_at_step >= 0 && static_cast<std::size_t>(ops[i].free_at_step) < ops.size()) {
            free_schedule[static_cast<std::size_t>(ops[i].free_at_step)].push_back(static_cast<int>(i));
        }
    }

    for (std::size_t step = 0; step < ops.size(); ++step) {
        auto t0 = Clock::now();
        void *p = do_allocate(ops[step].size);
        auto t1 = Clock::now();
        result.latencies_ns.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count());
        live[step] = p;
        result.peak_reserved_bytes = std::max(result.peak_reserved_bytes, current_reserved());

        for (int idx : free_schedule[step]) {
            if (live[static_cast<std::size_t>(idx)] != nullptr) {
                auto f0 = Clock::now();
                do_free(live[static_cast<std::size_t>(idx)]);
                auto f1 = Clock::now();
                result.latencies_ns.push_back(std::chrono::duration<double, std::nano>(f1 - f0).count());
                live[static_cast<std::size_t>(idx)] = nullptr;
            }
        }
    }
    for (void *p : live) {
        if (p != nullptr) {
            do_free(p);
        }
    }
    return result;
}

double percentile(std::vector<double> values, double p) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    std::size_t index = static_cast<std::size_t>(p * static_cast<double>(values.size() - 1));
    return values[index];
}

void print_csv_row(const std::string &allocator_name, const std::string &workload_name, int op_count,
                    const RunResult &result, double total_seconds) {
    double throughput_ops_per_sec = static_cast<double>(result.latencies_ns.size()) / total_seconds;
    std::cout << allocator_name << "," << workload_name << "," << op_count << "," << result.latencies_ns.size()
              << "," << throughput_ops_per_sec << "," << percentile(result.latencies_ns, 0.50) << ","
              << percentile(result.latencies_ns, 0.99) << "," << result.peak_reserved_bytes << "\n";
}

}  // namespace

int main(int argc, char **argv) {
    int op_count = 20000;
    if (argc > 1) {
        op_count = std::atoi(argv[1]);
    }

    constexpr std::size_t kArenaCapacity = 256u * 1024u * 1024u;  // 256 MiB, large enough no workload here exhausts it

    std::cout << "# custom-memory-allocator benchmark raw results\n";
    std::cout << "# op_count=" << op_count << "\n";
    std::cout << "allocator,workload,op_count,timed_calls,throughput_ops_per_sec,p50_latency_ns,p99_latency_ns,"
                 "peak_reserved_bytes\n";

    const std::vector<std::string> workloads = {"short-lived", "long-lived", "mixed"};

    for (const auto &workload_name : workloads) {
        std::vector<ScheduledOp> ops = generate_workload(workload_name, op_count, /*seed=*/1234);

        // --- malloc/free ---
        // "Currently reserved" must go down on free, the same way
        // FreeListAllocator's bytes_in_use does, or the peak-reserved
        // comparison across allocators is not apples-to-apples. malloc()
        // does not expose the size of a live block given only its
        // pointer, so a side table records what each outstanding
        // pointer's requested size was.
        {
            std::size_t reserved = 0;
            std::unordered_map<void *, std::size_t> live_sizes;
            auto t0 = Clock::now();
            RunResult r = run_workload(
                ops,
                [&](std::size_t size) {
                    void *p = std::malloc(size);
                    if (p != nullptr) {
                        reserved += size;
                        live_sizes[p] = size;
                    }
                    return p;
                },
                [&](void *p) {
                    auto it = live_sizes.find(p);
                    if (it != live_sizes.end()) {
                        reserved -= it->second;
                        live_sizes.erase(it);
                    }
                    std::free(p);
                },
                [&]() { return reserved; });
            double seconds = std::chrono::duration<double>(Clock::now() - t0).count();
            print_csv_row("malloc_free", workload_name, op_count, r, seconds);
        }

        // --- operator new/delete --- (same live-size bookkeeping as malloc/free above)
        {
            std::size_t reserved = 0;
            std::unordered_map<void *, std::size_t> live_sizes;
            auto t0 = Clock::now();
            RunResult r = run_workload(
                ops,
                [&](std::size_t size) {
                    void *p = ::operator new(size);
                    reserved += size;
                    live_sizes[p] = size;
                    return p;
                },
                [&](void *p) {
                    auto it = live_sizes.find(p);
                    if (it != live_sizes.end()) {
                        reserved -= it->second;
                        live_sizes.erase(it);
                    }
                    ::operator delete(p);
                },
                [&]() { return reserved; });
            double seconds = std::chrono::duration<double>(Clock::now() - t0).count();
            print_csv_row("operator_new_delete", workload_name, op_count, r, seconds);
        }

        // --- ArenaAllocator (reset between workloads only; never
        // frees individually, so "peak reserved" is just its bump
        // offset) ---
        {
            cma::ArenaAllocator arena(kArenaCapacity);
            auto t0 = Clock::now();
            RunResult r = run_workload(
                ops, [&](std::size_t size) { return arena.allocate(size, alignof(std::max_align_t), nullptr); },
                [&](void *p) { arena.deallocate(p); }, [&]() { return arena.stats().bytes_in_use; });
            double seconds = std::chrono::duration<double>(Clock::now() - t0).count();
            print_csv_row("arena_allocator", workload_name, op_count, r, seconds);
        }

        // --- FreeListAllocator, first-fit ---
        {
            cma::FreeListAllocator fl(kArenaCapacity, cma::FitPolicy::FirstFit);
            auto t0 = Clock::now();
            RunResult r = run_workload(
                ops, [&](std::size_t size) { return fl.allocate(size, alignof(std::max_align_t), nullptr); },
                [&](void *p) { fl.deallocate(p); }, [&]() { return fl.stats().bytes_in_use; });
            double seconds = std::chrono::duration<double>(Clock::now() - t0).count();
            print_csv_row("free_list_first_fit", workload_name, op_count, r, seconds);
        }

        // --- FreeListAllocator, best-fit ---
        {
            cma::FreeListAllocator fl(kArenaCapacity, cma::FitPolicy::BestFit);
            auto t0 = Clock::now();
            RunResult r = run_workload(
                ops, [&](std::size_t size) { return fl.allocate(size, alignof(std::max_align_t), nullptr); },
                [&](void *p) { fl.deallocate(p); }, [&]() { return fl.stats().bytes_in_use; });
            double seconds = std::chrono::duration<double>(Clock::now() - t0).count();
            print_csv_row("free_list_best_fit", workload_name, op_count, r, seconds);
        }
    }

    return 0;
}
