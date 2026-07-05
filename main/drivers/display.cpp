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
constexpr uint32_t kPanel = 0x18242D;
constexpr int kHeaderFullHeight = 20;
constexpr int kHeaderCompactHeight = 12;
constexpr int kDefaultCellWidth = 6;
constexpr int kDefaultCellHeight = 12;

bool g_wifi_connected = false;
bool g_ssh_connected = false;
TerminalChromeMode g_terminal_chrome_mode = TerminalChromeMode::kFull;
TerminalTheme g_terminal_theme = TerminalTheme::kAdvDark;
TerminalFontFace g_terminal_font_face = TerminalFontFace::kCjk12;

uint32_t ansi_color(uint8_t color, bool bold);

uint32_t dim_color(uint32_t color)
{
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    r = static_cast<uint8_t>(r * 2 / 3);
    g = static_cast<uint8_t>(g * 2 / 3);
    b = static_cast<uint8_t>(b * 2 / 3);
    return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
}

struct TerminalPalette {
    uint32_t normal[8];
    uint32_t bright[8];
};

static constexpr TerminalPalette kTerminalThemes[] = {
    {
        {0x101820, 0xD34D4D, 0x54C46B, 0xD7B94C, 0x5EA1FF, 0xC678DD, 0x4EC9B0, 0xE8F0F2},
        {0x2C3A42, 0xFF6B6B, 0x7CE38B, 0xFFE073, 0x80BCFF, 0xE39DFF, 0x77E6D2, 0xFFFFFF},
    },
    {
        {0x000000, 0xD34D4D, 0x54C46B, 0xD7B94C, 0x5EA1FF, 0xC678DD, 0x4EC9B0, 0xE8F0F2},
        {0x222A30, 0xFF6B6B, 0x7CE38B, 0xFFE073, 0x80BCFF, 0xE39DFF, 0x77E6D2, 0xFFFFFF},
    },
    {
        {0x002B36, 0xDC322F, 0x859900, 0xB58900, 0x268BD2, 0xD33682, 0x2AA198, 0xEEE8D5},
        {0x073642, 0xCB4B16, 0x586E75, 0x657B83, 0x839496, 0x6C71C4, 0x93A1A1, 0xFDF6E3},
    },
    {
        {0x282828, 0xCC241D, 0x98971A, 0xD79921, 0x458588, 0xB16286, 0x689D6A, 0xEBDBB2},
        {0x928374, 0xFB4934, 0xB8BB26, 0xFABD2F, 0x83A598, 0xD3869B, 0x8EC07C, 0xFBF1C7},
    },
    {
        {0x282A36, 0xFF5555, 0x50FA7B, 0xF1FA8C, 0xBD93F9, 0xFF79C6, 0x8BE9FD, 0xF8F8F2},
        {0x6272A4, 0xFF6E6E, 0x69FF94, 0xFFFFA5, 0xD6ACFF, 0xFF92DF, 0xA4FFFF, 0xFFFFFF},
    },
    {
        {0x2E3440, 0xBF616A, 0xA3BE8C, 0xEBCB8B, 0x81A1C1, 0xB48EAD, 0x88C0D0, 0xE5E9F0},
        {0x4C566A, 0xBF616A, 0xA3BE8C, 0xEBCB8B, 0x5E81AC, 0xB48EAD, 0x8FBCBB, 0xECEFF4},
    },
    {
        {0x1A1B26, 0xF7768E, 0x9ECE6A, 0xE0AF68, 0x7AA2F7, 0xBB9AF7, 0x7DCFFF, 0xC0CAF5},
        {0x414868, 0xF7768E, 0x9ECE6A, 0xE0AF68, 0x7AA2F7, 0xBB9AF7, 0x7DCFFF, 0xFFFFFF},
    },
    {
        {0x1E1E2E, 0xF38BA8, 0xA6E3A1, 0xF9E2AF, 0x89B4FA, 0xF5C2E7, 0x94E2D5, 0xCDD6F4},
        {0x585B70, 0xF38BA8, 0xA6E3A1, 0xF9E2AF, 0x89B4FA, 0xF5C2E7, 0x94E2D5, 0xFFFFFF},
    },
    {
        {0x272822, 0xF92672, 0xA6E22E, 0xE6DB74, 0x66D9EF, 0xAE81FF, 0xA1EFE4, 0xF8F8F2},
        {0x75715E, 0xF92672, 0xA6E22E, 0xF4BF75, 0x66D9EF, 0xAE81FF, 0xA1EFE4, 0xFFFFFF},
    },
};

