#include "drivers/keyboard.h"

#include "drivers/hardware_config.h"
#include "esp_log.h"

namespace adv {
namespace {
constexpr const char* TAG = "keyboard";

struct KeyMapEntry {
    char normal;
    char shifted;
    KeyKind normal_kind;
    KeyKind fn_kind;
    char fn_char;
};

constexpr KeyMapEntry K(char normal, char shifted = '\0')
{
    return {normal, shifted == '\0' ? normal : shifted, KeyKind::kCharacter, KeyKind::kNone, '\0'};
}

constexpr KeyMapEntry S(KeyKind kind)
{
    return {'\0', '\0', kind, KeyKind::kNone, '\0'};
}

constexpr KeyMapEntry F(char normal, char shifted, KeyKind fn_kind)
{
    return {normal, shifted, KeyKind::kCharacter, fn_kind, '\0'};
}

constexpr KeyMapEntry C(char normal, char shifted, char fn_char)
{
    return {normal, shifted, KeyKind::kCharacter, KeyKind::kCharacter, fn_char};
}

char control_char_for(char ch)
{
    if (ch >= 'a' && ch <= 'z') {
        return ch - 'a' + 1;
    }
    if (ch >= 'A' && ch <= 'Z') {
        return ch - 'A' + 1;
    }
    switch (ch) {
        case '@':
        case ' ':
            return '\0';
        case '[':
            return '\x1B';
        case '\\':
            return '\x1C';
        case ']':
            return '\x1D';
        case '^':
            return '\x1E';
        case '_':
            return '\x1F';
        default:
            return '\0';
    }
}

constexpr KeyMapEntry kKeyMap[4][14] = {
    {S(KeyKind::kEscape), K('1', '!'), K('2', '@'), K('3', '#'), K('4', '$'), K('5', '%'), K('6', '^'),
     K('7', '&'), K('8', '*'), K('9', '('), K('0', ')'), K('-', '_'), K('=', '+'), S(KeyKind::kBackspace)},
    {S(KeyKind::kTab), K('q', 'Q'), K('w', 'W'), K('e', 'E'), K('r', 'R'), K('t', 'T'), K('y', 'Y'),
     K('u', 'U'), K('i', 'I'), K('o', 'O'), K('p', 'P'), K('[', '{'), K(']', '}'), K('\\', '|')},
    {S(KeyKind::kNone), S(KeyKind::kNone), K('a', 'A'), K('s', 'S'), K('d', 'D'), K('f', 'F'), K('g', 'G'),
     K('h', 'H'), K('j', 'J'), K('k', 'K'), K('l', 'L'), F(';', ':', KeyKind::kUp), K('\'', '"'),
     S(KeyKind::kEnter)},
    {S(KeyKind::kNone), S(KeyKind::kNone), S(KeyKind::kNone), K('z', 'Z'), K('x', 'X'), K('c', 'C'),
     K('v', 'V'), K('b', 'B'), K('n', 'N'), K('m', 'M'), F(',', '<', KeyKind::kLeft),
     F('.', '>', KeyKind::kDown), F('/', '?', KeyKind::kRight), K(' ')},
};
}  // namespace

Keyboard::Keyboard() : tca_(hw::kTca8418Address, hw::kI2cFrequency, &m5::In_I2C) {}

bool Keyboard::begin()
{
    if (!tca_.begin()) {
        ESP_LOGE(TAG, "TCA8418 begin failed");
        return false;
    }
    if (!tca_.matrix(7, 8)) {
        ESP_LOGE(TAG, "TCA8418 matrix setup failed");
        return false;
    }
    tca_.flush();
    tca_.enable_interrupts();
    return true;
}

std::optional<KeyEvent> Keyboard::poll()
{
    M5.update();
    if (tca_.available() == 0) {
        return std::nullopt;
    }
    RawKey raw = decode(tca_.get_event());
    remap(raw);
    KeyEvent event = convert(raw);
    if (event.kind == KeyKind::kNone && event.character == '\0') {
        return std::nullopt;
    }
    return event;
}

Keyboard::RawKey Keyboard::decode(uint8_t raw)
{
    RawKey key;
    key.pressed = (raw & 0x80) != 0;
    uint8_t index = (raw & 0x7F);
    if (index > 0) {
        index -= 1;
    }
    key.row = index / 10;
    key.col = index % 10;
    return key;
}

void Keyboard::remap(RawKey& key)
{
    uint8_t col = key.row * 2;
    if (key.col > 3) {
        col++;
    }
    uint8_t row = (key.col + 4) % 4;
    key.row = row;
    key.col = col;
}

KeyEvent Keyboard::convert(const RawKey& key)
{
    if (key.row >= 4 || key.col >= 14) {
        return {};
    }

    if (key.row == 2 && key.col == 0) {
        fn_ = key.pressed;
        if (key.pressed) {
            fn_latched_ = true;
        }
        return {};
    }
    if (key.row == 2 && key.col == 1) {
        shift_ = key.pressed;
        return {};
    }
    if (key.row == 3 && key.col <= 2) {
        ctrl_ = key.pressed;
        if (key.pressed) {
            ctrl_latched_ = true;
        }
        return {};
    }

    if (!key.pressed) {
        return {};
    }

    const auto& entry = kKeyMap[key.row][key.col];
    bool use_fn = fn_ || fn_latched_;
    bool use_ctrl = ctrl_ || ctrl_latched_;
    if (use_fn && entry.fn_kind != KeyKind::kNone) {
        fn_latched_ = false;
        return {.kind = entry.fn_kind, .character = entry.fn_char, .pressed = true};
    }
    if (entry.normal_kind != KeyKind::kCharacter) {
        fn_latched_ = false;
        ctrl_latched_ = false;
        return {.kind = entry.normal_kind, .character = '\0', .pressed = true};
    }
    if (use_fn || use_ctrl) {
        char control = control_char_for(shift_ ? entry.shifted : entry.normal);
        if (control != '\0') {
            fn_latched_ = false;
            ctrl_latched_ = false;
            return {.kind = KeyKind::kCharacter, .character = control, .pressed = true};
        }
    }
    fn_latched_ = false;
    ctrl_latched_ = false;
    return {.kind = KeyKind::kCharacter, .character = shift_ ? entry.shifted : entry.normal, .pressed = true};
}

}  // namespace adv
