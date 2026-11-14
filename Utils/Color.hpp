//
// Terminal / Utils / Color.hpp
// 终端色彩系统：16 色（Win32 属性位） + 24 位真彩（ANSI/VT） + 样式组合
//

#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <string>
#include <windows.h>

namespace Terminal {

    //======================================================================
    // 16 色经典调色板（VGA 顺序），索引位布局与 Win32 属性位一致：
    //   bit0 = 蓝  bit1 = 绿  bit2 = 红  bit3 = 高亮(INTENSITY)
    //======================================================================
    namespace Detail {
        inline constexpr unsigned char kPalette16[16][3] = {
            { 0,   0,   0 }, // 0  black
            { 0,   0,   128 }, // 1  dark blue
            { 0,   128, 0 }, // 2  dark green
            { 0,   128, 128 }, // 3  dark cyan
            { 128, 0,   0 }, // 4  dark red
            { 128, 0,   128 }, // 5  dark magenta
            { 128, 128, 0 }, // 6  dark yellow / olive
            { 192, 192, 192 }, // 7  light gray
            { 128, 128, 128 }, // 8  dark gray
            { 0,   0,   255 }, // 9  blue
            { 0,   255, 0 }, // 10 green
            { 0,   255, 255 }, // 11 cyan
            { 255, 0,   0 }, // 12 red
            { 255, 0,   255 }, // 13 magenta
            { 255, 255, 0 }, // 14 yellow
            { 255, 255, 255 }  // 15 white
        };

        // 索引 -> Win32 前景属性低 4 位
        constexpr DWORD AttrBitsOf(const int index) noexcept {
            return ((index & 1) ? FOREGROUND_BLUE : 0u)
                | ((index & 2) ? FOREGROUND_GREEN : 0u)
                | ((index & 4) ? FOREGROUND_RED : 0u)
                | ((index & 8) ? FOREGROUND_INTENSITY : 0u);
        }

        // Win32 前景属性低 4 位 -> 索引
        constexpr int IndexOfAttrBits(const DWORD bits) noexcept {
            int i = 0;
            if (bits & FOREGROUND_BLUE)      i |= 1;
            if (bits & FOREGROUND_GREEN)     i |= 2;
            if (bits & FOREGROUND_RED)       i |= 4;
            if (bits & FOREGROUND_INTENSITY) i |= 8;
            return i;
        }

        // COMMON_LVB_* 在部分 SDK 头里未定义，做兜底
#ifndef COMMON_LVB_UNDERSCORE
#define COMMON_LVB_UNDERSCORE 0x8000
#endif
#ifndef COMMON_LVB_REVERSE_VIDEO
#define COMMON_LVB_REVERSE_VIDEO 0x4000
#endif
    }

    //======================================================================
    // Color：同时承载 24 位 RGB 与 16 色索引
    //   - 用 OfIndex() 构造时携带精确索引（无损）
    //   - 用 OfRGB()  构造时按最近色量化到 16 色，同时保留原始 RGB
    //======================================================================
    struct Color {
        std::uint8_t r{ 192 };
        std::uint8_t g{ 192 };
        std::uint8_t b{ 192 };
        std::int16_t index{ -1 }; // >=0 表示由 16 色索引精确指定

        constexpr Color() noexcept = default;

        constexpr Color(const std::uint8_t rr, const std::uint8_t gg, const std::uint8_t bb,
            const std::int16_t idx = -1) noexcept
            : r(rr), g(gg), b(bb), index(idx) {
        }

        // ---------- 构造 ----------
        static constexpr Color OfIndex(const int i) noexcept {
            const int idx = (i & 15);
            return Color(Detail::kPalette16[idx][0],
                Detail::kPalette16[idx][1],
                Detail::kPalette16[idx][2],
                static_cast<std::int16_t>(idx));
        }

        static constexpr Color OfRGB(const std::uint8_t rr, const std::uint8_t gg, const std::uint8_t bb) noexcept {
            return Color(rr, gg, bb);
        }

        // ---------- 量化 ----------
        [[nodiscard]] int NearestIndex() const noexcept {
            if (index >= 0) return index < 16 ? static_cast<int>(index) : 15;
            int best = 0;
            double bestDist = 1e30;
            for (int i = 0; i < 16; ++i) {
                const double dr = static_cast<double>(r) - Detail::kPalette16[i][0];
                const double dg = static_cast<double>(g) - Detail::kPalette16[i][1];
                const double db = static_cast<double>(b) - Detail::kPalette16[i][2];
                // 人眼对绿色更敏感，加权距离让量化结果更自然
                const double dist = 2.0 * dr * dr + 4.0 * dg * dg + 3.0 * db * db;
                if (dist < bestDist) { bestDist = dist; best = i; }
            }
            return best;
        }

