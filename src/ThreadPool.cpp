
#include "minisdk/ThreadPool.hpp"
#include "minisdk/trace.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace minisdk {

ThreadPool::ThreadPool(std::size_t threadCount) {
    if (threadCount == 0) {
        throw std::invalid_argument("ThreadPool: thread count must be greater than zero");
    }
    threads.reserve(threadCount);
    for (std::size_t i = 0; i < threadCount; ++i) {
        threads.emplace_back(&ThreadPool::run, this, static_cast<unsigned int>(i));
    }
}

ThreadPool::~ThreadPool() {
    shutdown();
}

void ThreadPool::shutdown() {
    {
        std::unique_lock<std::mutex> lk(mutex_);
        if (done) {
            return; // shutdown already performed
        }
        done = true;
    }
    cv.notify_all();
    for (auto& t : threads) {
        if (t.joinable()) {
            t.join();
        }
    }
}

void ThreadPool::enqueue(std::function<void()> job) {
    {
        std::unique_lock<std::mutex> lk(mutex_);
        if (done) {
            throw std::runtime_error("ThreadPool::enqueue: pool has already been shut down");
        }
        q.push(std::move(job));
    }
    cv.notify_one();
}

void ThreadPool::run(unsigned int id_) {

    if constexpr (traceEnabled) std::cout << ("Thread id: " + std::to_string(id_) + " created.\n");

    while (true) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lk(mutex_);
            cv.wait(lk, [this]{ return done || !q.empty(); });
            if (done && q.empty()) break;
            job = std::move(q.front());
            q.pop();
        }
        // A job must not throw into the thread function - that would call std::terminate().
        try {
            job();
        } catch (const std::exception& e) {
            std::cerr << "ThreadPool: job threw: " << e.what() << "\n";
        } catch (...) {
            std::cerr << "ThreadPool: job threw an unknown exception\n";
        }
    }

    if constexpr (traceEnabled) std::cout << ("Thread id: " + std::to_string(id_) + " finished.\n");
}

} // namespace minisdk