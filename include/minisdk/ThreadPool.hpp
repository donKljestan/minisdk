#pragma once
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace minisdk {


    class ThreadPool {

        private:
            std::mutex mutex_;
            std::condition_variable cv;
            std::queue<std::function<void()>> q;
            std::vector<std::thread> threads;
            bool done = false;
            void run(unsigned int id_);

        public:
            explicit ThreadPool(std::size_t threadCount);
            ~ThreadPool();

            void enqueue(std::function<void()> job);
            void shutdown();

            ThreadPool(const ThreadPool&) = delete;
            ThreadPool& operator=(const ThreadPool&) = delete;
    };
}