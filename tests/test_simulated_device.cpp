#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <stdexcept>

#include "minisdk/SimulatedDevice.hpp"
#include "minisdk/buffer.hpp"

using minisdk::Buffer;
using minisdk::RegisterMap;
using minisdk::SimulatedDevice;

TEST_CASE("a fresh device exposes the standard registers", "[device]") {
    SimulatedDevice device("port", 3);

    REQUIRE(device.getId() == 3);
    REQUIRE(device.readRegister("TX_PACKET_COUNT") == 0);
    REQUIRE(device.readRegister("RX_PACKET_COUNT") == 0);
    REQUIRE(device.readRegister("MTU") == 1500);
}

TEST_CASE("send and receive move the traffic counters", "[device]") {
    SimulatedDevice device("port", 0);
    Buffer payload(64);

    device.send(payload);
    device.send(payload);
    Buffer received = device.receive();

    REQUIRE(device.readRegister("TX_PACKET_COUNT") == 2);
    REQUIRE(device.readRegister("RX_PACKET_COUNT") == 1);
    REQUIRE(received.getSize() == 50);
}

TEST_CASE("writing an unknown register is an error, not a silent insert", "[device]") {
    SimulatedDevice device("port", 0);

    REQUIRE_THROWS_AS(device.writeRegister("TYPO_REGISTER", 1), std::out_of_range);
    REQUIRE_THROWS_AS(device.readRegister("TYPO_REGISTER"), std::out_of_range);
}

TEST_CASE("snapshot is an independent copy of the register state", "[device]") {
    SimulatedDevice device("port", 0);
    RegisterMap<uint32_t> before = device.snapshotRegisters();

    device.writeRegister("LINK_STATUS", 1);
    RegisterMap<uint32_t> after = device.snapshotRegisters();

    REQUIRE(before.get("LINK_STATUS") == 0); // the earlier snapshot must not have followed along
    REQUIRE(after.get("LINK_STATUS") == 1);
    REQUIRE_FALSE(before == after);
    REQUIRE(before.diff(after).size() == 1);
}
