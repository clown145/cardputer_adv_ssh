#pragma once

#include <cstdint>

#include "M5Unified.hpp"

namespace adv {

class Tca8418 : public m5::I2C_Device {
public:
    Tca8418(uint8_t address, uint32_t frequency, m5::I2C_Class* i2c);

    bool begin();
    bool matrix(uint8_t rows, uint8_t columns);
    uint8_t available();
    uint8_t get_event();
    void flush();
    void enable_interrupts();
};

}  // namespace adv
