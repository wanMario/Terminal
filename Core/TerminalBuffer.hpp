//
// Created by Administrator on 2026/7/14.
// 修订：补齐 ConsoleBackBuffer —— 单元格双缓冲、裁剪栈、脏区刷新、
//        UTF-8 写入、框线绘制、ANSI 快照导出。
//

#pragma once

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <utility>
#include <windows.h>

#include "../../Math/Geometry/G2D/Rect.hpp"
#include "../../Math/Geometry/G2D/Point.hpp"
#include "../Utils/Outputs.hpp"
#include "../Utils/Color.hpp"
#include "../Utils/Utf8.hpp"
#include "../Utils/BoxChars.hpp"

namespace Terminal {
    using WMath::GG2D::Rect2Di;

    /**
     * @brief 控制台后缓冲区。
     *        所有元件都往这里画（坐标 = 控制台字符坐标），Flush 时只把脏区
     *        通过 WriteConsoleOutputW 一次性刷到屏幕，避免整屏重绘闪烁。
     */
    class ConsoleBackBuffer {
    public:
        //------------------------------------------------------------------
        ConsoleBackBuffer() = default;
        ConsoleBackBuffer(const int w, const int h) { Resize(w, h); }

        static ConsoleBackBuffer FromConsole(const HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE)) {
            ConsoleBackBuffer buf;
            CONSOLE_SCREEN_BUFFER_INFO csbi{};
            if (GetConsoleScreenBufferInfo(hOut, &csbi)) {
                const int w = csbi.srWindow.Right - csbi.srWindow.Left + 1;
                const int h = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
                buf.Resize(std::max(w, 1), std::max(h, 1));
            } else {
                buf.Resize(80, 25);
            }
            return buf;
        }