const TerminalPalette& terminal_palette()
{
    size_t index = static_cast<size_t>(g_terminal_theme);
    if (index >= static_cast<size_t>(TerminalTheme::kCount)) {
        index = 0;
    }
    return kTerminalThemes[index];
}

const uint8_t* terminal_efont()
{
    switch (g_terminal_font_face) {
        case TerminalFontFace::kCjk12Bold:
            return lgfx_efont_cn_12_b;
        case TerminalFontFace::kCjk10:
            return lgfx_efont_cn_10;
        case TerminalFontFace::kCjk14:
            return lgfx_efont_cn_14;
        case TerminalFontFace::kCjk12:
        default:
            return lgfx_efont_cn_12;
    }
}

uint32_t terminal_background()
{
    return terminal_palette().normal[0];
}

int terminal_chrome_height(TerminalChromeMode mode)
{
    switch (mode) {
        case TerminalChromeMode::kHidden:
            return 0;
        case TerminalChromeMode::kCompact:
            return kHeaderCompactHeight;
        case TerminalChromeMode::kFull:
        default:
            return kHeaderFullHeight;
    }
}

void use_ui_font()
{
    M5.Display.setFont(&fonts::Font0);
    M5.Display.setTextSize(1);
    M5.Display.setTextWrap(false, false);
}

void use_terminal_font(const TerminalLayout& layout)
{
    if (layout.ascii_8x16) {
        M5.Display.setFont(&fonts::AsciiFont8x16);
    } else {
        M5.Display.setFont(&fonts::Font0);
    }
    M5.Display.setTextSize(layout.ascii_size);
    M5.Display.setTextWrap(false, false);
}

std::string fit_text(const std::string& text, int width)
{
    int max_chars = std::max(0, width / 6);
    if (static_cast<int>(text.size()) <= max_chars) {
        return text;
    }
    if (max_chars <= 1) {
        return "";
    }
    return text.substr(0, max_chars - 1) + "~";
}

int battery_level()
{
    int level = M5.Power.getBatteryLevel();
    if (level < 0 || level > 100) {
        return -1;
    }
    return std::clamp(level, 0, 100);
}

bool battery_charging()
{
    return M5.Power.isCharging() == m5::Power_Class::is_charging;
}

void draw_status_wifi_icon(int x, int y)
{
    uint32_t color = g_wifi_connected ? kAccent : kDim;
    int cx = x + 8;
    int cy = y + 11;
    M5.Display.drawArc(cx, cy, 9, 8, 220, 320, color);
    M5.Display.drawArc(cx, cy, 6, 5, 225, 315, color);
    M5.Display.fillCircle(cx, cy, 2, color);
    if (!g_wifi_connected) {
        M5.Display.drawLine(x + 2, y + 3, x + 14, y + 12, kWarn);
    }
}

void draw_status_ssh_icon(int x, int y)
{
    uint32_t color = g_ssh_connected ? kAccent : kDim;
    M5.Display.drawRoundRect(x + 3, y + 5, 12, 8, 2, color);
    M5.Display.drawRoundRect(x + 6, y + 1, 6, 8, 3, color);
    M5.Display.fillCircle(x + 9, y + 9, 1, color);
    if (!g_ssh_connected) {
        M5.Display.drawLine(x + 2, y + 3, x + 16, y + 12, kWarn);
    }
}

void draw_status_battery_icon(int x, int y)
{
    int level = battery_level();
    bool charging = battery_charging();
    uint32_t outline = level >= 0 ? kDim : dim_color(kDim);
    uint32_t fill = level >= 0 && level <= 15 ? kWarn : kAccent;

    M5.Display.drawRect(x, y + 2, 20, 8, outline);
    M5.Display.fillRect(x + 20, y + 5, 2, 3, outline);

    if (level >= 0) {
        int fill_width = std::clamp((level * 16 + 99) / 100, 1, 16);
        M5.Display.fillRect(x + 2, y + 4, fill_width, 4, fill);
    } else {
        M5.Display.drawFastHLine(x + 5, y + 6, 10, outline);
    }

    if (charging) {
        M5.Display.drawLine(x + 10, y + 3, x + 7, y + 8, kWarn);
        M5.Display.drawLine(x + 7, y + 8, x + 12, y + 7, kWarn);
        M5.Display.drawLine(x + 12, y + 7, x + 9, y + 11, kWarn);
    }
}

