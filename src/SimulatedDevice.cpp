
#include "minisdk/SimulatedDevice.hpp"
#include <cstdint>
#include <iostream>

namespace minisdk {

SimulatedDevice::SimulatedDevice(const std::string &name_, unsigned int id_): IDevice(id_), name(name_) {
    registers_map.add("TX_PACKET_COUNT", 0);
    registers_map.add("RX_PACKET_COUNT", 0);
    registers_map.add("LINK_STATUS", 0);
    registers_map.add("MTU", 1500);
}

void SimulatedDevice::send(const Buffer &buffer) {
    std::cout << "Buffer size: " << buffer.getSize() << " send to: {" << name << getId() << "}" << std::endl;
    writeRegister("TX_PACKET_COUNT", readRegister("TX_PACKET_COUNT") + 1);
}

Buffer SimulatedDevice::receive() {
    Buffer buffer(50);
    std::cout << "Buffer size: " << buffer.getSize() << " received on: {" << name << getId() << "}" << std::endl;
    writeRegister("RX_PACKET_COUNT", readRegister("RX_PACKET_COUNT") + 1);
    return buffer;
}

uint32_t SimulatedDevice::readRegister(const std::string& regName) const {
    return registers_map.get(regName);
}

void SimulatedDevice::writeRegister(const std::string& regName, uint32_t value) {
    registers_map.update(regName, value);
}

RegisterMap<uint32_t> SimulatedDevice::snapshotRegisters() const {
    return registers_map;
}

} // namespace minisdk