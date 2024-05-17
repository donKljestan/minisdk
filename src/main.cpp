

#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include "minisdk/buffer.hpp"
#include "minisdk/IDevice.hpp"
#include "minisdk/SimulatedDevice.hpp"
#include "minisdk/DeviceManager.hpp"
#include <thread>
#include <functional>
#include "minisdk/ThreadPool.hpp"

using namespace minisdk;

// Not assert(): NDEBUG would compile the checks away and the demo would still claim success.
static void check(bool condition, const char* what) {
    if (!condition) {
        std::cerr << "FAILED: " << what << std::endl;
        std::abort();
    }
}

static void section(const std::string& title) {
    std::cout << "\n=== " << title << " ===" << std::endl;
}

// Registers survive read/write, counters track traffic, and snapshot/diff reports exactly what changed.
void demoDeviceRegisters() {
    section("Device registers: read/write, TX/RX counters, snapshot diff");

    Buffer buffer1(1000);
    Buffer buffer2(1000);
    Buffer buffer3(500);
    std::unique_ptr<IDevice> port0 = std::make_unique<SimulatedDevice>("port", 0);
    port0->writeRegister("LINK_STATUS", 1);
    port0->writeRegister("MTU", 9000);

    RegisterMap<uint32_t> before = port0->snapshotRegisters();

    port0->send(buffer1);
    port0->send(buffer2);
    port0->send(buffer3);
    Buffer buf1 = port0->receive();
    Buffer buf2 = port0->receive();

    RegisterMap<uint32_t> after = port0->snapshotRegisters();

    std::cout << "LINK_STATUS: " << port0->readRegister("LINK_STATUS") << std::endl;
    std::cout << "MTU: " << port0->readRegister("MTU") << std::endl;
    std::cout << "TX_PACKET_COUNT: " << port0->readRegister("TX_PACKET_COUNT") << std::endl;
    std::cout << "RX_PACKET_COUNT: " << port0->readRegister("RX_PACKET_COUNT") << std::endl;

    std::vector<std::string> changed = before.diff(after);
    std::cout << "Changed registers: ";
    for (const auto& regName : changed) std::cout << regName << " ";
    std::cout << std::endl;

    check(port0->readRegister("TX_PACKET_COUNT") == 3, "three sends must be counted");
    check(port0->readRegister("RX_PACKET_COUNT") == 2, "two receives must be counted");
    check(changed.size() == 2, "only the TX/RX counters moved; LINK_STATUS and MTU stayed put");
    check(!(before == after), "snapshots taken around traffic must differ");

    std::cout << "OK: snapshot diff reported exactly the two counters that changed" << std::endl;
}

// Traffic reaches the addressed device only, and an unknown id is rejected instead of silently ignored.
void demoDeviceManagerRouting() {
    section("DeviceManager: routing by device id");

    DeviceManager manager;
    manager.addDevice(std::make_unique<SimulatedDevice>("port", 0));
    manager.addDevice(std::make_unique<SimulatedDevice>("port", 1));
    manager.addDevice(std::make_unique<SimulatedDevice>("port", 2));

    Buffer b1(65);
    Buffer b2(25);
    manager.sendBuffer(1, b1);
    manager.sendBuffer(2, b2);
    Buffer b3 = manager.receiveBuffer(0);

    check(manager.getRegister(0, "TX_PACKET_COUNT") == 0, "nothing was sent to port 0");
    check(manager.getRegister(1, "TX_PACKET_COUNT") == 1, "port 1 received exactly one buffer");
    check(manager.getRegister(2, "TX_PACKET_COUNT") == 1, "port 2 received exactly one buffer");
    check(manager.getRegister(0, "RX_PACKET_COUNT") == 1, "port 0 served one receive");
    check(b3.getSize() == 50, "receive returns the simulated payload size");

    bool unknownIdRejected = false;
    try {
        manager.sendBuffer(99, b1);
    } catch (const std::out_of_range&) {
        unknownIdRejected = true;
    }
    check(unknownIdRejected, "an unknown device id must be rejected");

    std::cout << "OK: each buffer reached only its target device, unknown id threw" << std::endl;
}

