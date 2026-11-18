//
// Terminal / Core / TerminalRichText.hpp
// TStrFmt：把带颜色控制符的 UTF-8 字符串解析成「文字段 + Style」；
// TStr  ：保存解析结果，可原样写入 ConsoleBackBuffer，不改动缓冲区结构。
//
// 控制语法：
//   {#RRGGBB} / {#RGB}   设置 RGB 颜色
//   {&x}                 设置 16 色索引（0~15）
//   {Red} / {DarkBlue}   设置具名颜色
//   {/r}                 重置为 TStrFmt 的基础颜色
//   {/c}                 彩虹模式：从红色开始，逐码点推进
//   {/r*0.8 + Red*0.6}   颜色表达式
//   \{ \} \\             转义字符
//
// 表达式语法：
//   * 乘系数 / ! 取反 / + 相加 / | 取平均；括号可改变优先级。
//

#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "../Utils/Color.hpp"
#include "../Utils/Utf8.hpp"
#include "TerminalBuffer.hpp"

namespace Terminal {

    //------------------------------------------------------------------
    // TStr：紧凑的「文字 + 属性」串
    // 同一 Style 的相邻文本会自动合并，避免每个字符一个节点。
    //------------------------------------------------------------------
    struct TStrRun {
        std::string text;
        Style style{};
    };

    class TStr {
    public:
        TStr() = default;

        [[nodiscard]] bool Empty() const noexcept { return m_runs.empty(); }
        [[nodiscard]] std::size_t RunCount() const noexcept { return m_runs.size(); }
        [[nodiscard]] const std::vector<TStrRun>& Runs() const noexcept { return m_runs; }

        void Clear() noexcept {
            m_runs.clear();
            m_width = -1;
        }

        void Reserve(const std::size_t runs) { m_runs.reserve(runs); }

        // 追加一段文本；如果与上一段 Style 相同就直接拼接，减少节点数量。
        void Append(std::string_view text, const Style& style) {
            if (text.empty()) return;
            if (!m_runs.empty() && m_runs.back().style == style) {
                m_runs.back().text.append(text.data(), text.size());
            }
            else {
                m_runs.push_back(TStrRun{ std::string(text), style });
            }
            m_width = -1;
        }

        void Append(const std::string& text, const Style& style) {
            Append(std::string_view(text), style);
        }

        void AppendCodepoint(const std::uint32_t cp, const Style& style) {
            Append(Utf8::Encode(cp), style);
        }

        // 纯文本（去掉所有属性），调试或复制用。
        [[nodiscard]] std::string Plain() const {
            std::string out;
            for (const auto& r : m_runs) out += r.text;
            return out;
        }

        // 最大行显示宽度（CJK 按 2 格）。
        [[nodiscard]] int Width() const {
            if (m_width >= 0) return m_width;
            int best = 0;
            int line = 0;
            for (const auto& r : m_runs) {
                std::size_t start = 0;
                while (start < r.text.size()) {
                    const auto nl = r.text.find('\n', start);
                    const auto end = (nl == std::string::npos) ? r.text.size() : nl;
                    line += Utf8::DisplayWidth(r.text.substr(start, end - start));
                    best = std::max(best, line);
                    if (nl == std::string::npos) break;
                    line = 0;
                    start = nl + 1;
                }
            }
            best = std::max(best, line);
            m_width = best;
            return best;
        }

        // 写入缓冲区；返回写入后的下一行 y（用于多行文本连续排版）。
        // 不改变 ConsoleBackBuffer 的数据模型，只是逐段调用 WriteText。
        int WriteTo(ConsoleBackBuffer& buf, const int x, const int y) const;
        int WriteTo(ConsoleBackBuffer& buf, const int x, const int y,
            const std::function<void(int&, int&)>& advance) const;

    private:
        std::vector<TStrRun> m_runs{};
        mutable int m_width{ -1 };
    };

    //------------------------------------------------------------------
    // TStrFmt：无状态解析器；每次 Parse 使用自己的基础 Style。
    //------------------------------------------------------------------
    class TStrFmt {
    public:
        Style baseStyle{};
        double rainbowStep{ 30.0 };   // 彩虹模式每个码点推进的色相角度

        TStrFmt() = default;
        explicit TStrFmt(const Style& base) : baseStyle(base) {}

        TStrFmt& SetStyle(const Style& s) { baseStyle = s; return *this; }
        TStrFmt& SetColor(const Color& c) { baseStyle.fg = c; return *this; }
        TStrFmt& SetBackground(const Color& c) { baseStyle.bg = c; return *this; }
        TStrFmt& SetRainbowStep(const double deg) { rainbowStep = deg; return *this; }

