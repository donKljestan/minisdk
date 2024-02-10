#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>

#include "minisdk/ThreadPool.hpp"

using minisdk::ThreadPool;

TEST_CASE("a pool without workers is rejected", "[threadpool]") {
    REQUIRE_THROWS_AS(ThreadPool(0), std::invalid_argument);
}

TEST_CASE("shutdown does not return until the queue is drained", "[threadpool]") {
    constexpr int jobCount = 2000;
    std::atomic<int> executed{0};

    {
        ThreadPool pool(4);
        for (int i = 0; i < jobCount; ++i) {
            pool.enqueue([&executed] { ++executed; });
        }
        pool.shutdown();

        REQUIRE(executed.load() == jobCount);
    }
}

TEST_CASE("the destructor shuts the pool down", "[threadpool]") {
    std::atomic<int> executed{0};

    {
        ThreadPool pool(2);
        for (int i = 0; i < 100; ++i) {
            pool.enqueue([&executed] { ++executed; });
        }
    } // destructor joins every worker

    REQUIRE(executed.load() == 100);
}

TEST_CASE("shutdown is idempotent", "[threadpool]") {
    ThreadPool pool(2);
    pool.enqueue([] {});

    pool.shutdown();
    REQUIRE_NOTHROW(pool.shutdown());
}

TEST_CASE("enqueueing after shutdown is rejected rather than silently dropped", "[threadpool]") {
    ThreadPool pool(2);
    pool.shutdown();

    REQUIRE_THROWS_AS(pool.enqueue([] {}), std::runtime_error);
}

// A job escaping with an exception would reach the thread function and call std::terminate.
TEST_CASE("a throwing job does not take the pool down", "[threadpool]") {
    std::atomic<int> executed{0};

    ThreadPool pool(2);
    pool.enqueue([] { throw std::runtime_error("job failure"); });
    pool.enqueue([] { throw 42; });
    for (int i = 0; i < 50; ++i) {
        pool.enqueue([&executed] { ++executed; });
    }
    pool.shutdown();

    REQUIRE(executed.load() == 50);
}

TEST_CASE("work is spread across the workers", "[threadpool][concurrency]") {
    constexpr int workerCount = 4;
    std::atomic<int> concurrentPeak{0};
    std::atomic<int> inFlight{0};
    std::atomic<int> executed{0};

    {
        ThreadPool pool(workerCount);
        for (int i = 0; i < 200; ++i) {
            pool.enqueue([&] {
                const int current = ++inFlight;
                int previousPeak = concurrentPeak.load();
                while (current > previousPeak &&
                       !concurrentPeak.compare_exchange_weak(previousPeak, current)) {
                    // retry until the observed peak is recorded
                }
                std::this_thread::sleep_for(std::chrono::microseconds(50));
                ++executed;
                --inFlight;
            });
        }
        pool.shutdown();
    }

    REQUIRE(executed.load() == 200);
    REQUIRE(concurrentPeak.load() > 1); // more than one worker actually ran jobs
}