        [[nodiscard]] DWORD ToAttrFg() const noexcept { return Detail::AttrBitsOf(NearestIndex()); }
        [[nodiscard]] DWORD ToAttrBg() const noexcept { return Detail::AttrBitsOf(NearestIndex()) << 4; }

        // 是否精确 16 色
        [[nodiscard]] constexpr bool IsIndexed() const noexcept { return index >= 0; }

        // ---------- 运算 ----------
        [[nodiscard]] static Color Blend(const Color& a, const Color& b, const double t) noexcept {
            const double k = std::clamp(t, 0.0, 1.0);
            return Color(static_cast<std::uint8_t>(a.r + (b.r - a.r) * k),
                static_cast<std::uint8_t>(a.g + (b.g - a.g) * k),
                static_cast<std::uint8_t>(a.b + (b.b - a.b) * k));
        }

        [[nodiscard]] Color Lighten(const double k) const noexcept {
            return Blend(*this, Color::OfIndex(15), k);
        }

        [[nodiscard]] Color Darken(const double k) const noexcept {
            return Blend(*this, Color::OfIndex(0), k);
        }

        [[nodiscard]] Color Invcolor(const Color& c) {
            return Color(0xff - c.r, 0xff - c.g, 0xff - c.b);
        }

        // ---------- 文本 ----------
        [[nodiscard]] std::string Hex() const {
            static const char* digits = "0123456789ABCDEF";
            std::string s = "#";
            s.push_back(digits[r >> 4]); s.push_back(digits[r & 15]);
            s.push_back(digits[g >> 4]); s.push_back(digits[g & 15]);
            s.push_back(digits[b >> 4]); s.push_back(digits[b & 15]);
            return s;
        }

        [[nodiscard]] static std::array<Color, 16> Palette() {
            std::array<Color, 16> out{};
            for (int i = 0; i < 16; ++i) out[static_cast<size_t>(i)] = Color::OfIndex(i);
            return out;
        }

        // 线性渐变色带（用于标题、进度条美化）
        [[nodiscard]] static std::array<Color, 16> Gradient(const Color& from, const Color& to,
            const int steps = 16) {
            std::array<Color, 16> out{};
            const int n = std::clamp(steps, 1, 16);
            for (int i = 0; i < 16; ++i) {
                const double t = (n <= 1) ? 0.0 : static_cast<double>(std::min(i, n - 1)) / (n - 1);
                out[static_cast<size_t>(i)] = Color::Blend(from, to, t);
            }
            return out;
        }