        [[nodiscard]] TStr Parse(std::string_view text) const;

        [[nodiscard]] static TStr Parse(std::string_view text, const Style& base) {
            TStrFmt fmt(base);
            return fmt.Parse(text);
        }

    private:
        struct Parser;
    };

    //------------------------------------------------------------------
    // 表达式解析器（TStrFmt::Parser 的私有实现）
    //------------------------------------------------------------------
    struct TStrFmt::Parser {
        struct Value {
            bool isNumber{ false };
            double number{ 0.0 };
            Color color{};
        };

        std::string_view src;
        const TStrFmt& fmt;
        TStr out;
        Color current;
        bool rainbow{ false };
        double hue{ 0.0 };
        std::string literal;

        explicit Parser(const std::string_view s, const TStrFmt& f)
            : src(s), fmt(f), current(f.baseStyle.fg) {
            out.Reserve(32);
        }

        [[nodiscard]] Style StyleFor(const Color& c) const {
            Style s = fmt.baseStyle;
            s.fg = c;
            return s;
        }

        static void SkipSpace(const std::string_view s, std::size_t& p) {
            while (p < s.size() && std::isspace(static_cast<unsigned char>(s[p]))) ++p;
        }

        static std::string Trim(const std::string_view s) {
            std::size_t b = 0, e = s.size();
            while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
            while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
            return std::string(s.substr(b, e - b));
        }

        static bool IsKnownColorName(const std::string& raw) {
            std::string s;
            s.reserve(raw.size());
            for (const char c : raw) {
                if (c != '-' && c != '_' && !std::isspace(static_cast<unsigned char>(c)))
                    s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            }
            if (s.empty()) return false;
            static const char* names[] = {
                "black", "darkblue", "darkgreen", "darkcyan", "darkred", "darkmagenta",
                "darkyellow", "olive", "lightgray", "grey", "gray", "lightgrey",
                "darkgray", "darkgrey", "grey50", "blue", "green", "cyan", "aqua",
                "red", "magenta", "purple", "yellow", "white", "brightwhite"
            };
            for (const char* n : names) if (s == n) return true;
            return false;
        }

        static Color RainbowAt(const double degree) {
            double h = std::fmod(degree, 360.0);
            if (h < 0.0) h += 360.0;
            const double x = 1.0 - std::abs(std::fmod(h / 60.0, 2.0) - 1.0);
            double r = 0.0, g = 0.0, b = 0.0;
            if (h < 60.0) { r = 1.0; g = x; }
            else if (h < 120.0) { r = x; g = 1.0; }
            else if (h < 180.0) { g = 1.0; b = x; }
            else if (h < 240.0) { g = x; b = 1.0; }
            else if (h < 300.0) { r = x; b = 1.0; }
            else { r = 1.0; b = x; }
            return Color::OfRGB(
                static_cast<std::uint8_t>(std::lround(r * 255.0)),
                static_cast<std::uint8_t>(std::lround(g * 255.0)),
                static_cast<std::uint8_t>(std::lround(b * 255.0)));
        }

        static Color ClampRGB(const int r, const int g, const int b) {
            return Color::OfRGB(
                static_cast<std::uint8_t>(std::clamp(r, 0, 255)),
                static_cast<std::uint8_t>(std::clamp(g, 0, 255)),
                static_cast<std::uint8_t>(std::clamp(b, 0, 255)));
        }

        static Color ScaleColor(const Color& c, const double factor) {
            const double k = std::clamp(factor, 0.0, 1.0);
            return ClampRGB(
                static_cast<int>(std::lround(c.r * k)),
                static_cast<int>(std::lround(c.g * k)),
                static_cast<int>(std::lround(c.b * k)));
        }

        static Color AddColor(const Color& a, const Color& b) {
            return ClampRGB(a.r + b.r, a.g + b.g, a.b + b.b);
        }

        static Color AverageColor(const Color& a, const Color& b) {
            return ClampRGB((a.r + b.r) / 2, (a.g + b.g) / 2, (a.b + b.b) / 2);
        }

        static Color InvertColor(const Color& c) {
            return Color::OfRGB(static_cast<std::uint8_t>(255 - c.r),
                static_cast<std::uint8_t>(255 - c.g),
                static_cast<std::uint8_t>(255 - c.b));
        }

