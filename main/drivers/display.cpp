#include "drivers/display.h"

#include <algorithm>

#include "M5Unified.hpp"
#include "lgfx/Fonts/efont/lgfx_efont_cn.h"
#include "lgfx/utility/pgmspace.h"
#include "terminal/terminal_emulator.h"

namespace adv {
namespace {
constexpr uint32_t kBg = 0x101820;
constexpr uint32_t kFg = 0xE8F0F2;
constexpr uint32_t kDim = 0x8EA0A8;
constexpr uint32_t kAccent = 0x18C7A6;
constexpr uint32_t kWarn = 0xFFB000;
constexpr int kTerminalTop = 20;
constexpr int kCellWidth = 6;
constexpr int kCellHeight = 12;

bool g_wifi_connected = false;
bool g_ssh_connected = false;

void use_ui_font()
{
    M5.Display.setFont(&fonts::Font0);
    M5.Display.setTextSize(1);
    M5.Display.setTextWrap(false, false);
}

void use_terminal_font()
{
    M5.Display.setFont(&fonts::Font0);
    M5.Display.setTextSize(1);
    M5.Display.setTextWrap(false, false);
}

uint8_t font_byte(const uint8_t* ptr)
{
    return pgm_read_byte(ptr);
}

uint16_t font_u16(const uint8_t* ptr)
{
    return (static_cast<uint16_t>(font_byte(ptr)) << 8) | font_byte(ptr + 1);
}

int8_t font_i8(size_t index)
{
    return static_cast<int8_t>(font_byte(&lgfx_efont_cn_12[index]));
}

uint8_t font_header(size_t index)
{
    return font_byte(&lgfx_efont_cn_12[index]);
}

const uint8_t* find_efont_cn_glyph(uint16_t encoding)
{
    const uint8_t* font = &lgfx_efont_cn_12[23];
    uint16_t start_upper = font_u16(&lgfx_efont_cn_12[17]);
    uint16_t start_lower = font_u16(&lgfx_efont_cn_12[19]);
    uint16_t start_unicode = font_u16(&lgfx_efont_cn_12[21]);

    if (encoding <= 255) {
        if (encoding >= 'a') {
            font += start_lower;
        } else if (encoding >= 'A') {
            font += start_upper;
        }
        for (; font_byte(font + 1) != 0; font += font_byte(font + 1)) {
            if (font_byte(font) == encoding) {
                return font + 2;
            }
        }
        return nullptr;
    }

    font += start_unicode;
    const uint8_t* unicode_lut = font;
    uint16_t current = 0;
    do {
        font += font_u16(unicode_lut);
        current = font_u16(unicode_lut + 2);
        unicode_lut += 4;
    } while (current < encoding);

    for (; (current = font_u16(font)) != 0; font += font_byte(font + 2)) {
        if (current == encoding) {
            return font + 3;
        }
    }
    return nullptr;
}

class FontDecoder {
public:
    explicit FontDecoder(const uint8_t* ptr) : ptr_(ptr) {}

    uint8_t unsigned_bits(uint8_t count)
    {
        uint8_t value = font_byte(ptr_) >> bit_pos_;
        uint8_t next_bit_pos = bit_pos_ + count;
        if (next_bit_pos >= 8) {
            next_bit_pos -= 8;
            value |= font_byte(++ptr_) << (8 - bit_pos_);
        }
        bit_pos_ = next_bit_pos;
        return value & ((1U << count) - 1);
    }

