#include "ui/text_input.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace adv {

TextInput::TextInput(Display& display, Keyboard& keyboard) : display_(display), keyboard_(keyboard) {}

std::optional<std::string> TextInput::prompt(const std::string& title, const std::string& label,
                                             const std::string& initial, bool secret)
{
    std::string value = initial;
    display_.show_text_input(title, label, value, secret);

    while (true) {
        auto event = keyboard_.poll();
        if (!event.has_value()) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        switch (event->kind) {
            case KeyKind::kCharacter:
                if (event->character == '\x1b') {
                    return std::nullopt;
                }
                if (value.size() < 96 && event->character >= 32 && event->character <= 126) {
                    value.push_back(event->character);
                }
                break;
            case KeyKind::kBackspace:
            case KeyKind::kDelete:
                if (!value.empty()) {
                    value.pop_back();
                }
                break;
            case KeyKind::kEnter:
                return value;
            case KeyKind::kEscape:
                return std::nullopt;
            default:
                break;
        }
        display_.show_text_input(title, label, value, secret);
    }
}

}  // namespace adv