        // ---- 表达式：sum := product (('+'|'|') product)* ----
        bool ParseExpression(const std::string_view s, Color& out) const {
            std::size_t p = 0;
            Value v;
            if (!ParseSum(s, p, v) || v.isNumber) return false;
            SkipSpace(s, p);
            if (p != s.size()) return false;
            out = v.color;
            return true;
        }

        bool ParseSum(const std::string_view s, std::size_t& p, Value& out) const {
            if (!ParseProduct(s, p, out)) return false;
            while (true) {
                SkipSpace(s, p);
                if (p >= s.size() || (s[p] != '+' && s[p] != '|')) break;
                const char op = s[p++];
                Value rhs;
                if (!ParseProduct(s, p, rhs)) return false;
                if (out.isNumber || rhs.isNumber) return false;
                out.color = (op == '+') ? AddColor(out.color, rhs.color)
                    : AverageColor(out.color, rhs.color);
            }
            return true;
        }

        bool ParseProduct(const std::string_view s, std::size_t& p, Value& out) const {
            if (!ParseUnary(s, p, out)) return false;
            while (true) {
                SkipSpace(s, p);
                if (p >= s.size() || s[p] != '*') break;
                ++p;
                Value rhs;
                if (!ParseUnary(s, p, rhs)) return false;
                if (out.isNumber && rhs.isNumber) {
                    out.number *= rhs.number;
                }
                else if (!out.isNumber && rhs.isNumber) {
                    out.color = ScaleColor(out.color, rhs.number);
                }
                else if (out.isNumber && !rhs.isNumber) {
                    out.color = ScaleColor(rhs.color, out.number);
                    out.isNumber = false;
                }
                else {
                    return false;
                }
            }
            return true;
        }

        bool ParseUnary(const std::string_view s, std::size_t& p, Value& out) const {
            SkipSpace(s, p);
            if (p < s.size() && s[p] == '!') {
                ++p;
                if (!ParseUnary(s, p, out) || out.isNumber) return false;
                out.color = InvertColor(out.color);
                return true;
            }
            return ParsePrimary(s, p, out);
        }

        bool ParsePrimary(const std::string_view s, std::size_t& p, Value& out) const {
            SkipSpace(s, p);
            if (p >= s.size()) return false;

            if (s[p] == '(') {
                ++p;
                if (!ParseSum(s, p, out)) return false;
                SkipSpace(s, p);
                if (p >= s.size() || s[p] != ')') return false;
                ++p;
                return true;
            }

            // 基础颜色：/r
            if (s[p] == '/' && p + 1 < s.size() && (s[p + 1] == 'r' || s[p + 1] == 'R')) {
                p += 2;
                out.isNumber = false;
                out.color = fmt.baseStyle.fg;
                return true;
            }

            // #RRGGBB / #RGB
            if (s[p] == '#') {
                const auto begin = p;
                ++p;
                while (p < s.size() && std::isxdigit(static_cast<unsigned char>(s[p]))) ++p;
                const auto len = p - begin - 1;
                if (len != 3 && len != 6) return false;
                out.isNumber = false;
                out.color = Color::Parse(std::string(s.substr(begin, p - begin)));
                return true;
            }

            // &x
            if (s[p] == '&') {
                ++p;
                const auto begin = p;
                while (p < s.size() && std::isdigit(static_cast<unsigned char>(s[p]))) ++p;
                if (begin == p) return false;
                const int idx = std::atoi(std::string(s.substr(begin, p - begin)).c_str());
                if (idx < 0 || idx > 15) return false;
                out.isNumber = false;
                out.color = Color::OfIndex(idx);
                return true;
            }

            // 数字（系数）
            if (std::isdigit(static_cast<unsigned char>(s[p])) || s[p] == '.') {
                const auto begin = p;
                bool dot = false;
                while (p < s.size() && (std::isdigit(static_cast<unsigned char>(s[p])) || s[p] == '.')) {
                    if (s[p] == '.') {
                        if (dot) return false;
                        dot = true;
                    }
                    ++p;
                }
                const std::string num(s.substr(begin, p - begin));
                if (num.empty() || num == ".") return false;
                out.isNumber = true;
                out.number = std::atof(num.c_str());
                return true;
            }

            // 具名颜色
            if (std::isalpha(static_cast<unsigned char>(s[p]))) {
                const auto begin = p;
                while (p < s.size() &&
                    (std::isalnum(static_cast<unsigned char>(s[p])) || s[p] == '-' || s[p] == '_')) ++p;
                const std::string name(s.substr(begin, p - begin));
                if (!IsKnownColorName(name)) return false;
                out.isNumber = false;
                out.color = Color::Parse(name);
                return true;
            }

            return false;
        }

