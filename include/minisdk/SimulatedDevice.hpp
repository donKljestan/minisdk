#include "minisdk/IDevice.hpp"
#include "minisdk/RegisterMap.hpp"
#include "minisdk/buffer.hpp"
#include <cstdint>
#include <string>

namespace minisdk {

    class SimulatedDevice : public IDevice {

        private:
            std::string name;
            RegisterMap<uint32_t> registers_map;

        public:
            SimulatedDevice(const std::string& name_, unsigned int id_);
            void send(const Buffer &buffer) override;
            Buffer receive() override;
            uint32_t readRegister(const std::string& regName) const override;
            void writeRegister(const std::string& regName, uint32_t value) override;
            RegisterMap<uint32_t> snapshotRegisters() const override;


    };
}