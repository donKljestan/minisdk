#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "minisdk/DeviceManager.hpp"
#include "minisdk/SimulatedDevice.hpp"
#include "minisdk/buffer.hpp"

using minisdk::Buffer;
using minisdk::DeviceManager;
using minisdk::SimulatedDevice;

namespace {

// DeviceManager holds a shared_mutex and an EventBus, so it is neither copyable nor movable
// and cannot be returned by value.
void addPorts(DeviceManager& manager, unsigned int count) {
    for (unsigned int i = 0; i < count; ++i) {
        manager.addDevice(std::make_unique<SimulatedDevice>("port", i));
    }
}

} // namespace

TEST_CASE("traffic reaches only the addressed device", "[manager]") {
    DeviceManager manager;
    manager.addDevice(std::make_unique<SimulatedDevice>("port", 0));
    manager.addDevice(std::make_unique<SimulatedDevice>("port", 1));

    Buffer payload(32);
    manager.sendBuffer(1, payload);

    REQUIRE(manager.getRegister(0, "TX_PACKET_COUNT") == 0);
    REQUIRE(manager.getRegister(1, "TX_PACKET_COUNT") == 1);
}

TEST_CASE("receive is routed and reported on the right device", "[manager]") {
    DeviceManager manager;
    manager.addDevice(std::make_unique<SimulatedDevice>("port", 0));

    Buffer received = manager.receiveBuffer(0);

    REQUIRE(received.getSize() == 50);
    REQUIRE(manager.getRegister(0, "RX_PACKET_COUNT") == 1);
}

TEST_CASE("an unknown device id is rejected instead of silently ignored", "[manager]") {
    DeviceManager manager;
    manager.addDevice(std::make_unique<SimulatedDevice>("port", 0));
    Buffer payload(8);

    REQUIRE_THROWS_AS(manager.sendBuffer(99, payload), std::out_of_range);
    REQUIRE_THROWS_AS(manager.receiveBuffer(99), std::out_of_range);
    REQUIRE_THROWS_AS(manager.getRegister(99, "TX_PACKET_COUNT"), std::out_of_range);
}

TEST_CASE("registering the same device id twice is rejected", "[manager]") {
    DeviceManager manager;
    manager.addDevice(std::make_unique<SimulatedDevice>("port", 0));

    REQUIRE_THROWS_AS(manager.addDevice(std::make_unique<SimulatedDevice>("other", 0)),
                      std::invalid_argument);
}

TEST_CASE("a removed device is no longer reachable", "[manager]") {
    DeviceManager manager;
    addPorts(manager, 2);

    manager.removeDevice(0);

    Buffer payload(8);
    REQUIRE_THROWS_AS(manager.sendBuffer(0, payload), std::out_of_range);
    REQUIRE_NOTHROW(manager.sendBuffer(1, payload));
    REQUIRE_NOTHROW(manager.removeDevice(0)); // removing twice is a no-op
}

TEST_CASE("the manager publishes traffic events", "[manager]") {
    DeviceManager manager;
    manager.addDevice(std::make_unique<SimulatedDevice>("port", 0));

    int txEvents = 0;
    manager.events().addEvent("TX_PACKET_COUNT");
    manager.events().addSubscriber("TX_PACKET_COUNT", [&](const std::string&) { ++txEvents; });

    Buffer payload(8);
    manager.sendBuffer(0, payload);
    manager.sendBuffer(0, payload);

    REQUIRE(txEvents == 2);
}

// The TX counter is a non-atomic read-modify-write inside the device; it only reaches the
// expected total because sendBuffer serialises access per device.
TEST_CASE("concurrent senders do not lose counter increments", "[manager][concurrency]") {
    constexpr uint32_t threadCount = 8;
    constexpr uint32_t sendsPerThread = 1000;

    DeviceManager manager;
    manager.addDevice(std::make_unique<SimulatedDevice>("port", 0));

    std::vector<std::thread> threads;
    threads.reserve(threadCount);
    for (uint32_t i = 0; i < threadCount; ++i) {
        threads.emplace_back([&manager] {
            Buffer payload(64);
            for (uint32_t j = 0; j < sendsPerThread; ++j) {
                manager.sendBuffer(0, payload);
            }
        });
    }
    for (auto& t : threads) {
        t.join();
    }

    REQUIRE(manager.getRegister(0, "TX_PACKET_COUNT") == threadCount * sendsPerThread);
}
