
#include "minisdk/DeviceManager.hpp"
#include <iostream>
#include <shared_mutex>
#include <stdexcept>
#include <string>

namespace minisdk {

std::shared_ptr<DeviceManager::DeviceEntry> DeviceManager::findEntry(unsigned int id) const {
    std::shared_lock<std::shared_mutex> lock(devicesMutex);
    auto it = devices.find(id);
    if (it == devices.end()) {
        throw std::out_of_range("DeviceManager: no device with id " + std::to_string(id));
    }
    return it->second;
}

void DeviceManager::addDevice(std::unique_ptr<IDevice> device) {
    const unsigned int id = device->getId();
    auto entry = std::make_shared<DeviceEntry>();
    entry->device = std::move(device);
    {
        std::unique_lock<std::shared_mutex> lock(devicesMutex);
        if (!devices.insert({id, std::move(entry)}).second) {
            throw std::invalid_argument("DeviceManager::addDevice: duplicate device id " + std::to_string(id));
        }
    }
    std::cout << "Added device: " << id << std::endl;
}

void DeviceManager::removeDevice(unsigned int id) {
    bool removed = false;
    {
        std::unique_lock<std::shared_mutex> lock(devicesMutex);
        removed = devices.erase(id) > 0;
    }
    if (removed) {
        std::cout << "Removed device: " << id << std::endl;
    }
}

void DeviceManager::sendBuffer(unsigned int id, const Buffer& buffer) {
    auto entry = findEntry(id);
    {
        ScopedDeviceLock sdl(entry->lock);
        entry->device->send(buffer);
    }
    // Fired outside the device lock so a subscriber may call back into the manager.
    eventBus.eventHappen("TX_PACKET_COUNT", "device " + std::to_string(id));
}

Buffer DeviceManager::receiveBuffer(unsigned int id) {
    auto entry = findEntry(id);
    Buffer buffer;
    {
        ScopedDeviceLock sdl(entry->lock);
        buffer = entry->device->receive();
    }
    eventBus.eventHappen("RX_PACKET_COUNT", "device " + std::to_string(id));
    return buffer;
}

uint32_t DeviceManager::getRegister(unsigned int id, const std::string& regName) const {
    auto entry = findEntry(id);
    ScopedDeviceLock sdl(entry->lock);
    return entry->device->readRegister(regName);
}

} // namespace minisdk
