// Measures the payload allocation path of Buffer: plain new[]/delete[] against BufferPool.
//
// Methodology notes, because an allocator benchmark is easy to get wrong:
//  - every run is preceded by a warmup run whose result is discarded (page faults, CPU scaling)
//  - each configuration runs several times and the MEDIAN is reported, not the mean, so a single
//    scheduling hiccup cannot move the number
//  - each iteration touches the first and last byte and folds them into a checksum that is printed
//    at the end, otherwise the optimiser is free to delete the allocation entirely
//  - both paths zero their payload, so the comparison is allocation against allocation
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "minisdk/BufferPool.hpp"
#include "minisdk/buffer.hpp"

using minisdk::Buffer;
using minisdk::BufferPool;

namespace {

constexpr int kRuns = 11;
constexpr std::size_t kOpsPerRun = 200000;
constexpr std::size_t kBlockSize = 2048;
constexpr std::size_t kBlockCount = 64;

std::uint64_t g_checksum = 0;

// One unit of work: allocate a payload, touch it, release it.
std::uint64_t churn(std::size_t payloadSize, BufferPool* pool, std::size_t ops) {
    std::uint64_t checksum = 0;
    for (std::size_t i = 0; i < ops; ++i) {
        Buffer payload(payloadSize, pool);
        payload[0] = static_cast<unsigned char>(i);
        payload[payloadSize - 1] = static_cast<unsigned char>(i >> 8);
        checksum += payload[0] + payload[payloadSize - 1];
    }
    return checksum;
}

double medianOf(std::vector<double> samples) {
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

double timeSingleThreaded(std::size_t payloadSize, BufferPool* pool) {
    const auto start = std::chrono::steady_clock::now();
    g_checksum += churn(payloadSize, pool, kOpsPerRun);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    const auto ns = std::chrono::duration<double, std::nano>(elapsed).count();
    return ns / static_cast<double>(kOpsPerRun);
}

double timeMultiThreaded(std::size_t payloadSize, BufferPool* pool, unsigned threadCount) {
    const std::size_t opsPerThread = kOpsPerRun / threadCount;

    std::vector<std::thread> threads;
    std::vector<std::uint64_t> partials(threadCount, 0);
    threads.reserve(threadCount);

    const auto start = std::chrono::steady_clock::now();
    for (unsigned t = 0; t < threadCount; ++t) {
        threads.emplace_back([&, t] { partials[t] = churn(payloadSize, pool, opsPerThread); });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;

    for (std::uint64_t partial : partials) {
        g_checksum += partial;
    }
    const auto ns = std::chrono::duration<double, std::nano>(elapsed).count();
    return ns / static_cast<double>(opsPerThread * threadCount);
}

struct Comparison {
    double baselineNs;
    double poolNs;
    double medianRatio;
    double worstRatio;
    double bestRatio;
};

// Measures both variants back to back inside every run and reports the median of the per-run
// ratios. Timing all of A and then all of B lets machine drift between the two halves land
// straight in the result; interleaving cancels it out.
template <typename TimeOne>
Comparison compareVariants(TimeOne&& timeOne) {
    BufferPool pool(kBlockSize, kBlockCount);

    timeOne(nullptr); // warmup, discarded
    timeOne(&pool);

    std::vector<double> baseline;
    std::vector<double> pooled;
    std::vector<double> ratios;
    for (int run = 0; run < kRuns; ++run) {
        const double baselineNs = timeOne(nullptr);
        const double poolNs = timeOne(&pool);
        baseline.push_back(baselineNs);
        pooled.push_back(poolNs);
        ratios.push_back(baselineNs / poolNs);
    }

    std::sort(ratios.begin(), ratios.end());
    return {medianOf(baseline), medianOf(pooled), ratios[ratios.size() / 2], ratios.front(),
            ratios.back()};
}

void report(const std::string& scenario, const Comparison& result) {
    std::cout << std::left << std::setw(30) << scenario << std::right << std::fixed
              << std::setprecision(1) << std::setw(11) << result.baselineNs << std::setw(11)
              << result.poolNs << std::setprecision(2) << std::setw(10) << result.medianRatio
              << "x" << std::setw(14)
              << (std::to_string(result.worstRatio).substr(0, 4) + " - " +
                  std::to_string(result.bestRatio).substr(0, 4))
              << std::endl;
}

} // namespace

int main() {
#ifndef NDEBUG
    std::cout << "WARNING: this is an unoptimised build; the numbers below mean nothing.\n"
              << "Rebuild with -DCMAKE_BUILD_TYPE=Release.\n"
              << std::endl;
#endif

    std::cout << "BufferPool: " << kBlockSize << " B blocks x " << kBlockCount
              << ", hardware threads: " << std::thread::hardware_concurrency() << "\n"
              << kOpsPerRun << " allocate/touch/free cycles per run, " << kRuns
              << " interleaved runs\n"
              << "The spread column is the range of per-run ratios: if it straddles 1.00 the\n"
              << "result is noise, not a difference.\n"
              << std::endl;

    std::cout << std::left << std::setw(30) << "scenario" << std::right << std::setw(11)
              << "new[] ns" << std::setw(11) << "pool ns" << std::setw(11) << "median"
              << std::setw(14) << "spread" << std::endl;
    std::cout << std::string(77, '-') << std::endl;

    report("1500 B payload, 1 thread",
           compareVariants([](BufferPool* pool) { return timeSingleThreaded(1500, pool); }));
    report("64 B payload, 1 thread",
           compareVariants([](BufferPool* pool) { return timeSingleThreaded(64, pool); }));
    // Larger than a block: every request falls through to the global allocator, plus the pool's
    // own bookkeeping. This is where the pool is expected to lose.
    report("9000 B payload (oversized)",
           compareVariants([](BufferPool* pool) { return timeSingleThreaded(9000, pool); }));
    // glibc malloc keeps per-thread arenas; the pool answers with sharded free lists.
    report("1500 B payload, 4 threads",
           compareVariants([](BufferPool* pool) { return timeMultiThreaded(1500, pool, 4); }));
    report("1500 B payload, 8 threads",
           compareVariants([](BufferPool* pool) { return timeMultiThreaded(1500, pool, 8); }));

    std::cout << "\nchecksum: " << g_checksum << " (printed so the work cannot be optimised away)"
              << std::endl;
    return 0;
}