    int8_t signed_bits(uint8_t count)
    {
        return static_cast<int8_t>(unsigned_bits(count) - (1 << (count - 1)));
    }

private:
    const uint8_t* ptr_;
    uint8_t bit_pos_ = 0;
};

bool draw_efont_cn_glyph(uint32_t codepoint, int x, int y, uint32_t fg, int scale = 1)
{
    if (codepoint > 0xFFFF) {
        return false;
    }
    const uint8_t* glyph = find_efont_cn_glyph(static_cast<uint16_t>(codepoint));
    if (glyph == nullptr) {
        return false;
    }

    FontDecoder decoder(glyph);
    uint8_t width = decoder.unsigned_bits(font_header(4));
    uint8_t height = decoder.unsigned_bits(font_header(5));
    int x_offset = decoder.signed_bits(font_header(6));
    int y_offset = decoder.signed_bits(font_header(7));
    decoder.signed_bits(font_header(8));  // x advance
    if (width == 0 || height == 0) {
        return true;
    }

    int baseline = font_i8(10) + font_i8(12);
    scale = std::max(1, scale);
    int draw_y = y + (baseline - y_offset - height) * scale;
    int draw_x = x + x_offset * scale;
    uint32_t run_lengths[2] = {};
    uint32_t lx = 0;
    uint32_t ly = 0;

    M5.Display.startWrite();
    while (ly < height) {
        run_lengths[0] = decoder.unsigned_bits(font_header(2));
        run_lengths[1] = decoder.unsigned_bits(font_header(3));
        bool foreground = false;
        do {
            uint32_t length = run_lengths[foreground ? 1 : 0];
            while (length > 0 && ly < height) {
                uint32_t count = std::min<uint32_t>(length, width - lx);
                length -= count;
                if (foreground && count > 0) {
                    M5.Display.fillRect(draw_x + lx * scale, draw_y + ly * scale, count * scale, scale, fg);
                }
                lx += count;
                if (lx == width) {
                    lx = 0;
                    ++ly;
                }
            }
            foreground = !foreground;
        } while (foreground || decoder.unsigned_bits(1) != 0);
    }
    M5.Display.endWrite();
    return true;
}

uint32_t ansi_color(uint8_t color, bool bold)
{
    static constexpr uint32_t kNormal[] = {
        0x101820,  // black
        0xD34D4D,  // red
        0x54C46B,  // green
        0xD7B94C,  // yellow
        0x5EA1FF,  // blue
        0xC678DD,  // magenta
        0x4EC9B0,  // cyan
        0xE8F0F2,  // white
    };
    static constexpr uint32_t kBright[] = {
        0x2C3A42,
        0xFF6B6B,
        0x7CE38B,
        0xFFE073,
        0x80BCFF,
        0xE39DFF,
        0x77E6D2,
        0xFFFFFF,
    };
    bool bright = bold;
    if (color >= 8 && color < 16) {
        bright = true;
        color -= 8;
    }
    color = color < 8 ? color : 7;
    return bright ? kBright[color] : kNormal[color];
}

void header(const std::string& title)
{
    use_ui_font();
    M5.Display.fillScreen(kBg);
    M5.Display.setTextColor(kAccent, kBg);
    M5.Display.setCursor(6, 4);
    M5.Display.print(title.c_str());
    M5.Display.setTextColor(kDim, kBg);
    M5.Display.setCursor(M5.Display.width() - 70, 4);
    M5.Display.print(g_wifi_connected ? "W:OK " : "W:-- ");
    M5.Display.print(g_ssh_connected ? "S:OK" : "S:--");
    M5.Display.drawFastHLine(0, 18, M5.Display.width(), kAccent);
}

void draw_header_status()
{
    use_ui_font();
    M5.Display.fillRect(M5.Display.width() - 74, 2, 74, 14, kBg);
    M5.Display.setTextColor(kDim, kBg);
    M5.Display.setCursor(M5.Display.width() - 70, 4);
    M5.Display.print(g_wifi_connected ? "W:OK " : "W:-- ");
    M5.Display.print(g_ssh_connected ? "S:OK" : "S:--");
}
}  // namespace

void Display::begin()
{
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Display.setRotation(1);
    use_ui_font();
    M5.Display.fillScreen(kBg);
}

void Display::set_status_flags(bool wifi_connected, bool ssh_connected)
{
    g_wifi_connected = wifi_connected;
    g_ssh_connected = ssh_connected;
}

void Display::show_status(const std::string& title, const std::vector<std::string>& lines)
{
    header(title);
    int y = 26;
    M5.Display.setTextColor(kFg, kBg);
    for (const auto& line : lines) {
        M5.Display.setCursor(6, y);
        M5.Display.print(line.c_str());
        y += 12;
        if (y > M5.Display.height() - 10) {
            break;
        }
    }
}

void Display::show_menu(const std::string& title, const std::vector<std::string>& items, int selected,
                        const std::string& footer)
{
    header(title);
    int y = 24;
    int first = selected > 7 ? selected - 7 : 0;
    int last = std::min(static_cast<int>(items.size()), first + 8);
    for (int i = first; i < last; ++i) {
        bool active = i == selected;
        uint32_t bg = active ? kAccent : kBg;
        uint32_t fg = active ? kBg : kFg;
        M5.Display.fillRect(3, y - 2, M5.Display.width() - 6, 12, bg);
        M5.Display.setTextColor(fg, bg);
        M5.Display.setCursor(8, y);
        M5.Display.print(items[i].c_str());
        y += 14;
    }
    M5.Display.setTextColor(kDim, kBg);
    M5.Display.setCursor(6, M5.Display.height() - 10);
    M5.Display.print(footer.c_str());
}

void Display::show_text_input(const std::string& title, const std::string& label, const std::string& value, bool secret)
{
    header(title);
    M5.Display.setTextColor(kDim, kBg);
    M5.Display.setCursor(6, 32);
    M5.Display.print(label.c_str());

    M5.Display.fillRect(6, 50, M5.Display.width() - 12, 20, 0x18242D);
    M5.Display.drawRect(6, 50, M5.Display.width() - 12, 20, kWarn);
    M5.Display.setTextColor(kFg, 0x18242D);
    M5.Display.setCursor(10, 57);
    if (secret) {
        std::string masked(value.size(), '*');
        M5.Display.print(masked.c_str());
    } else {
        M5.Display.print(value.c_str());
    }

    M5.Display.setTextColor(kDim, kBg);
    M5.Display.setCursor(6, M5.Display.height() - 10);
    M5.Display.print("Enter save  Esc cancel  Del backspace");
}

void Display::show_terminal(const std::vector<std::string>& lines, const std::string& input)
{
    header("Terminal");
    int max_lines = 8;
    int first = static_cast<int>(lines.size()) > max_lines ? static_cast<int>(lines.size()) - max_lines : 0;
    int y = 24;
    M5.Display.setTextColor(kFg, kBg);
    for (int i = first; i < static_cast<int>(lines.size()); ++i) {
        M5.Display.setCursor(6, y);
        M5.Display.print(lines[i].substr(0, 38).c_str());
        y += 12;
    }

    M5.Display.fillRect(0, M5.Display.height() - 22, M5.Display.width(), 22, 0x18242D);
    M5.Display.setCursor(6, M5.Display.height() - 16);
    if (input.empty()) {
        M5.Display.setTextColor(kDim, 0x18242D);
        M5.Display.print("Raw shell  Esc back");
    } else {
        M5.Display.setTextColor(kAccent, 0x18242D);
        M5.Display.print("$ ");
        M5.Display.setTextColor(kFg, 0x18242D);
        std::string visible = input.size() > 32 ? input.substr(input.size() - 32) : input;
        M5.Display.print(visible.c_str());
    }
}

void Display::draw_terminal_frame()
{
    header("Terminal");
    M5.Display.fillRect(0, kTerminalTop, M5.Display.width(), M5.Display.height() - kTerminalTop, 0x000000);
}

void Display::draw_terminal_rows(const TerminalEmulator& terminal, const std::vector<int>& rows)
{
    draw_header_status();
    for (int row : rows) {
        if (row < 0 || row >= TerminalEmulator::kRows) {
            continue;
        }
        int y = kTerminalTop + row * kCellHeight;
        for (int col = 0; col < TerminalEmulator::kCols; ++col) {
            const auto& cell = terminal.cell(row, col);
            bool cursor = terminal.cursor_visible() && row == terminal.cursor_row() && col == terminal.cursor_col();
            uint32_t fg = ansi_color(cell.fg, cell.bold);
            uint32_t bg = ansi_color(cell.bg, false);
            if (cell.inverse || cursor) {
                std::swap(fg, bg);
            }
            int x = col * kCellWidth;
            M5.Display.fillRect(x, y, kCellWidth, kCellHeight, bg);
        }

        use_terminal_font();
        for (int col = 0; col < TerminalEmulator::kCols; ++col) {
            const auto& cell = terminal.cell(row, col);
            if (cell.continuation) {
                continue;
            }
            bool cursor = terminal.cursor_visible() && row == terminal.cursor_row() && col == terminal.cursor_col();
            uint32_t fg = ansi_color(cell.fg, cell.bold);
            uint32_t bg = ansi_color(cell.bg, false);
            if (cell.inverse || cursor) {
                std::swap(fg, bg);
            }
            int x = col * kCellWidth;
            M5.Display.setTextColor(fg, bg);
            M5.Display.setCursor(x, y);
            uint32_t codepoint = cell.codepoint == 0 ? ' ' : cell.codepoint;
            if (codepoint < 128) {
                char out[2] = {static_cast<char>(codepoint), '\0'};
                M5.Display.print(out);
            } else if (!draw_efont_cn_glyph(codepoint, x, y, fg)) {
                M5.Display.print("?");
            }
            if (cell.underline) {
                int width = std::max<int>(1, cell.width) * kCellWidth;
                M5.Display.drawFastHLine(x, y + kCellHeight - 2, width, fg);
            }
        }
    }
}

}  // namespace adv
