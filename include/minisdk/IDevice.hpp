#pragma once
#include "minisdk/buffer.hpp"
#include "minisdk/RegisterMap.hpp"
#include <cstdint>
#include <string>

namespace minisdk {
    
class IDevice {

private:
    unsigned int id;

protected:
    IDevice(const IDevice&) = default;
    IDevice& operator=(const IDevice&) = default;
    IDevice(IDevice&&) = default;
    IDevice& operator=(IDevice&&) = default;

public:
    explicit IDevice(unsigned int id_) : id(id_) {}
    virtual ~IDevice() = default;

    virtual void send(const Buffer &buffer) = 0;
    virtual Buffer receive() = 0;
    virtual uint32_t readRegister(const std::string& name) const = 0;
    virtual void writeRegister(const std::string& name, uint32_t value) = 0;
    virtual RegisterMap<uint32_t> snapshotRegisters() const = 0;

    unsigned int getId() const noexcept {
        return id;
    }
};

}