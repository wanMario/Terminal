//
// Terminal / Utils / Utf8.hpp
// UTF-8 文本度量与排版工具：宽字符(wcwidth 近似)、截断、填充、对齐
//

#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>
#include <windows.h>

namespace Terminal {
namespace Utf8 {

    //--------------------------------------------------------
    // 解码：UTF-8 -> 码点序列
    //--------------------------------------------------------
    inline std::vector<std::uint32_t> Decode(const std::string& s) {
        std::vector<std::uint32_t> out;
        out.reserve(s.size());
        size_t i = 0;
        while (i < s.size()) {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            if (c < 0x80) {
                out.push_back(c);
                ++i;
                continue;
            }
            std::uint32_t cp = 0;
            size_t need = 0;
            if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; need = 1; }
            else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; need = 2; }
            else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; need = 3; }
            else { ++i; continue; } // 非法起始字节，跳过

            if (i + need >= s.size()) break; // 序列不完整，丢弃尾部的半截字节
            bool ok = true;
            for (size_t k = 1; k <= need; ++k) {
                const unsigned char cc = static_cast<unsigned char>(s[i + k]);
                if ((cc & 0xC0) != 0x80) { ok = false; break; }
                cp = (cp << 6) | (cc & 0x3F);
            }
            if (!ok) { ++i; continue; }
            out.push_back(cp);
            i += need + 1;
        }
        return out;
    }

    // 码点 -> UTF-8 字节串
    inline std::string Encode(const std::uint32_t cp) {
        std::string s;
        if (cp < 0x80) {
            s.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            s.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            s.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            s.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            s.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        return s;
    }

    //--------------------------------------------------------
    // 显示宽度：全角/宽字符占 2 列，其余占 1 列
    //--------------------------------------------------------
    inline int CharWidth(const std::uint32_t cp) {
        if (cp == 0) return 0;
        // 组合符（零宽）
        if ((cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x1AB0 && cp <= 0x1AFF)
            || (cp >= 0x1DC0 && cp <= 0x1DFF) || (cp >= 0x20D0 && cp <= 0x20FF)
            || (cp >= 0xFE00 && cp <= 0xFE0F) || (cp >= 0xFE20 && cp <= 0xFE2F))
            return 0;

        // East Asian Wide / Fullwidth 近似区间
        if ((cp >= 0x1100 && cp <= 0x115F)   // Hangul Jamo
            || cp == 0x2329 || cp == 0x232A
            || (cp >= 0x2E80 && cp <= 0x303E) // CJK Radicals .. CJK Symbols
            || (cp >= 0x3041 && cp <= 0x33FF) // Hiragana .. CJK Compatibility
            || (cp >= 0x3400 && cp <= 0x4DBF) // CJK Ext A
            || (cp >= 0x4E00 && cp <= 0x9FFF) // CJK Unified
            || (cp >= 0xA000 && cp <= 0xA4CF) // Yi
            || (cp >= 0xA960 && cp <= 0xA97F)
            || (cp >= 0xAC00 && cp <= 0xD7A3) // Hangul Syllables
            || (cp >= 0xF900 && cp <= 0xFAFF) // CJK Compatibility Ideographs
            || (cp >= 0xFE10 && cp <= 0xFE19)
            || (cp >= 0xFE30 && cp <= 0xFE6F)
            || (cp >= 0xFF00 && cp <= 0xFF60) // Fullwidth Forms
            || (cp >= 0xFFE0 && cp <= 0xFFE6)
            || (cp >= 0x1F300 && cp <= 0x1F64F) // Emoji
            || (cp >= 0x1F900 && cp <= 0x1F9FF)
            || (cp >= 0x20000 && cp <= 0x3FFFD))
            return 2;
        return 1;
    }

    // 整串显示宽度
    inline int DisplayWidth(const std::string& s) {
        int w = 0;
        for (const auto cp : Decode(s)) w += CharWidth(cp);
        return w;
    }

    // 显示宽度转 UTF-16 宽字符串（供 WriteConsoleOutputW 使用）
    inline std::wstring ToWide(const std::string& s) {
        std::wstring out;
        for (const auto cp : Decode(s)) {
            if (cp < 0x10000) {
                out.push_back(static_cast<wchar_t>(cp));
            } else {
                const std::uint32_t v = cp - 0x10000;
                out.push_back(static_cast<wchar_t>(0xD800 + (v >> 10)));
                out.push_back(static_cast<wchar_t>(0xDC00 + (v & 0x3FF)));
            }
        }
        return out;
    }

    //--------------------------------------------------------
    // 排版
    //--------------------------------------------------------
    // 按显示宽度截断，超出部分用 "…" 代替
    inline std::string Truncate(const std::string& s, int width, const std::string& tail = "…") {
        if (width <= 0) return {};
        if (DisplayWidth(s) <= width) return s;
        const int tailWidth = DisplayWidth(tail);
        std::string out;
        int used = 0;
        for (const auto cp : Decode(s)) {
            const int w = CharWidth(cp);
            if (used + w > width - tailWidth) break;
            out += Encode(cp);
            used += w;
        }
        if (tailWidth <= width) out += tail;
        return out;
    }

    // 右侧补空格到指定显示宽度
    inline std::string PadRight(const std::string& s, int width) {
        const int w = DisplayWidth(s);
        if (w >= width) return s;
        return s + std::string(static_cast<size_t>(width - w), ' ');
    }

    // 居中/右对齐（"left" | "center" | "right"）
    inline std::string Align(const std::string& s, int width, const int mode) {
        if (width <= 0) return {};
        const int w = DisplayWidth(s);
        if (w > width) return Truncate(s, width);
        if (mode == 1) { // center
            const int left = (width - w) / 2;
            return std::string(static_cast<size_t>(left), ' ') + s
                 + std::string(static_cast<size_t>(width - w - left), ' ');
        }
        if (mode == 2) { // right
            return std::string(static_cast<size_t>(width - w), ' ') + s;
        }
        return s + std::string(static_cast<size_t>(width - w), ' ');
    }

    // 重复字符串 n 次
    inline std::string Repeat(const std::string& unit, const int times) {
        std::string out;
        if (times <= 0 || unit.empty()) return out;
        out.reserve(unit.size() * static_cast<size_t>(times));
        for (int i = 0; i < times; ++i) out += unit;
        return out;
    }

    // 是否纯 ASCII
    inline bool IsAscii(const std::string& s) {
        for (const char c : s) if (static_cast<unsigned char>(c) >= 0x80) return false;
        return true;
    }

} // namespace Utf8
} // namespace Terminal