        // ---------- 解析 ----------
        // 支持 "#RRGGBB" / "#RGB" / "r,g,b" / 颜色名（含 bright-/dark- 前缀）
        static Color Parse(const std::string& text, const Color& fallback = Color::OfIndex(7)) {
            std::string s;
            s.reserve(text.size());
            for (const char c : text) {
                if (c != ' ' && c != '\t' && c != '\r' && c != '\n') s.push_back(static_cast<char>(std::tolower(c)));
            }
            if (s.empty()) return fallback;

            auto hexVal = [](const char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                return -1;
                };

            if (s.size() == 7 && s[0] == '#') {
                const int r1 = hexVal(s[1]), r2 = hexVal(s[2]);
                const int g1 = hexVal(s[3]), g2 = hexVal(s[4]);
                const int b1 = hexVal(s[5]), b2 = hexVal(s[6]);
                if (r1 >= 0 && g1 >= 0 && b1 >= 0)
                    return Color::OfRGB(static_cast<std::uint8_t>(r1 * 16 + r2),
                        static_cast<std::uint8_t>(g1 * 16 + g2),
                        static_cast<std::uint8_t>(b1 * 16 + b2));
                return fallback;
            }
            if (s.size() == 4 && s[0] == '#') {
                const int r1 = hexVal(s[1]), g1 = hexVal(s[2]), b1 = hexVal(s[3]);
                if (r1 >= 0 && g1 >= 0 && b1 >= 0)
                    return Color::OfRGB(static_cast<std::uint8_t>(r1 * 17),
                        static_cast<std::uint8_t>(g1 * 17),
                        static_cast<std::uint8_t>(b1 * 17));
                return fallback;
            }

            // "r,g,b"
            if (s.find(',') != std::string::npos) {
                int v[3] = { 0, 0, 0 };
                size_t p = 0, k = 0;
                for (; k < 3 && p < s.size(); ++k) {
                    int acc = 0;
                    bool any = false;
                    while (p < s.size() && s[p] >= '0' && s[p] <= '9') {
                        acc = acc * 10 + (s[p] - '0');
                        any = true;
                        ++p;
                    }
                    v[k] = std::clamp(acc, 0, 255);
                    if (!any) return fallback;
                    if (p < s.size() && s[p] == ',') ++p;
                }
                return Color::OfRGB(static_cast<std::uint8_t>(v[0]),
                    static_cast<std::uint8_t>(v[1]),
                    static_cast<std::uint8_t>(v[2]));
            }

            // 颜色名
            struct Entry { const char* name; int index; };
            static const Entry names[] = {
                { "black",0 },{ "darkblue",1 },{ "darkgreen",2 },{ "darkcyan",3 },
                { "darkred",4 },{ "darkmagenta",5 },{ "darkyellow",6 },{ "olive",6 },
                { "lightgray",7 },{ "grey",7 },{ "gray",7 },{ "lightgrey",7 },
                { "darkgray",8 },{ "darkgrey",8 },{ "grey50",8 },
                { "blue",9 },{ "green",10 },{ "cyan",11 },{ "aqua",11 },
                { "red",12 },{ "magenta",13 },{ "purple",13 },{ "yellow",14 },
                { "white",15 },{ "brightwhite",15 }
            };
            for (const auto& e : names) {
                if (s == e.name) return Color::OfIndex(e.index);
            }
            // 0~15 数字直接当索引
            if (s.size() <= 2 && std::isdigit(static_cast<unsigned char>(s[0]))) {
                const int i = std::stoi(s);
                if (i >= 0 && i <= 15) return Color::OfIndex(i);
            }
            return fallback;
        }

        [[nodiscard]] bool Equals(const Color& o) const noexcept {
            return r == o.r && g == o.g && b == o.b;
        }

        [[nodiscard]] bool operator==(const Color& o) const noexcept { return Equals(o); }
        [[nodiscard]] bool operator!=(const Color& o) const noexcept { return !Equals(o); }
    };

    //======================================================================
    // 具名调色板
    //======================================================================
    namespace Colors {
        inline constexpr Color Black = Color::OfIndex(0);
        inline constexpr Color DarkBlue = Color::OfIndex(1);
        inline constexpr Color DarkGreen = Color::OfIndex(2);
        inline constexpr Color DarkCyan = Color::OfIndex(3);
        inline constexpr Color DarkRed = Color::OfIndex(4);
        inline constexpr Color DarkMagenta = Color::OfIndex(5);
        inline constexpr Color DarkYellow = Color::OfIndex(6);
        inline constexpr Color Gray = Color::OfIndex(7);
        inline constexpr Color DarkGray = Color::OfIndex(8);
        inline constexpr Color Blue = Color::OfIndex(9);
        inline constexpr Color Green = Color::OfIndex(10);
        inline constexpr Color Cyan = Color::OfIndex(11);
        inline constexpr Color Red = Color::OfIndex(12);
        inline constexpr Color Magenta = Color::OfIndex(13);
        inline constexpr Color Yellow = Color::OfIndex(14);
        inline constexpr Color White = Color::OfIndex(15);

        // 常用语义色（默认暗色主题，可由 Theme 覆盖）
        inline constexpr Color Primary = Color::OfIndex(11); // cyan
        inline constexpr Color Accent = Color::OfIndex(14); // yellow
        inline constexpr Color Success = Color::OfIndex(10); // green
        inline constexpr Color Warning = Color::OfIndex(14);
        inline constexpr Color Danger = Color::OfIndex(12);
        inline constexpr Color Muted = Color::OfIndex(8);
    }

    //======================================================================
    // Style：一次渲染需要的完整配色
    //======================================================================
    struct Style {
        Color fg{ Color::OfIndex(7) };
        Color bg{ Color::OfIndex(0) };
        bool bold{ false };
        bool underline{ false };
        bool reverse{ false };
        bool opaque{ false }; // 渲染前用 bg 铺满整个 bounds

        constexpr Style() noexcept = default;
        constexpr Style(const Color& fore, const Color& back) noexcept : fg(fore), bg(back) {}

