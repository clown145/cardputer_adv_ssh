#pragma once

#include "driver/gpio.h"

namespace adv::hw {

constexpr gpio_num_t kKeyboardIntPin = GPIO_NUM_11;
constexpr uint8_t kTca8418Address = 0x34;
constexpr uint32_t kI2cFrequency = 400000;

constexpr int kDisplayWidth = 240;
constexpr int kDisplayHeight = 135;

}  // namespace adv::hw