        // 终端当前尺寸（不重建缓冲）
        static bool QueryConsoleSize(int& w, int& h, const HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE)) {
            CONSOLE_SCREEN_BUFFER_INFO csbi{};
            if (!GetConsoleScreenBufferInfo(hOut, &csbi)) return false;
            w = csbi.srWindow.Right - csbi.srWindow.Left + 1;
            h = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
            return true;
        }

        //------------------------------------------------------------------
        [[nodiscard]] int Width() const noexcept { return m_width; }
        [[nodiscard]] int Height() const noexcept { return m_height; }
        [[nodiscard]] Rect2Di Bounds() const { return Rect2Di{ 0, 0, m_width, m_height }; }
        [[nodiscard]] bool IsEmpty() const noexcept { return m_width <= 0 || m_height <= 0; }

        void Resize(const int w, const int h) {
            m_width = std::max(w, 0);
            m_height = std::max(h, 0);
            const size_t n = static_cast<size_t>(m_width) * m_height;
            m_data.assign(n, MakeCell(L' ', Style{}.ToAttr()));
            // 24 位真彩：逐格记录精确 RGB（写控制台用 16 色属性，ANSI 导出用 RGB）
            m_fgRgb.assign(n, Style{}.fg);
            m_bgRgb.assign(n, Style{}.bg);
            m_clips.clear();
            m_fullDirty = true;
            m_dirtyValid = false;
        }

        // 24 位真彩开关：开启后 ToAnsiText / FlushAnsi 输出 38;2;r;g;b
        void SetTrueColor(const bool on) noexcept { m_trueColor = on; }
        [[nodiscard]] bool TrueColor() const noexcept { return m_trueColor; }

        //------------------------------------------------------------------
        // 裁剪栈
        void PushClip(const Rect2Di& r) {
            Rect2Di clip = r;
            if (!m_clips.empty()) {
                const auto info = Rect2Di::RectInfo::Intersect(m_clips.back(), clip);
                clip = info.valid ? info.result : Rect2Di{};
            }
            m_clips.push_back(clip);
        }

        void PopClip() {
            if (!m_clips.empty()) m_clips.pop_back();
        }

        [[nodiscard]] Rect2Di CurrentClip() const {
            return m_clips.empty() ? Bounds() : m_clips.back();
        }

        [[nodiscard]] bool InClip(const int x, const int y) const {
            if (x < 0 || y < 0 || x >= m_width || y >= m_height) return false;
            if (m_clips.empty()) return true;
            return m_clips.back().Contains(WMath::GG2D::Point2Di{ x, y });
        }

        //------------------------------------------------------------------
        // 基本写入
        void SetCell(const int x, const int y, const wchar_t ch, const DWORD attr) {
            const Style st = Style::FromAttr(attr);
            WriteCell(x, y, ch, attr, st.fg, st.bg);
        }

        void SetCell(const int x, const int y, const char ch, const DWORD attr) {
            SetCell(x, y, static_cast<wchar_t>(ch), attr);
        }

        void SetCell(const int x, const int y, const wchar_t ch, const Style& style) {
            // 精确 RGB 直接写入；reverse 先对调前景/背景
            const Color fg = style.reverse ? style.bg : style.fg;
            const Color bg = style.reverse ? style.fg : style.bg;
            WriteCell(x, y, ch, style.ToAttr(), fg, bg);
        }

        [[nodiscard]] wchar_t GetChar(const int x, const int y) const {
            if (x < 0 || y < 0 || x >= m_width || y >= m_height) return L' ';
            return m_data[Index(x, y)].Char.UnicodeChar;
        }

        [[nodiscard]] DWORD GetAttr(const int x, const int y) const {
            if (x < 0 || y < 0 || x >= m_width || y >= m_height) return Style{}.ToAttr();
            return m_data[Index(x, y)].Attributes;
        }

        // 只换字符、保留原有属性与精确 RGB（排版守卫用）
        bool ReplaceChar(const int x, const int y, const wchar_t ch) {
            if (x < 0 || y < 0 || x >= m_width || y >= m_height) return false;
            const size_t i = Index(x, y);
            if (m_data[i].Char.UnicodeChar == ch) return false;
            m_data[i].Char.UnicodeChar = ch;
            MarkCell(x, y);
            return true;
        }

        // 控件绘制出的内容按原样写入缓冲；兼容模式只在渲染入口按 ANSI 能力统一切换，
        // 不再逐帧扫描宽字符改写画布（避免把正常 CJK/边框误判成错乱）。

        //------------------------------------------------------------------
        // 区域填充
        void Clear(const DWORD attr = 0) { Fill(Bounds(), L' ', attr); }

        void Clear(const Style& style) { Fill(Bounds(), L' ', style.ToAttr()); }

        void Fill(const Rect2Di& rect, const wchar_t ch, const DWORD attr) {
            const Rect2Di clip = CurrentClip();
            const auto info = Rect2Di::RectInfo::Intersect(clip, rect);
            if (!info.valid) return;
            const Rect2Di r = info.result;
            const Style st = Style::FromAttr(attr);
            FillRaw(r, ch, attr, st.fg, st.bg);
        }

        void Fill(const Rect2Di& rect, const wchar_t ch, const Style& style) {
            const Rect2Di clip = CurrentClip();
            const auto info = Rect2Di::RectInfo::Intersect(clip, rect);
            if (!info.valid) return;
            const Color fg = style.reverse ? style.bg : style.fg;
            const Color bg = style.reverse ? style.fg : style.bg;
            FillRaw(info.result, ch, style.ToAttr(), fg, bg);
        }

        void FillStyle(const Rect2Di& rect, const Style& style) { Fill(rect, L' ', style); }

        void FillGradient(const Rect2Di& rect, const Color& from, const Color& to, const wchar_t ch = L' ') {
            const int n = std::max(rect.width(), 1);
            for (int y = rect.Top(); y < rect.Bottom(); ++y) {
                for (int x = rect.Left(); x < rect.Right(); ++x) {
                    const double t = static_cast<double>(x - rect.Left()) / std::max(n - 1, 1);
                    const Color c = Color::Blend(from, to, t);
                    SetCell(x, y, ch, Style(c, c).WithBold());
                }
            }
        }

        //------------------------------------------------------------------
        // 文本写入（UTF-8，自动按显示宽度排布，宽字符占两格）
        int WriteText(const int x, const int y, const std::string& text, const DWORD attr,
                      const int maxWidth = INT_MAX) {
            int cx = x;
            const int right = (maxWidth == INT_MAX) ? (m_width + 1) : (x + std::max(0, maxWidth));
            for (const auto cp : Utf8::Decode(text)) {
                if (cp == '\n') break;
                const int w = Utf8::CharWidth(cp);
                if (w == 0) continue;
                if (cx + w > right) break;
                if (cp < 0x10000) {
                    SetCell(cx, y, static_cast<wchar_t>(cp), attr);
                    if (w == 2) SetCell(cx + 1, y, L' ', attr);
                }
                cx += w;
            }
            return cx - x;
        }

        int WriteText(const int x, const int y, const std::string& text, const Style& style,
                      const int maxWidth = INT_MAX) {
            int cx = x;
            const int right = (maxWidth == INT_MAX) ? (m_width + 1) : (x + std::max(0, maxWidth));
            for (const auto cp : Utf8::Decode(text)) {
                if (cp == '\n') break;
                const int w = Utf8::CharWidth(cp);
                if (w == 0) continue;
                if (cx + w > right) break;
                if (cp < 0x10000) {
                    SetCell(cx, y, static_cast<wchar_t>(cp), style);
                    if (w == 2) SetCell(cx + 1, y, L' ', style);
                }
                cx += w;
            }
            return cx - x;
        }

        //------------------------------------------------------------------
        // 直线与框线
        void HLine(const int x, const int y, const int len, const wchar_t ch, const DWORD attr) {
            for (int i = 0; i < len; ++i) SetCell(x + i, y, ch, attr);
        }

        void VLine(const int x, const int y, const int len, const wchar_t ch, const DWORD attr) {
            for (int i = 0; i < len; ++i) SetCell(x, y + i, ch, attr);
        }

        void DrawBox(const Rect2Di& rect, const BoxChars& chars, const DWORD attr) {
            if (rect.width() < 2 || rect.height() < 2) return;
            const BoxChars& glyphs = Box::Effective(chars);
            const int l = rect.Left(), t = rect.Top(), r = rect.Right() - 1, b = rect.Bottom() - 1;
            for (int x = l + 1; x < r; ++x) {
                SetCell(x, t, glyphs.horizontal, attr);
                SetCell(x, b, glyphs.horizontal, attr);
            }
            for (int y = t + 1; y < b; ++y) {
                SetCell(l, y, glyphs.vertical, attr);
                SetCell(r, y, glyphs.vertical, attr);
            }
            SetCell(l, t, glyphs.topLeft, attr);
            SetCell(r, t, glyphs.topRight, attr);
            SetCell(l, b, glyphs.bottomLeft, attr);
            SetCell(r, b, glyphs.bottomRight, attr);
        }

        void DrawBox(const Rect2Di& rect, const BoxChars& chars, const Style& style) {
            if (rect.width() < 2 || rect.height() < 2) return;
            const BoxChars& glyphs = Box::Effective(chars);
            const int l = rect.Left(), t = rect.Top(), r = rect.Right() - 1, b = rect.Bottom() - 1;
            for (int x = l + 1; x < r; ++x) {
                SetCell(x, t, glyphs.horizontal, style);
                SetCell(x, b, glyphs.horizontal, style);
            }
            for (int y = t + 1; y < b; ++y) {
                SetCell(l, y, glyphs.vertical, style);
                SetCell(r, y, glyphs.vertical, style);
            }
            SetCell(l, t, glyphs.topLeft, style);
            SetCell(r, t, glyphs.topRight, style);
            SetCell(l, b, glyphs.bottomLeft, style);
            SetCell(r, b, glyphs.bottomRight, style);
        }

        //------------------------------------------------------------------
        // 刷新
        void MarkDirty(const Rect2Di& rect) { MarkRect(rect); }

        void MarkAllDirty() {
            m_fullDirty = true;
            m_dirtyValid = false;
        }

        [[nodiscard]] bool HasDirty() const noexcept { return m_fullDirty || m_dirtyValid; }
        [[nodiscard]] Rect2Di DirtyRect() const { return m_fullDirty ? Bounds() : (m_dirtyValid ? m_dirty : Rect2Di{}); }

        //--------------------------------------------------------------
        // 终端真彩能力探测：Windows Terminal / VS Code 终端直接认定；
        // 老 conhost 则尝试打开 ENABLE_VIRTUAL_TERMINAL_PROCESSING。
        //--------------------------------------------------------------
        static bool SupportsTrueColor(const HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE)) {
            if (const char* wt = std::getenv("WT_SESSION"); wt && *wt) return true;
            if (const char* tp = std::getenv("TERM_PROGRAM"); tp && *tp) return true;
            if (hOut == nullptr) return false;
            DWORD mode = 0;
            if (!GetConsoleMode(hOut, &mode)) return false;   // 被重定向到文件/管道
            if ((mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0) return true;
            return EnableVirtualTerminal(hOut);              // 尝试打开 VT
        }

        // 自动选择刷新通道：真彩 + 终端支持 → ANSI，否则 16 色属性
        void FlushAuto(const HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE)) {
            if (m_trueColor && SupportsTrueColor(hOut)) FlushAnsi(hOut);
            else Flush(hOut);
        }

        void Flush(const HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE)) {
            if (IsEmpty() || hOut == nullptr) return;

            Rect2Di region = Bounds();
            if (!m_fullDirty) {
                if (!m_dirtyValid) return;
                region = m_dirty;
            }

            const int w = region.width();
            const int h = region.height();
            if (w <= 0 || h <= 0) return;

            SMALL_RECT win{ static_cast<SHORT>(region.Left()), static_cast<SHORT>(region.Top()),
                            static_cast<SHORT>(region.Right() - 1), static_cast<SHORT>(region.Bottom() - 1) };
            const COORD size{ static_cast<SHORT>(w), static_cast<SHORT>(h) };
            const COORD src{ 0, 0 };
            // 源缓冲左上角对应目标区左上角，所以传偏移后的指针
            const CHAR_INFO* data = m_data.data() + static_cast<size_t>(region.Top()) * m_width + region.Left();
            WriteConsoleOutputW(hOut, data, size, src, &win);

            m_fullDirty = false;
            m_dirtyValid = false;
        }

        //------------------------------------------------------------------
        // 24 位真彩刷新：走 ANSI（需要虚拟终端支持）
        // 只重绘脏区内的行，避免整屏重写造成的闪烁与多余输出。
        void FlushAnsi(const HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE)) {
            if (IsEmpty() || hOut == nullptr) return;
            EnableVirtualTerminal(hOut);

            Rect2Di region = m_fullDirty ? Bounds() : (m_dirtyValid ? m_dirty : Rect2Di{});
            if (region.IsEmpty()) return;

            std::string out;
            out.reserve(static_cast<size_t>(region.width()) * 12 + 32);
            for (int y = region.Top(); y < region.Bottom(); ++y) {
                // 定位到行首，再只输出该行脏区
                out += "\x1b[" + std::to_string(y + 1) + ";" +
                       std::to_string(region.Left() + 1) + "H";
                out += AnsiRow(y, region.Left(), region.Right());
                out += "\x1b[0m";
            }

            DWORD written = 0;
            WriteConsoleA(hOut, out.data(), static_cast<DWORD>(out.size()), &written, nullptr);
            m_fullDirty = false;
            m_dirtyValid = false;
        }

        //------------------------------------------------------------------
        // 调试/导出：整屏转 ANSI 文本（便于在普通终端或日志里检查配色）
        [[nodiscard]] std::string ToAnsiText() const {
            std::string out;
            out.reserve(static_cast<size_t>(m_width) * m_height * 8);
            for (int y = 0; y < m_height; ++y) {
                out += AnsiRow(y, 0, m_width);
                out += "\x1b[0m\n";
            }
            return out;
        }

        [[nodiscard]] const CHAR_INFO* Data() const noexcept { return m_data.data(); }

    private:
        [[nodiscard]] bool UseTrueColor() const noexcept {
            return m_trueColor && m_fgRgb.size() == m_data.size();
        }

        // 单格的 SGR 前缀
        [[nodiscard]] std::string SgrForCell(const int x, const int y) const {
            static const char* fgCodes[16] = {
                "30","34","32","36","31","35","33","37",
                "90","94","92","96","91","95","93","97"
            };
            static const char* bgCodes[16] = {
                "40","44","42","46","41","45","43","47",
                "100","104","102","106","101","105","103","107"
            };
            const size_t i = Index(x, y);
            const DWORD attr = m_data[i].Attributes;

            std::string sgr = "\x1b[0;";
            if (attr & FOREGROUND_INTENSITY) sgr += "1;";
            if (attr & COMMON_LVB_UNDERSCORE) sgr += "4;";
            if (UseTrueColor()) {
                const Color& fg = m_fgRgb[i];
                const Color& bg = m_bgRgb[i];
                sgr += "38;2;" + std::to_string(fg.r) + ";" + std::to_string(fg.g) + ";" +
                       std::to_string(fg.b) + ";48;2;" + std::to_string(bg.r) + ";" +
                       std::to_string(bg.g) + ";" + std::to_string(bg.b);
            } else {
                sgr += fgCodes[Detail::IndexOfAttrBits(attr & 0x0F)];
                sgr += ";";
                sgr += bgCodes[Detail::IndexOfAttrBits((attr & 0xF0) >> 4)];
            }
            sgr += "m";
            return sgr;
        }

        // 一行 [x0,x1) 的 ANSI 文本（按需切换 SGR，末段直接拼 UTF-8）
        [[nodiscard]] std::string AnsiRow(const int y, const int x0, const int x1) const {
            auto flushSegment = [](std::string& out, std::wstring& seg) {
                if (seg.empty()) return;
                const int need = WideCharToMultiByte(CP_UTF8, 0, seg.c_str(), static_cast<int>(seg.size()),
                                                     nullptr, 0, nullptr, nullptr);
                if (need > 0) {
                    std::string bytes(static_cast<size_t>(need), '\0');
                    WideCharToMultiByte(CP_UTF8, 0, seg.c_str(), static_cast<int>(seg.size()),
                                        bytes.data(), need, nullptr, nullptr);
                    out += bytes;
                }
                seg.clear();
            };

            std::string out;
            std::wstring seg;
            DWORD lastAttr = 0xFFFFFFFFu;
            Color lastFg, lastBg;
            bool haveLast = false;
            bool skipCell = false;   // 宽字符占两格：第二格（占位空格）不输出，交给终端自然占位

            for (int x = x0; x < x1; ++x) {
                if (skipCell) { skipCell = false; continue; }
                const size_t i = Index(x, y);
                const DWORD attr = m_data[i].Attributes;
                const Color& fg = m_fgRgb[i];
                const Color& bg = m_bgRgb[i];
                // 只在样式真正变化时才重算/输出 SGR（同一行连续同色时零开销）
                if (!haveLast || attr != lastAttr || !fg.Equals(lastFg) || !bg.Equals(lastBg)) {
                    flushSegment(out, seg);
                    lastAttr = attr;
                    lastFg = fg;
                    lastBg = bg;
                    haveLast = true;
                    out += SgrText(attr, fg, bg);
                }
                const wchar_t wc = m_data[i].Char.UnicodeChar;
                const wchar_t shown = (wc == 0) ? L' ' : wc;
                seg.push_back(shown);
                if (Utf8::CharWidth(shown) == 2 && x + 1 < x1) skipCell = true;
            }
            flushSegment(out, seg);
            return out;
        }

        // 直接按 attr + RGB 生成 SGR，避免每格都走一遍 SgrForCell 的字符串拼装
        [[nodiscard]] std::string SgrText(const DWORD attr, const Color& fg, const Color& bg) const {
            static const char* fgCodes[16] = {
                "30","34","32","36","31","35","33","37",
                "90","94","92","96","91","95","93","97"
            };
            static const char* bgCodes[16] = {
                "40","44","42","46","41","45","43","47",
                "100","104","102","106","101","105","103","107"
            };
            std::string sgr = "\x1b[0;";
            if (attr & FOREGROUND_INTENSITY) sgr += "1;";
            if (attr & COMMON_LVB_UNDERSCORE) sgr += "4;";
            if (UseTrueColor()) {
                sgr += "38;2;";
                sgr += std::to_string(fg.r);
                sgr += ";";
                sgr += std::to_string(fg.g);
                sgr += ";";
                sgr += std::to_string(fg.b);
                sgr += ";48;2;";
                sgr += std::to_string(bg.r);
                sgr += ";";
                sgr += std::to_string(bg.g);
                sgr += ";";
                sgr += std::to_string(bg.b);
            } else {
                sgr += fgCodes[Detail::IndexOfAttrBits(attr & 0x0F)];
                sgr += ";";
                sgr += bgCodes[Detail::IndexOfAttrBits((attr & 0xF0) >> 4)];
            }
            sgr += "m";
            return sgr;
        }

        [[nodiscard]] size_t Index(const int x, const int y) const {
            return static_cast<size_t>(y) * m_width + x;
        }

        static CHAR_INFO MakeCell(const wchar_t ch, const DWORD attr) {
            CHAR_INFO c{};
            c.Char.UnicodeChar = ch;
            c.Attributes = static_cast<WORD>(attr);
            return c;
        }

        void MarkCell(const int x, const int y) {
            if (m_fullDirty) return;
            if (!m_dirtyValid) {
                m_dirty = Rect2Di{ x, y, 1, 1 };
                m_dirtyValid = true;
                return;
            }
            if (x < m_dirty.Left())   m_dirty.SetLeft(x);
            if (x >= m_dirty.Right()) m_dirty.SetRight(x + 1);
            if (y < m_dirty.Top())    m_dirty.SetTop(y);
            if (y >= m_dirty.Bottom()) m_dirty.SetBottom(y + 1);
        }

        void MarkRect(const Rect2Di& rect) {
            if (m_fullDirty) return;
            if (rect.IsEmpty()) return;
            if (!m_dirtyValid) {
                m_dirty = rect;
                m_dirtyValid = true;
                return;
            }
            const auto info = Rect2Di::RectInfo::Merge(m_dirty, rect);
            if (info.valid) m_dirty = info.result;
        }

        int m_width{ 0 };
        int m_height{ 0 };
        std::vector<CHAR_INFO> m_data{};
        std::vector<Color> m_fgRgb{};
        std::vector<Color> m_bgRgb{};
        bool m_trueColor{ false };
        std::vector<Rect2Di> m_clips{};
        Rect2Di m_dirty{};
        bool m_dirtyValid{ false };
        bool m_fullDirty{ true };

        void StoreColor(const int x, const int y, const Style& st) {
            StoreColor(x, y, st.reverse ? st.bg : st.fg, st.reverse ? st.fg : st.bg);
        }

        void StoreColor(const int x, const int y, const Color& fg, const Color& bg) {
            const size_t i = Index(x, y);
            if (i >= m_fgRgb.size()) return;
            m_fgRgb[i] = fg;
            m_bgRgb[i] = bg;
        }

        // 统一写入口：字符 + 16 色属性 + 精确 RGB（内容真正变化才标脏）
        void WriteCell(const int x, const int y, const wchar_t ch, const DWORD attr,
                       const Color& fg, const Color& bg) {
            if (!InClip(x, y)) return;
            const size_t i = Index(x, y);
            CHAR_INFO& cell = m_data[i];
            const bool charChanged = cell.Char.UnicodeChar != ch || cell.Attributes != attr;
            const bool colorChanged = !m_fgRgb[i].Equals(fg) || !m_bgRgb[i].Equals(bg);
            if (charChanged) cell = MakeCell(ch, attr);
            if (colorChanged) { m_fgRgb[i] = fg; m_bgRgb[i] = bg; }
            if (charChanged || colorChanged) MarkCell(x, y);
        }

        // 区域填充（同样做逐格比较，未变化不标脏）
        void FillRaw(const Rect2Di& r, const wchar_t ch, const DWORD attr,
                     const Color& fg, const Color& bg) {
            bool any = false;
            for (int y = r.Top(); y < r.Bottom(); ++y) {
                for (int x = r.Left(); x < r.Right(); ++x) {
                    const size_t i = Index(x, y);
                    CHAR_INFO& cell = m_data[i];
                    if (cell.Char.UnicodeChar != ch || cell.Attributes != attr) {
                        cell = MakeCell(ch, attr);
                        any = true;
                    }
                    if (!m_fgRgb[i].Equals(fg) || !m_bgRgb[i].Equals(bg)) {
                        m_fgRgb[i] = fg;
                        m_bgRgb[i] = bg;
                        any = true;
                    }
                }
            }
            if (any) MarkRect(r);
        }
    };
}