        // ---- 控制符 ----
        // 返回 false 表示不是合法控制符，调用方按普通文字输出。
        bool TryToken(const std::string& raw, Color& newColor, bool& reset, bool& startRainbow) const {
            const std::string token = Trim(raw);
            if (token.empty()) return false;
            reset = false;
            startRainbow = false;

            if (token == "/r") { reset = true; return true; }
            if (token == "/c") { startRainbow = true; return true; }
            // 兼容简写；不合法颜色名不会走到这里
            if (token == "r") { reset = true; return true; }
            if (token == "c") { startRainbow = true; return true; }

            Color parsed;
            if (!ParseExpression(token, parsed)) return false;
            newColor = parsed;
            return true;
        }

        void FlushLiteral() {
            if (literal.empty()) return;
            if (!rainbow) {
                out.Append(literal, StyleFor(current));
            }
            else {
                // 彩虹按 Unicode 码点推进，CJK 仍然是一个字符一个颜色
                for (const auto cp : Utf8::Decode(literal)) {
                    out.AppendCodepoint(cp, StyleFor(RainbowAt(hue)));
                    hue += fmt.rainbowStep;
                }
            }
            literal.clear();
        }

        [[nodiscard]] TStr Run() {
            std::size_t p = 0;
            while (p < src.size()) {
                const char c = src[p];
                if (c == '\\') {
                    if (p + 1 < src.size() && (src[p + 1] == '{' || src[p + 1] == '}' || src[p + 1] == '\\')) {
                        literal.push_back(src[p + 1]);
                        p += 2;
                    }
                    else {
                        literal.push_back('\\');
                        ++p;
                    }
                    continue;
                }
                if (c != '{') {
                    literal.push_back(c);
                    ++p;
                    continue;
                }

                const auto close = src.find('}', p + 1);
                if (close == std::string_view::npos) {
                    literal.push_back('{');
                    ++p;
                    continue;
                }

                Color newColor;
                bool reset = false;
                bool startRainbow = false;
                if (!TryToken(std::string(src.substr(p + 1, close - p - 1)), newColor, reset, startRainbow)) {
                    // 非法 token：保留 `{`，后续内容继续按文字解析
                    literal.push_back('{');
                    ++p;
                    continue;
                }

                FlushLiteral();
                if (reset) {
                    current = fmt.baseStyle.fg;
                    rainbow = false;
                }
                else if (startRainbow) {
                    rainbow = true;
                    hue = 0.0;
                }
                else {
                    current = newColor;
                    rainbow = false;
                }
                p = close + 1;
            }
            FlushLiteral();
            return std::move(out);
        }
    };

    inline TStr TStrFmt::Parse(const std::string_view text) const {
        Parser parser(text, *this);
        return parser.Run();
    }

    //------------------------------------------------------------------
    // TStr 写入实现（需要 ConsoleBackBuffer，放在文件后半段避免循环依赖）
    //------------------------------------------------------------------
    inline int TStr::WriteTo(ConsoleBackBuffer& buf, const int x, const int y) const {
        int cx = x;
        int cy = y;
        const int left = x;
        for (const auto& run : m_runs) { 
            std::size_t start = 0;
            while (start < run.text.size()) {
                const auto nl = run.text.find('\n', start);
                const auto end = (nl == std::string::npos) ? run.text.size() : nl;
                const std::string segment = run.text.substr(start, end - start);
                if (!segment.empty()) {
                    buf.WriteText(cx, cy, segment, run.style);
                    cx += Utf8::DisplayWidth(segment);
                }
                if (nl == std::string::npos) break;
                cx = left;
                ++cy;
                start = nl + 1;
            }
        }
        return cy;
    }

    inline int TStr::WriteTo(ConsoleBackBuffer& buf, const int x, const int y,
        const std::function<void(int&, int&)>& advance) const {
        int cx = x;
        int cy = y;
        const int left = x;
        for (const auto& run : m_runs) {
            std::size_t start = 0;
            while (start < run.text.size()) {
                const auto nl = run.text.find('\n', start);
                const auto end = (nl == std::string::npos) ? run.text.size() : nl;
                const std::string segment = run.text.substr(start, end - start);
                if (!segment.empty()) {
                    buf.WriteText(cx, cy, segment, run.style);
                    cx += Utf8::DisplayWidth(segment);
                    if (advance) advance(cx, cy);
                }
                if (nl == std::string::npos) break;
                cx = left;
                ++cy;
                start = nl + 1;
            }
        }
        return cy;
    }

} // namespace Terminal
