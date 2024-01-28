#pragma once
#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include "minisdk/IDevice.hpp"
#include "minisdk/EventBus.hpp"
#include "minisdk/ScopedDeviceLock.hpp"

namespace minisdk {

    class DeviceManager {

        private:
            // Device and its lock are stored together so the two can never get out of sync.
            struct DeviceEntry {
                std::unique_ptr<IDevice> device;
                std::mutex lock;
            };

            mutable std::shared_mutex devicesMutex;
            // shared_ptr keeps an entry alive while a thread is using it, even if it gets removed meanwhile.
            std::unordered_map<unsigned int, std::shared_ptr<DeviceEntry>> devices;
            EventBus eventBus;

            std::shared_ptr<DeviceEntry> findEntry(unsigned int id) const;

        public:
            void addDevice(std::unique_ptr<IDevice> device);
            void removeDevice(unsigned int id);
            void sendBuffer(unsigned int id, const Buffer& buffer);
            Buffer receiveBuffer(unsigned int id);
            uint32_t getRegister(unsigned int id, const std::string& regName) const;

            EventBus& events() noexcept { return eventBus; }
    };

}