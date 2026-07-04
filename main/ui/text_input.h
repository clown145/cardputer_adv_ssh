#pragma once

#include <optional>
#include <string>

#include "drivers/display.h"
#include "drivers/keyboard.h"

namespace adv {

class TextInput {
public:
    TextInput(Display& display, Keyboard& keyboard);
    std::optional<std::string> prompt(const std::string& title, const std::string& label,
                                      const std::string& initial = "", bool secret = false);

private:
    Display& display_;
    Keyboard& keyboard_;
};

}  // namespace adv