        // ---------- 链式配置 ----------
        constexpr Style& SetFg(const Color& c) noexcept { fg = c; return *this; }
        constexpr Style& SetBg(const Color& c) noexcept { bg = c; return *this; }
        constexpr Style& SetBold(const bool b = true) noexcept { bold = b; return *this; }
        constexpr Style& SetUnderline(const bool b = true) noexcept { underline = b; return *this; }
        constexpr Style& SetReverse(const bool b = true) noexcept { reverse = b; return *this; }
        constexpr Style& SetOpaque(const bool b = true) noexcept { opaque = b; return *this; }

        [[nodiscard]] Style WithFg(const Color& c) const noexcept { Style s = *this; s.fg = c; return s; }
        [[nodiscard]] Style WithBg(const Color& c) const noexcept { Style s = *this; s.bg = c; return s; }
        [[nodiscard]] Style WithBold(const bool b = true) const noexcept { Style s = *this; s.bold = b; return s; }
        [[nodiscard]] Style WithOpaque(const bool b = true) const noexcept { Style s = *this; s.opaque = b; return s; }

        // ---------- 比较（脏判断用） ----------
        [[nodiscard]] bool operator==(const Style& o) const noexcept {
            return fg.Equals(o.fg) && bg.Equals(o.bg) && bold == o.bold &&
                underline == o.underline && reverse == o.reverse && opaque == o.opaque;
        }
        [[nodiscard]] bool operator!=(const Style& o) const noexcept { return !(*this == o); }

        // ---------- Win32 控制台属性 ----------
        [[nodiscard]] DWORD ToAttr() const noexcept {
            int fi = fg.NearestIndex();
            int bi = bg.NearestIndex();
            if (bold && (fi & 8) == 0) fi |= 8;
            if (reverse) std::swap(fi, bi);
            DWORD attr = Detail::AttrBitsOf(fi) | (Detail::AttrBitsOf(bi) << 4);
            if (underline) attr |= COMMON_LVB_UNDERSCORE;
            return attr;
        }

        [[nodiscard]] static Style FromAttr(const DWORD attr) noexcept {
            Style s;
            s.fg = Color::OfIndex(Detail::IndexOfAttrBits(attr & 0x0F));
            s.bg = Color::OfIndex(Detail::IndexOfAttrBits((attr & 0xF0) >> 4));
            s.underline = (attr & COMMON_LVB_UNDERSCORE) != 0;
            return s;
        }

        // ---------- ANSI 真彩色序列（需要 ENABLE_VIRTUAL_TERMINAL_PROCESSING） ----------
        [[nodiscard]] std::string AnsiPrefix() const {
            std::string s = "\x1b[0m";
            s += "\x1b[38;2;" + std::to_string(fg.r) + ";" + std::to_string(fg.g) + ";" + std::to_string(fg.b) + "m";
            s += "\x1b[48;2;" + std::to_string(bg.r) + ";" + std::to_string(bg.g) + ";" + std::to_string(bg.b) + "m";
            if (bold) s += "\x1b[1m";
            if (underline) s += "\x1b[4m";
            if (reverse) s += "\x1b[7m";
            return s;
        }

        static constexpr const char* AnsiReset = "\x1b[0m";
    };

    //======================================================================
    // 输出辅助
    //======================================================================
    inline bool EnableVirtualTerminal(const HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE)) {
        DWORD mode = 0;
        if (!GetConsoleMode(hOut, &mode)) return false;
        mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        if (!SetConsoleMode(hOut, mode)) return false;
        mode |= DISABLE_NEWLINE_AUTO_RETURN;
        SetConsoleMode(hOut, mode);
        return true;
    }

    // 用 ANSI 真彩色直接输出一段文本（需要 VT 支持）
    inline void OutRgb(const std::string& text, const Style& style, const HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE)) {
        const std::string payload = style.AnsiPrefix() + text + Style::AnsiReset;
        DWORD written = 0;
        WriteConsoleA(hOut, payload.c_str(), static_cast<DWORD>(payload.size()), &written, nullptr);
    }

    //======================================================================
    // 便捷函数
    //======================================================================
    inline Style FgStyle(const Color& fg) { return Style{}.WithFg(fg); }
    inline Style FgBgStyle(const Color& fg, const Color& bg) { return Style(fg, bg); }
    inline DWORD AttrOf(const Color& fg, const Color& bg) { return Style(fg, bg).ToAttr(); }
}