// Every subscriber of an event is invoked once per publish; unrelated events stay silent.
void demoEventBusPubSub() {
    section("EventBus: publish/subscribe fan-out");

    EventBus eventBus;
    eventBus.addEvent("LINK_STATUS");
    eventBus.addEvent("MTU");
    eventBus.addEvent("TX_PACKET_COUNT");
    eventBus.addEvent("RX_PACKET_COUNT");

    int txNotifications = 0;
    int linkNotifications = 0;

    eventBus.addSubscriber("TX_PACKET_COUNT",
        [&txNotifications](const std::string& message) {
            ++txNotifications;
            std::cout << "Received1: " << message << std::endl;
    });
    eventBus.addSubscriber("TX_PACKET_COUNT",
        [&txNotifications](const std::string& message) {
            ++txNotifications;
            std::cout << "Received2: " << message << std::endl;
    });
    eventBus.addSubscriber("LINK_STATUS",
        [&linkNotifications](const std::string& message) {
            ++linkNotifications;
            std::cout << "Link status: " << message << std::endl;
    });

    eventBus.eventHappen("TX_PACKET_COUNT", "5 bytes");
    eventBus.eventHappen("LINK_STATUS", " connected.");
    eventBus.eventHappen("MTU", "9000"); // registered but has no subscribers

    check(txNotifications == 2, "both subscribers of the event fired");
    check(linkNotifications == 1, "the unrelated event reached only its own subscriber");

    std::cout << "OK: publish reached every subscriber of the event and no one else" << std::endl;
}

// std::atomic<int> counter{0};
// void increment() {
//     counter++;
// }

static void sendBurst(DeviceManager& manager) {
    Buffer b1(65);
    for (int i = 0; i < 1000; i++) {
        manager.sendBuffer(0, b1);
    }
}

// The TX counter is a non-atomic read-modify-write; it only survives contention because of ScopedDeviceLock.
void demoLockedCounterUnderContention() {
    section("Concurrency: 8 raw threads hammering one device");

    DeviceManager deviceManager;
    deviceManager.addDevice(std::make_unique<SimulatedDevice>("port", 0));

    std::vector<std::thread> threads;
    for (int i = 0; i < 8; i++) {
        threads.emplace_back(sendBurst, std::ref(deviceManager));
        // threads.emplace_back(increment);
    }
    for (auto& t : threads) {
        t.join();
    }

    // Read outside the check so the call is never optimised out of the demo.
    const uint32_t txCount = deviceManager.getRegister(0, "TX_PACKET_COUNT");
    std::cout << "TX_PACKET_COUNT: " << txCount << std::endl;
    // std::cout << "Counter: " << counter << std::endl;

    check(txCount == 8000, "8 threads x 1000 sends; without the device lock increments get lost");

    std::cout << "OK: 8000 increments survived, no lost updates" << std::endl;
}

// shutdown() must not return until every queued job has actually run.
void demoThreadPoolDispatch() {
    section("ThreadPool: 8000 jobs across 5 workers, drained on shutdown");

    DeviceManager deviceManager;
    deviceManager.addDevice(std::make_unique<SimulatedDevice>("port", 0));

    ThreadPool threadPool(5);
    for (int i = 0; i < 8000; i++) {
        threadPool.enqueue([&deviceManager] {
            Buffer b1(65);
            deviceManager.sendBuffer(0, b1);
        });
    }
    threadPool.shutdown();

    const uint32_t txCount = deviceManager.getRegister(0, "TX_PACKET_COUNT");
    std::cout << "TX_PACKET_COUNT: " << txCount << std::endl;

    check(txCount == 8000, "every job must have run before shutdown() returned");

    std::cout << "OK: queue fully drained, no job dropped" << std::endl;
}

int main() {
    demoDeviceRegisters();
    demoDeviceManagerRouting();
    demoEventBusPubSub();
    demoLockedCounterUnderContention();
    demoThreadPoolDispatch();

    std::cout << "\nAll demos passed." << std::endl;
    return 0;
}

