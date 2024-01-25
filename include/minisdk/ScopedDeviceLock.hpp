
#pragma once
#include <mutex>

namespace minisdk {
    // Hand-written equivalent of std::lock_guard, kept so the RAII locking is explicit in the code.
    class ScopedDeviceLock {

        std::mutex& mutex;
        public:
            explicit ScopedDeviceLock(std::mutex & mutex_) : mutex(mutex_) {
                mutex.lock();
            }

            ~ScopedDeviceLock() {
                mutex.unlock();
            }
            ScopedDeviceLock(const ScopedDeviceLock&) = delete;
            ScopedDeviceLock& operator=(const ScopedDeviceLock&) = delete;

    };
}