void draw_status_icons(int y)
{
    constexpr int kStatusWidth = 68;
    int width = M5.Display.width();
    int x = std::max(4, width - kStatusWidth - 4);
    M5.Display.fillRect(x - 2, std::max(0, y - 2), kStatusWidth + 6, 16, kBg);
    draw_status_wifi_icon(x, y);
    draw_status_ssh_icon(x + 22, y);
    draw_status_battery_icon(x + 44, y);
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
    const uint8_t* font = terminal_efont();
    return static_cast<int8_t>(font_byte(&font[index]));
}

uint8_t font_header(size_t index)
{
    const uint8_t* font = terminal_efont();
    return font_byte(&font[index]);
}

const uint8_t* find_efont_cn_glyph(uint16_t encoding)
{
    const uint8_t* base = terminal_efont();
    const uint8_t* font = &base[23];
    uint16_t start_upper = font_u16(&base[17]);
    uint16_t start_lower = font_u16(&base[19]);
    uint16_t start_unicode = font_u16(&base[21]);

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

void draw_terminal_line(const std::vector<TerminalCell>& line, int row, const TerminalLayout& layout, bool show_cursor,
                        int cursor_row, int cursor_col)
{
    int y = layout.top + row * layout.cell_height;
    for (int col = 0; col < layout.cols; ++col) {
        const auto& cell = col < static_cast<int>(line.size()) ? line[col] : TerminalCell{};
        bool cursor = show_cursor && row == cursor_row && col == cursor_col;
        uint32_t fg = ansi_color(cell.fg, cell.bold);
        uint32_t bg = ansi_color(cell.bg, false);
        if (cell.dim && !cell.bold) {
            fg = dim_color(fg);
        }
        if (cell.inverse || cursor) {
            std::swap(fg, bg);
        }
        int x = col * layout.cell_width;
        M5.Display.fillRect(x, y, layout.cell_width, layout.cell_height, bg);
    }

    use_terminal_font(layout);
    for (int col = 0; col < layout.cols; ++col) {
        const auto& cell = col < static_cast<int>(line.size()) ? line[col] : TerminalCell{};
        if (cell.continuation) {
            continue;
        }
        bool cursor = show_cursor && row == cursor_row && col == cursor_col;
        uint32_t fg = ansi_color(cell.fg, cell.bold);
        uint32_t bg = ansi_color(cell.bg, false);
        if (cell.dim && !cell.bold) {
            fg = dim_color(fg);
        }
        if (cell.inverse || cursor) {
            std::swap(fg, bg);
        }
        if (cell.hidden && !cursor) {
            fg = bg;
        }
        int x = col * layout.cell_width;
        M5.Display.setTextColor(fg, bg);
        M5.Display.setCursor(x, y);
        uint32_t codepoint = cell.codepoint == 0 ? ' ' : cell.codepoint;
        if (codepoint < 128) {
            char out[2] = {static_cast<char>(codepoint), '\0'};
            M5.Display.print(out);
        } else if (!draw_efont_cn_glyph(codepoint, x, y, fg, layout.cjk_scale)) {
            M5.Display.print("?");
        }
        if (cell.underline) {
            int width = std::max<int>(1, cell.width) * layout.cell_width;
            M5.Display.drawFastHLine(x, y + layout.cell_height - 2, width, fg);
        }
        if (cell.strikethrough) {
            int width = std::max<int>(1, cell.width) * layout.cell_width;
            M5.Display.drawFastHLine(x, y + layout.cell_height / 2, width, fg);
        }
    }
}

uint32_t ansi_color(uint8_t color, bool bold)
{
    bool bright = bold;
    if (color >= 8 && color < 16) {
        bright = true;
        color -= 8;
    }
    color = color < 8 ? color : 7;
    const auto& palette = terminal_palette();
    return bright ? palette.bright[color] : palette.normal[color];
}

void header(const std::string& title)
{
    use_ui_font();
    M5.Display.fillScreen(kBg);
    M5.Display.setTextColor(kAccent, kBg);
    M5.Display.setCursor(6, 4);
    M5.Display.print(fit_text(title, M5.Display.width() - 84).c_str());
    draw_status_icons(3);
    M5.Display.drawFastHLine(0, 18, M5.Display.width(), kAccent);
}

void terminal_header(const TerminalLayout& layout, const std::string& title)
{
    if (layout.chrome == TerminalChromeMode::kHidden) {
        M5.Display.fillScreen(terminal_background());
        return;
    }
    if (layout.chrome == TerminalChromeMode::kFull) {
        header(title);
        return;
    }

    use_ui_font();
    M5.Display.fillScreen(kBg);
    M5.Display.setTextColor(kAccent, kBg);
    M5.Display.setCursor(4, 2);
    M5.Display.print(fit_text(title, M5.Display.width() - 80).c_str());
    draw_status_icons(0);
    M5.Display.drawFastHLine(0, kHeaderCompactHeight - 1, M5.Display.width(), kAccent);
}

void draw_header_status()
{
    if (g_terminal_chrome_mode == TerminalChromeMode::kHidden) {
        return;
    }
    use_ui_font();
    draw_status_icons(g_terminal_chrome_mode == TerminalChromeMode::kCompact ? 0 : 3);
}

void draw_terminal_icon(int cx, int y, uint32_t color)
{
    M5.Display.drawRect(cx - 24, y, 48, 34, color);
    M5.Display.fillRect(cx - 22, y + 2, 44, 30, 0x000000);
    M5.Display.drawFastHLine(cx - 18, y + 8, 8, color);
    M5.Display.drawFastVLine(cx - 10, y + 8, 8, color);
    M5.Display.drawFastHLine(cx - 4, y + 18, 14, color);
}

void draw_wifi_icon(int cx, int y, uint32_t color)
{
    M5.Display.drawArc(cx, y + 32, 26, 22, 215, 325, color);
    M5.Display.drawArc(cx, y + 32, 18, 14, 220, 320, color);
    M5.Display.drawArc(cx, y + 32, 10, 6, 230, 310, color);
    M5.Display.fillCircle(cx, y + 32, 3, color);
}

void draw_ssh_icon(int cx, int y, uint32_t color)
{
    M5.Display.drawRect(cx - 21, y + 10, 42, 25, color);
    M5.Display.drawRoundRect(cx - 12, y, 24, 22, 8, color);
    M5.Display.fillCircle(cx, y + 22, 3, color);
    M5.Display.drawFastVLine(cx, y + 25, 6, color);
}

void draw_status_icon(int cx, int y, uint32_t color)
{
    M5.Display.drawCircle(cx, y + 18, 18, color);
    M5.Display.fillCircle(cx, y + 10, 2, color);
    M5.Display.fillRect(cx - 1, y + 16, 3, 14, color);
}

void draw_web_icon(int cx, int y, uint32_t color)
{
    M5.Display.drawCircle(cx, y + 20, 20, color);
    M5.Display.drawFastHLine(cx - 17, y + 20, 34, color);
    M5.Display.drawFastHLine(cx - 13, y + 11, 26, color);
    M5.Display.drawFastHLine(cx - 13, y + 29, 26, color);
    M5.Display.drawArc(cx, y + 20, 10, 20, 80, 280, color);
    M5.Display.drawArc(cx, y + 20, 10, 20, 260, 100, color);
}

void draw_launcher_icon(LauncherIcon icon, int cx, int y, uint32_t color)
{
    switch (icon) {
        case LauncherIcon::kWifi:
            draw_wifi_icon(cx, y, color);
            break;
        case LauncherIcon::kWeb:
            draw_web_icon(cx, y, color);
            break;
        case LauncherIcon::kSsh:
            draw_ssh_icon(cx, y, color);
            break;
        case LauncherIcon::kStatus:
            draw_status_icon(cx, y, color);
            break;
        case LauncherIcon::kTerminal:
        default:
            draw_terminal_icon(cx, y, color);
            break;
    }
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

void Display::set_terminal_chrome_mode(TerminalChromeMode mode)
{
    g_terminal_chrome_mode = mode;
}

void Display::set_terminal_theme(TerminalTheme theme)
{
    g_terminal_theme = theme;
}

void Display::set_terminal_font_face(TerminalFontFace face)
{
    g_terminal_font_face = face;
}

TerminalLayout Display::terminal_layout(TerminalFontMode mode) const
{
    TerminalLayout layout;
    layout.mode = mode;
    layout.chrome = g_terminal_chrome_mode;
    layout.top = terminal_chrome_height(g_terminal_chrome_mode);

    switch (mode) {
        case TerminalFontMode::kReadable:
            layout.cell_width = 8;
            layout.cell_height = 16;
            layout.ascii_size = 1;
            layout.cjk_scale = 1;
            layout.ascii_8x16 = true;
            break;
        case TerminalFontMode::kLarge:
            layout.cell_width = 12;
            layout.cell_height = 24;
            layout.ascii_size = 2;
            layout.cjk_scale = 2;
            layout.ascii_8x16 = false;
            break;
        case TerminalFontMode::kCompact:
        default:
            layout.cell_width = kDefaultCellWidth;
            layout.cell_height = kDefaultCellHeight;
            layout.ascii_size = 1;
            layout.cjk_scale = 1;
            layout.ascii_8x16 = false;
            break;
    }

    if (g_terminal_font_face == TerminalFontFace::kCjk14) {
        if (mode == TerminalFontMode::kCompact) {
            layout.cell_width = 7;
            layout.cell_height = 14;
        } else if (mode == TerminalFontMode::kLarge) {
            layout.cell_width = 14;
            layout.cell_height = 28;
        }
    }

    int width = std::max(1, static_cast<int>(M5.Display.width()));
    int height = std::max(1, static_cast<int>(M5.Display.height()) - layout.top);
    layout.cols = std::max(8, width / layout.cell_width);
    layout.rows = std::max(4, height / layout.cell_height);
    return layout;
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

void Display::show_launcher(const std::vector<LauncherItem>& items, int selected, const std::string& footer)
{
    header("Cardputer-Adv");
    if (items.empty()) {
        return;
    }

    selected = std::clamp(selected, 0, static_cast<int>(items.size()) - 1);
    const auto& item = items[selected];
    int width = M5.Display.width();
    int height = M5.Display.height();
    int cx = width / 2;

    uint32_t icon_color = kAccent;
    M5.Display.fillCircle(cx, 52, 31, kPanel);
    M5.Display.drawCircle(cx, 52, 31, dim_color(kAccent));
    draw_launcher_icon(item.icon, cx, 34, icon_color);

    M5.Display.drawLine(18, 62, 10, 68, kDim);
    M5.Display.drawLine(10, 68, 18, 74, kDim);
    M5.Display.drawLine(width - 18, 62, width - 10, 68, kDim);
    M5.Display.drawLine(width - 10, 68, width - 18, 74, kDim);

    M5.Display.setTextColor(kFg, kBg);
    M5.Display.setCursor(std::max(6, cx - static_cast<int>(item.title.size()) * 3), 84);
    M5.Display.print(fit_text(item.title, width - 12).c_str());

    M5.Display.setTextColor(kDim, kBg);
    std::string subtitle = fit_text(item.subtitle, width - 12);
    M5.Display.setCursor(std::max(6, cx - static_cast<int>(subtitle.size()) * 3), 98);
    M5.Display.print(subtitle.c_str());

    int dots_width = static_cast<int>(items.size()) * 10 - 4;
    int dot_x = cx - dots_width / 2;
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        if (i == selected) {
            M5.Display.fillRoundRect(dot_x + i * 10 - 4, 113, 8, 5, 2, kAccent);
        } else {
            M5.Display.fillCircle(dot_x + i * 10, 116, 2, dim_color(kDim));
        }
    }

    M5.Display.setTextColor(kDim, kBg);
    M5.Display.setCursor(6, height - 10);
    M5.Display.print(fit_text(footer, width - 12).c_str());
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
    draw_terminal_frame(terminal_layout(TerminalFontMode::kCompact));
}

void Display::draw_terminal_frame(const TerminalLayout& layout, const std::string& title)
{
    terminal_header(layout, title);
    int terminal_height = layout.rows * layout.cell_height;
    M5.Display.fillRect(0, layout.top, M5.Display.width(), terminal_height, terminal_background());
    int used_bottom = layout.top + terminal_height;
    if (used_bottom < M5.Display.height()) {
        M5.Display.fillRect(0, used_bottom, M5.Display.width(), M5.Display.height() - used_bottom, kBg);
    }
}

void Display::draw_terminal_rows(const TerminalEmulator& terminal, const std::vector<int>& rows)
{
    draw_terminal_rows(terminal, rows, terminal_layout(TerminalFontMode::kCompact));
}

void Display::draw_terminal_rows(const TerminalEmulator& terminal, const std::vector<int>& rows,
                                 const TerminalLayout& layout)
{
    draw_header_status();
    for (int row : rows) {
        if (row < 0 || row >= terminal.rows() || row >= layout.rows) {
            continue;
        }
        draw_terminal_line(terminal.line(row), row, layout, terminal.cursor_visible(), terminal.cursor_row(),
                           terminal.cursor_col());
    }
}

void Display::draw_terminal_snapshot(const std::vector<std::vector<TerminalCell>>& lines,
                                     const TerminalLayout& layout)
{
    draw_header_status();
    for (int row = 0; row < layout.rows; ++row) {
        const auto blank = std::vector<TerminalCell>(layout.cols);
        const auto& line = row < static_cast<int>(lines.size()) ? lines[row] : blank;
        draw_terminal_line(line, row, layout, false, -1, -1);
    }
}

}  // namespace adv
