#include "drivers/tca8418.h"

namespace adv {
namespace {
// Register names and setup flow follow the BSD-licensed Adafruit TCA8418 driver,
// adapted to the M5Unified I2C device wrapper used by M5Stack's Adv firmware.
constexpr uint8_t REG_CFG = 0x01;
constexpr uint8_t REG_INT_STAT = 0x02;
constexpr uint8_t REG_KEY_LCK_EC = 0x03;
constexpr uint8_t REG_KEY_EVENT_A = 0x04;
constexpr uint8_t REG_KP_GPIO_1 = 0x1D;
constexpr uint8_t REG_KP_GPIO_2 = 0x1E;
constexpr uint8_t REG_KP_GPIO_3 = 0x1F;
constexpr uint8_t REG_GPIO_INT_STAT_1 = 0x11;
constexpr uint8_t REG_GPIO_INT_STAT_2 = 0x12;
constexpr uint8_t REG_GPIO_INT_STAT_3 = 0x13;
constexpr uint8_t REG_GPIO_INT_EN_1 = 0x1A;
constexpr uint8_t REG_GPIO_INT_EN_2 = 0x1B;
constexpr uint8_t REG_GPIO_INT_EN_3 = 0x1C;
constexpr uint8_t REG_GPI_EM_1 = 0x20;
constexpr uint8_t REG_GPI_EM_2 = 0x21;
constexpr uint8_t REG_GPI_EM_3 = 0x22;
constexpr uint8_t REG_GPIO_DIR_1 = 0x23;
constexpr uint8_t REG_GPIO_DIR_2 = 0x24;
constexpr uint8_t REG_GPIO_DIR_3 = 0x25;
constexpr uint8_t REG_GPIO_INT_LVL_1 = 0x26;
constexpr uint8_t REG_GPIO_INT_LVL_2 = 0x27;
constexpr uint8_t REG_GPIO_INT_LVL_3 = 0x28;
constexpr uint8_t CFG_KE_IEN = 0x01;
constexpr uint8_t CFG_GPI_IEN = 0x02;
}  // namespace

Tca8418::Tca8418(uint8_t address, uint32_t frequency, m5::I2C_Class* i2c) : I2C_Device(address, frequency, i2c) {}

bool Tca8418::begin()
{
    bool ok = true;
    ok &= writeRegister8(REG_GPIO_DIR_1, 0x00);
    ok &= writeRegister8(REG_GPIO_DIR_2, 0x00);
    ok &= writeRegister8(REG_GPIO_DIR_3, 0x00);
    ok &= writeRegister8(REG_GPI_EM_1, 0xFF);
    ok &= writeRegister8(REG_GPI_EM_2, 0xFF);
    ok &= writeRegister8(REG_GPI_EM_3, 0xFF);
    ok &= writeRegister8(REG_GPIO_INT_LVL_1, 0x00);
    ok &= writeRegister8(REG_GPIO_INT_LVL_2, 0x00);
    ok &= writeRegister8(REG_GPIO_INT_LVL_3, 0x00);
    ok &= writeRegister8(REG_GPIO_INT_EN_1, 0xFF);
    ok &= writeRegister8(REG_GPIO_INT_EN_2, 0xFF);
    ok &= writeRegister8(REG_GPIO_INT_EN_3, 0xFF);
    return ok;
}

bool Tca8418::matrix(uint8_t rows, uint8_t columns)
{
    if (rows > 8 || columns > 10) {
        return false;
    }

    uint8_t row_mask = 0;
    for (uint8_t r = 0; r < rows; ++r) {
        row_mask = static_cast<uint8_t>((row_mask << 1) | 1);
    }
    writeRegister8(REG_KP_GPIO_1, row_mask);

    uint8_t col_mask = 0;
    for (uint8_t c = 0; c < columns && c < 8; ++c) {
        col_mask = static_cast<uint8_t>((col_mask << 1) | 1);
    }
    writeRegister8(REG_KP_GPIO_2, col_mask);

    if (columns > 8) {
        writeRegister8(REG_KP_GPIO_3, columns == 9 ? 0x01 : 0x03);
    }
    return true;
}

uint8_t Tca8418::available()
{
    return readRegister8(REG_KEY_LCK_EC) & 0x0F;
}

uint8_t Tca8418::get_event()
{
    return readRegister8(REG_KEY_EVENT_A);
}

void Tca8418::flush()
{
    while (get_event() != 0) {
    }
    readRegister8(REG_GPIO_INT_STAT_1);
    readRegister8(REG_GPIO_INT_STAT_2);
    readRegister8(REG_GPIO_INT_STAT_3);
    writeRegister8(REG_INT_STAT, 3);
}

void Tca8418::enable_interrupts()
{
    uint8_t value = readRegister8(REG_CFG);
    value |= (CFG_GPI_IEN | CFG_KE_IEN);
    writeRegister8(REG_CFG, value);
}

}  // namespace adv
