//
// Created by Administrator on 2026/4/20.
// 修订：保留旧的单行输入工具 InputV1（放到 Terminal::Legacy），
//        新版 TextInput 支持：标签 + 输入框、插入/替换模式、选区、
//        剪贴板（Ctrl+C/V/X）、撤销重做（Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y）、
//        多行编辑（上下左右移动，行数超过 maxLines 时内部滚动）。
//

#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <windows.h>

#include "../Utils/Outputs.hpp"
#include "../Utils/Utf8.hpp"
#include "../Utils/BoxChars.hpp"
#include "../Core/UI.hpp"
#include "../Core/Input.hpp"
#include "../Core/TerminalBuffer.hpp"
#include "../Core/TerminalMessage.hpp"
#include "../Core/TerminalMouse.hpp"

namespace Terminal {

    //======================================================================
    // 旧版：直接往控制台写字的单行输入工具（不参与布局，保留兼容）
    //======================================================================
    namespace Legacy {
        class InputV1 {
        public:
            int x{}, y{};
            std::string text{};
            int cursorPos{ 0 };
            int endPos{ 0 };
            int MaxLength{ 1 << 16 };
            bool Focusable{};

            enum cursorMode {
                CInsert,
                CSelection,
                CReplace,
            } cmode{ CInsert };

            struct TerminalInputStyle {
                DWORD normal;
                DWORD cursor;
                DWORD cursor2;
            } style{
                TextStyle::NowTextStyle,
                TextStyle::ChangeStyleText(TextStyle::ReverseStyle(TextStyle::NowTextStyle), TextStyle::TextGray),
                TextStyle::ChangeStyleText(TextStyle::NowTextStyle, TextStyle::TextGray)
            };

        private:
            long clc{ 0 };
            int changed{ 0 };
            bool gotsc{ false };

            [[nodiscard]] DWORD GetCursorColor() const {
                if (cmode == CInsert) {
                    return clock() - clc < 500 ? style.cursor : style.cursor2;
                }
                return style.normal;
            }

            bool Write(const char c) {
                if (cursorPos + 1 >= MaxLength) return false;
                text = text.substr(0, cursorPos) + c + text.substr(cursorPos);
                cursorPos++;
                changed = 1;
                return true;
            }

            bool Backspace() {
                if (cursorPos == 0) return false;
                if (cursorPos-- == static_cast<int>(text.size())) {
                    text = text.substr(0, text.size() - 1);
                } else {
                    text = text.substr(0, cursorPos) + text.substr(cursorPos + 1);
                }
                changed = 1;
                return true;
            }

            bool FastBackspace() {
                if (cursorPos == 0) return false;
                int cnt = 0;
                while (cursorPos != 0 && text[cursorPos - 1] == ' ') { cursorPos--; cnt++; }
                while (cursorPos != 0 && text[cursorPos - 1] != ' ') { cursorPos--; cnt++; }

                text = text.substr(0, cursorPos) + text.substr(cursorPos + cnt);
                changed = cnt;
                return true;
            }

            bool CursorMove(const int mv) {
                if (mv < 0 && cursorPos + mv < 0) return false;
                if (mv > 0 && cursorPos + mv > static_cast<int>(text.size())) return false;
                cursorPos += mv;
                changed = 1;
                return true;
            }

        public:
            InputV1() { clc = clock(); }
            ~InputV1() = default;

            int update(const int c) {
                if (cmode == CInsert) {
                    if (gotsc) {
                        if (c == 'K') return CursorMove(-1);
                        if (c == 'M') return CursorMove(1);
                        gotsc = false;
                    }
                    if (c == 12) return 3;
                    if (c == 8) return Backspace();
                    if (c == 127) return FastBackspace();
                    if (c == 0) {
                        gotsc = true;
                        return 4;
                    }
                    return Write(static_cast<char>(c));
                }
                return -1;
            }

            void display() {
                int ox, oy;
                GetCursorPosition(ox, oy);
                SetCursorPosition(x, y);
                if (changed) {
                    clc = clock();
                }
                if (clock() - clc > 1000) {
                    clc = clock();
                }
                if (cmode == CInsert) {
                    if (cursorPos == static_cast<int>(text.size())) {
                        TextStyle::StyleOutput(text.c_str(), style.normal);
                        TextStyle::StyleOutput(" ", GetCursorColor());
                        while (changed-- > 0) TextStyle::StyleOutput("  ", TextStyle::NowTextStyle);
                    } else if (cursorPos == 0) {
                        TextStyle::StyleOutput(text.substr(0, 1).c_str(), GetCursorColor());
                        TextStyle::StyleOutput(text.substr(1).c_str(), style.normal);
                        while (changed-- > 0) TextStyle::StyleOutput("  ", TextStyle::NowTextStyle);
                    } else {
                        TextStyle::StyleOutput(text.substr(0, cursorPos).c_str(), style.normal);
                        TextStyle::StyleOutput(text.substr(cursorPos, 1).c_str(), GetCursorColor());
                        TextStyle::StyleOutput(text.substr(cursorPos + 1).c_str(), style.normal);
                        while (changed-- > 0) TextStyle::StyleOutput("  ", TextStyle::NowTextStyle);
                    }
                }
                changed = 0;
                SetCursorPosition(ox, oy);
            }
        };
    }

    //======================================================================
    // 剪贴板：UTF-8 <-> CF_UNICODETEXT
    //======================================================================
    namespace TextClip {
        // 静态类型转换，供剪贴板用（本类无状态）
        class Converter {
        public:
            static std::string WideToUtf8(const std::wstring& w) {
                std::string out;
                for (std::size_t i = 0; i < w.size(); ++i) {
                    std::uint32_t cp = static_cast<std::uint32_t>(w[i]);
                    if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < w.size()) {
                        const std::uint32_t lo = static_cast<std::uint32_t>(w[i + 1]);
                        if (lo >= 0xDC00 && lo <= 0xDFFF) {
                            cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
                            ++i;
                        }
                    }
                    out += Utf8::Encode(cp);
                }
                return out;
            }

            static bool Copy(const std::string& utf8) {
                if (!OpenClipboard(nullptr)) return false;
                EmptyClipboard();
                const std::wstring w = Utf8::ToWide(utf8);
                const std::size_t bytes = (w.size() + 1) * sizeof(wchar_t);
                bool ok = false;
                if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
                    if (void* p = GlobalLock(h)) {
                        std::memcpy(p, w.c_str(), bytes);
                        GlobalUnlock(h);
                        ok = SetClipboardData(CF_UNICODETEXT, h) != nullptr;
                    }
                    if (!ok) GlobalFree(h);
                }
                CloseClipboard();
                return ok;
            }

            static bool Paste(std::string& out) {
                if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) return false;
                if (!OpenClipboard(nullptr)) return false;
                bool ok = false;
                if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
                    if (const wchar_t* p = static_cast<const wchar_t*>(GlobalLock(h))) {
                        out = WideToUtf8(std::wstring(p));
                        GlobalUnlock(h);
                        ok = true;
                    }
                }
                CloseClipboard();
                return ok;
            }
        };
    }

    //======================================================================
    // 新版：TextInput 元件
    //======================================================================
    class TextInput : public UI {
    public:
        enum class EditMode { Insert, Replace, Select };

        std::string label{};                       // 框外标签：文字 + 空格 + 框
        std::string value{};                       // 内容（可含 '\n'）
        std::string placeholder{ "输入…" };
        std::string prefix{};                      // 框内前缀（保留兼容，默认空）
        bool password{ false };
        bool showFrame{ true };
        bool multiline{ false };
        int maxLength{ 256 };                      // 按码点计
        int maxLines{ 6 };                         // 多行时可见行数上限，超出内部滚动
        EditMode mode{ EditMode::Insert };

        BoxChars frame{ Box::Round };

        std::function<void(const std::string&)> onSubmit{};
        std::function<void(const std::string&)> onChange{};

        std::size_t cursor{ 0 };                   // 码点索引
        std::size_t selAnchor{ 0 };                // 选区锚点（与 cursor 之间为选中区）
        int scroll{ 0 };                            // 当前行左侧滚出的码点数
        int scrollLine{ 0 };                        // 多行时的行滚动

        TextInput() { signature = "UITextInput";
            focusable = true;
            mouseCursor = MouseCursor::IBeam;
            // 不设不透明底色：输入框坐在谁身上就用谁的底色（想改请显式 SetBackground）
            style = Style(Theme::Current().text, Theme::Current().surfaceAlt);
        }

        explicit TextInput(const std::string& val) : TextInput() { signature = "UITextInput"; value = val; cursor = Cps().size(); }

        //---------- 配置 ----------
        TextInput& SetLabel(const std::string& text) { label = text; InvalidateLayout(); return *this; }
        TextInput& SetPlaceholder(const std::string& p) { placeholder = p; Invalidate(); return *this; }
        TextInput& SetPrefix(const std::string& p) { prefix = p; Invalidate(); return *this; }
        TextInput& SetFrame(const BoxChars& c) { frame = c; showFrame = true; InvalidateLayout(); return *this; }
        TextInput& SetPassword(const bool b) { password = b; Invalidate(); return *this; }
        TextInput& SetMaxLength(const int n) {
            maxLength = std::max(n, 1);
            if (static_cast<int>(Cps().size()) > maxLength) {
                std::vector<std::uint32_t> cps = Cps();
                cps.resize(static_cast<std::size_t>(maxLength));
                cursor = std::min(cursor, cps.size());
                selAnchor = cursor;
                Rebuild(cps);
            }
            return *this;
        }
        TextInput& SetMultiline(const bool b) { multiline = b; scrollLine = 0; InvalidateLayout(); return *this; }
        TextInput& SetMaxLines(const int n) { maxLines = std::max(n, 1); InvalidateLayout(); return *this; }
        TextInput& SetMode(const EditMode m) { mode = m; Invalidate(); return *this; }
        TextInput& SetOverwrite(const bool b) { mode = b ? EditMode::Replace : EditMode::Insert; Invalidate(); return *this; }
        // 选择模式：方向键直接扩展选区（不必按 Shift）
        TextInput& SetSelectionMode(const bool b) { mode = b ? EditMode::Select : EditMode::Insert; Invalidate(); return *this; }
        [[nodiscard]] bool IsSelectionMode() const { return mode == EditMode::Select; }
        // 只改文字颜色（输入框底色始终继承容器）
        TextInput& SetTextColor(const Color& c) { style.fg = c; Invalidate(); return *this; }

        [[nodiscard]] bool IsOverwrite() const { return mode == EditMode::Replace; }

        //---------- 内容 ----------
        [[nodiscard]] std::vector<std::uint32_t> Cps() const { return Utf8::Decode(value); }

        void SetValue(const std::string& v) {
            // 截断在这里也必须生效：外部 SetValue / 长度上限调小都走同一条路
            std::vector<std::uint32_t> cps = Utf8::Decode(v);
            if (static_cast<int>(cps.size()) > maxLength) cps.resize(static_cast<std::size_t>(maxLength));
            std::string s;
            s.reserve(cps.size());
            for (const auto cp : cps) s += Utf8::Encode(cp);
            value = s;
            cursor = cps.size();
            selAnchor = cursor;
            scroll = 0;
            scrollLine = 0;
            InvalidateLayout();
            Invalidate();
            NotifyChange();
        }

        [[nodiscard]] const std::string& GetValue() const { return value; }

        //---------- 度量 ----------
        [[nodiscard]] int LabelWidth() const {
            return label.empty() ? 0 : Utf8::DisplayWidth(label) + 1;
        }

        [[nodiscard]] int LineCount() const {
            if (!multiline) return 1;
            int n = 1;
            for (const auto cp : Cps()) if (cp == '\n') ++n;
            return n;
        }

        [[nodiscard]] int VisibleRows() const {
            return multiline ? std::clamp(LineCount(), 1, maxLines) : 1;
        }

        [[nodiscard]] int MeasureWidth() const override {
            if (layout.width >= 0) return layout.width;
            return LabelWidth() + 24;
        }

        [[nodiscard]] int MeasureHeight() const override {
            if (layout.height >= 0) return layout.height;
            return showFrame ? VisibleRows() + 2 : VisibleRows();
        }

        //---------- 选区 ----------
        [[nodiscard]] bool HasSelection() const { return selAnchor != cursor; }
        [[nodiscard]] std::size_t SelBegin() const { return std::min(selAnchor, cursor); }
        [[nodiscard]] std::size_t SelEnd() const { return std::max(selAnchor, cursor); }

        void ClearSelection() { selAnchor = cursor; }

        void SelectAll() {
            selAnchor = 0;
            cursor = Cps().size();
            Invalidate();
        }

        [[nodiscard]] std::string SelectedText() const {
            if (!HasSelection()) return {};
            const auto cps = Cps();
            std::string s;
            for (std::size_t i = SelBegin(); i < SelEnd() && i < cps.size(); ++i) s += Utf8::Encode(cps[i]);
            return s;
        }

        //---------- 编辑 ----------
        // 插入/替换（会先吃掉选区）；overwrite 模式下覆盖光标右侧一个码点
        void InsertText(const std::string& s) {
            m_menuOpen = false;
            PushUndo();
            auto cps = Cps();
            if (HasSelection()) {
                cps.erase(cps.begin() + static_cast<std::ptrdiff_t>(SelBegin()),
                          cps.begin() + static_cast<std::ptrdiff_t>(SelEnd()));
                cursor = SelBegin();
                selAnchor = cursor;
            }
            const auto add = Utf8::Decode(s);
            for (const auto cp : add) {
                if (static_cast<int>(cps.size()) >= maxLength && mode == EditMode::Insert) break;
                if (mode == EditMode::Replace && cursor < cps.size() && cps[cursor] != '\n' && cp != '\n') {
                    cps[cursor] = cp;
                    ++cursor;
                } else {
                    cps.insert(cps.begin() + static_cast<std::ptrdiff_t>(cursor), cp);
                    ++cursor;
                }
            }
            // 统一截断（Replace 模式/粘贴在尾部也可能把长度顶上去）
            if (static_cast<int>(cps.size()) > maxLength) cps.resize(static_cast<std::size_t>(maxLength));
            cursor = std::min(cursor, cps.size());
            selAnchor = cursor;
            Rebuild(cps);
        }

        void EraseBefore() {
            if (HasSelection()) {
                PushUndo();
                auto cps = Cps();
                cps.erase(cps.begin() + static_cast<std::ptrdiff_t>(SelBegin()),
                          cps.begin() + static_cast<std::ptrdiff_t>(SelEnd()));
                cursor = SelBegin();
                selAnchor = cursor;
                Rebuild(cps);
                return;
            }
            if (cursor == 0) return;
            PushUndo();
            auto cps = Cps();
            cps.erase(cps.begin() + static_cast<std::ptrdiff_t>(cursor - 1));
            --cursor;
            selAnchor = cursor;
            Rebuild(cps);
        }

        void EraseAfter() {
            if (HasSelection()) {
                PushUndo();
                auto cps = Cps();
                cps.erase(cps.begin() + static_cast<std::ptrdiff_t>(SelBegin()),
                          cps.begin() + static_cast<std::ptrdiff_t>(SelEnd()));
                cursor = SelBegin();
                selAnchor = cursor;
                Rebuild(cps);
                return;
            }
            auto cps = Cps();
            if (cursor >= cps.size()) return;
            PushUndo();
            cps.erase(cps.begin() + static_cast<std::ptrdiff_t>(cursor));
            selAnchor = cursor;
            Rebuild(cps);
        }

        void EraseWordBefore() {
            if (HasSelection()) { EraseBefore(); return; }
            if (cursor == 0) return;
            PushUndo();
            auto cps = Cps();
            std::size_t i = cursor;
            while (i > 0 && cps[i - 1] == ' ') --i;
            while (i > 0 && cps[i - 1] != ' ' && cps[i - 1] != '\n') --i;
            cps.erase(cps.begin() + static_cast<std::ptrdiff_t>(i),
                      cps.begin() + static_cast<std::ptrdiff_t>(cursor));
            cursor = i;
            selAnchor = cursor;
            Rebuild(cps);
        }

        void MoveCursor(const int delta, const bool extend = false) {
            const auto cps = Cps();
            const int n = static_cast<int>(cps.size());
            int pos = static_cast<int>(cursor) + delta;
            pos = std::clamp(pos, 0, n);
            // 跨行时跳过 '\n'（向左）——光标停在行尾/行首更自然
            cursor = static_cast<std::size_t>(pos);
            if (!extend) selAnchor = cursor;
            Invalidate();
        }

        // 按词移动（Ctrl+←/→）：只移动光标，绝不删字
        void MoveWord(const int delta, const bool extend = false) {
            const auto cps = Cps();
            const int n = static_cast<int>(cps.size());
            int pos = static_cast<int>(cursor);
            if (delta < 0) {
                while (pos > 0 && (cps[pos - 1] == ' ' || cps[pos - 1] == '\n')) --pos;
                while (pos > 0 && cps[pos - 1] != ' ' && cps[pos - 1] != '\n') --pos;
            } else {
                while (pos < n && cps[pos] == ' ') ++pos;
                while (pos < n && cps[pos] != ' ' && cps[pos] != '\n') ++pos;
                while (pos < n && cps[pos] == ' ') ++pos;
            }
            cursor = static_cast<std::size_t>(std::clamp(pos, 0, n));
            if (!extend) selAnchor = cursor;
            Invalidate();
        }

        // 按词删除（Ctrl+Backspace / Ctrl+Delete）：编辑器标准行为
        void EraseWordAfter() {
            if (HasSelection()) { EraseAfter(); return; }
            auto cps = Cps();
            if (cursor >= cps.size()) return;
            std::size_t i = cursor;
            if (cps[i] == ' ') { while (i < cps.size() && cps[i] == ' ') ++i; }
            else { while (i < cps.size() && cps[i] != ' ' && cps[i] != '\n') ++i; }
            PushUndo();
            cps.erase(cps.begin() + static_cast<std::ptrdiff_t>(cursor),
                      cps.begin() + static_cast<std::ptrdiff_t>(i));
            selAnchor = cursor;
            Rebuild(cps);
        }

        // PageUp / PageDown：多行时按可见行翻页，单行时跳到头/尾
        void MovePage(const int pages, const bool extend = false) {
            if (multiline) MoveLine(pages * VisibleRows(), extend);
            else { cursor = pages < 0 ? 0 : Cps().size(); if (!extend) selAnchor = cursor; Invalidate(); }
        }

        // 多行：上下移动保持列号
        void MoveLine(const int delta, const bool extend = false) {
            if (!multiline) return;
            const auto cps = Cps();
            const auto [line, col] = CaretLineCol();
            int target = static_cast<int>(line) + delta;
            if (target < 0 || target >= LineCount()) return;
            std::size_t pos = LineStart(static_cast<std::size_t>(target));
            const std::size_t len = LineLength(static_cast<std::size_t>(target));
            pos += std::min(col, len);
            cursor = pos;
            if (!extend) selAnchor = cursor;
            Invalidate();
        }

        void CursorHome(const bool extend = false) {
            if (multiline) cursor = LineStart(CaretLineCol().first);
            else cursor = 0;
            if (!extend) selAnchor = cursor;
            Invalidate();
        }

        void CursorEnd(const bool extend = false) {
            if (multiline) {
                const std::size_t line = CaretLineCol().first;
                cursor = LineStart(line) + LineLength(line);
            } else {
                cursor = Cps().size();
            }
            if (!extend) selAnchor = cursor;
            Invalidate();
        }

        void NotifyChange() { if (onChange) onChange(value); }

        void Submit() { if (onSubmit) onSubmit(value); }

        // 消息："setText"/"append"/"submit"/"clear"/"copy"/"cut"/"paste"/"undo"/"redo"
        bool OnMessage(const Message& msg) override {
            if (!isEnabled) return false;
            if (msg.type == "setText") { SetValue(msg.text); return true; }
            if (msg.type == "append") { InsertText(msg.text); return true; }
            if (msg.type == "submit") { Submit(); return true; }
            if (msg.type == "clear") { SetValue(""); return true; }
            if (msg.type == "copy") { CopySelection(); return true; }
            if (msg.type == "cut") { CutSelection(); return true; }
            if (msg.type == "paste") { PasteClipboard(); return true; }
            if (msg.type == "undo") { Undo(); return true; }
            if (msg.type == "redo") { Redo(); return true; }
            return UI::OnMessage(msg);
        }

        //---------- 撤销 / 重做 ----------
        void PushUndo() {
            undoStack.push_back({ value, cursor });
            if (undoStack.size() > 200) undoStack.erase(undoStack.begin());
            redoStack.clear();
        }

        void Undo() {
            if (undoStack.empty()) return;
            redoStack.push_back({ value, cursor });
            auto snap = undoStack.back();
            undoStack.pop_back();
            value = snap.first;
            cursor = std::min(snap.second, Cps().size());
            selAnchor = cursor;
            InvalidateLayout();
            Invalidate();
            NotifyChange();
        }

        void Redo() {
            if (redoStack.empty()) return;
            undoStack.push_back({ value, cursor });
            auto snap = redoStack.back();
            redoStack.pop_back();
            value = snap.first;
            cursor = std::min(snap.second, Cps().size());
            selAnchor = cursor;
            InvalidateLayout();
            Invalidate();
            NotifyChange();
        }

        //---------- 剪贴板 ----------
        bool CopySelection() {
            const std::string s = SelectedText();
            if (s.empty()) return false;
            return TextClip::Converter::Copy(s);
        }

        bool CutSelection() {
            const std::string s = SelectedText();
            if (s.empty()) return false;
            const bool ok = TextClip::Converter::Copy(s);
            PushUndo();
            auto cps = Cps();
            cps.erase(cps.begin() + static_cast<std::ptrdiff_t>(SelBegin()),
                      cps.begin() + static_cast<std::ptrdiff_t>(SelEnd()));
            cursor = SelBegin();
            selAnchor = cursor;
            Rebuild(cps);
            return ok;
        }

        bool PasteClipboard() {
            std::string s;
            if (!TextClip::Converter::Paste(s)) return false;
            // 归一化换行：统一成 '\n'，去掉 '\r'
            std::string clean;
            for (const char c : s) {
                if (c == '\r') continue;
                if (c == '\n' && !multiline) { clean.push_back(' '); continue; }
                clean.push_back(c);
            }
            InsertText(clean);
            return true;
        }

        //---------- 行/列工具 ----------
        [[nodiscard]] std::vector<std::vector<std::uint32_t>> Lines() const {
            const auto cps = Cps();
            std::vector<std::vector<std::uint32_t>> out(1);
            for (const auto cp : cps) {
                if (cp == '\n') out.emplace_back();
                else out.back().push_back(cp);
            }
            return out;
        }

        // 光标所在行 / 行内列（码点）
        [[nodiscard]] std::pair<std::size_t, std::size_t> CaretLineCol() const {
            const auto cps = Cps();
            std::size_t line = 0, col = 0;
            for (std::size_t i = 0; i < cursor && i < cps.size(); ++i) {
                if (cps[i] == '\n') { ++line; col = 0; }
                else ++col;
            }
            return { line, col };
        }

        // 第 line 行在整体码点序列里的起点
        [[nodiscard]] std::size_t LineStart(const std::size_t line) const {
            const auto cps = Cps();
            std::size_t idx = 0, n = 0;
            while (idx < cps.size() && n < line) {
                if (cps[idx] == '\n') ++n;
                ++idx;
            }
            return idx;
        }

        [[nodiscard]] std::size_t LineLength(const std::size_t line) const {
            const auto cps = Cps();
            const std::size_t start = LineStart(line);
            std::size_t len = 0;
            while (start + len < cps.size() && cps[start + len] != '\n') ++len;
            return len;
        }

        //---------- 事件 ----------
        bool OnKey(const xChar& key) override {
            if (!isEnabled) return false;
            const auto mods = Input::LastModifiers();
            const int code = key.Size() == 1 ? static_cast<int>(static_cast<unsigned char>(key.GetXChar(0))) : -1;

            // MENU 键（VK_APPS）：打开/关闭编辑菜单（复制 / 剪切 / 粘贴 / 撤销 / 重做 / 全选）
            if (IsKey(key, SKey::Menu)) {
                m_menuOpen = !m_menuOpen;
                m_menuIndex = 0;
                Invalidate();
                return true;
            }
            if (m_menuOpen) {
                const int items = static_cast<int>(MenuItems().size());
                if (IsKey(key, SKey::UpArrow) || IsKey(key, SKey::LeftArrow)) { m_menuIndex = (m_menuIndex + items - 1) % items; Invalidate(); return true; }
                if (IsKey(key, SKey::DownArrow) || IsKey(key, SKey::RightArrow)) { m_menuIndex = (m_menuIndex + 1) % items; Invalidate(); return true; }
                if (key == 13 || key == 10) { RunMenu(m_menuIndex); return true; }
                if (IsKey(key, SKey::Esc)) { m_menuOpen = false; Invalidate(); return true; }
                if (key == ' ') { RunMenu(m_menuIndex); return true; }
            }

            // Ctrl 快捷键（控制台会把这些键送成 C0 控制码，部分终端直接给 ctrl 位）
            const bool ctrl = mods.ctrl || (code >= 1 && code <= 26 && code != 8 && code != 9 && code != 13);
            if (ctrl) {
                switch (code) {
                case 1:  SelectAll(); return true;          // Ctrl+A
                case 3:  return CopySelection();            // Ctrl+C
                case 22: return PasteClipboard();           // Ctrl+V
                case 24: return CutSelection();             // Ctrl+X
                case 25: Redo(); return true;               // Ctrl+Y
                case 26:                                    // Ctrl(+Shift)+Z
                    if (mods.shift) Redo(); else Undo();
                    return true;
                default: break;
                }
            }

            if (key == 13 || key == 10) {                  // Enter
                if (multiline) InsertText("\n");
                else Submit();
                return true;
            }
            if (key == 8) {                                // Backspace
                if (ctrl || mods.alt) EraseWordBefore();
                else EraseBefore();
                return true;
            }
            if (key == 127) { EraseAfter(); return true; } // Delete
            if (IsKey(key, SKey::Delete)) {
                if (ctrl) EraseWordAfter();
                else EraseAfter();
                return true;
            }
            if (IsKey(key, SKey::Insert)) {                // Insert：插入/替换模式切换
                mode = (mode == EditMode::Insert) ? EditMode::Replace : EditMode::Insert;
                Invalidate();
                return true;
            }

            if (IsKey(key, SKey::LeftArrow)) {
                if (ctrl) MoveWord(-1, mods.shift || mode == EditMode::Select);   // 按词移动，不删字
                else MoveCursor(-1, mods.shift || mode == EditMode::Select);
                return true;
            }
            if (IsKey(key, SKey::RightArrow)) {
                if (ctrl) MoveWord(1, mods.shift || mode == EditMode::Select);
                else MoveCursor(1, mods.shift || mode == EditMode::Select);
                return true;
            }
            if (IsKey(key, SKey::UpArrow)) { MoveLine(-1, mods.shift || mode == EditMode::Select); return true; }
            if (IsKey(key, SKey::DownArrow)) { MoveLine(1, mods.shift || mode == EditMode::Select); return true; }
            if (IsKey(key, SKey::Home)) {
                if (ctrl) { cursor = 0; if (!(mods.shift || mode == EditMode::Select)) selAnchor = cursor; Invalidate(); }
                else CursorHome(mods.shift || mode == EditMode::Select);
                return true;
            }
            if (IsKey(key, SKey::End)) {
                if (ctrl) { cursor = Cps().size(); if (!(mods.shift || mode == EditMode::Select)) selAnchor = cursor; Invalidate(); }
                else CursorEnd(mods.shift || mode == EditMode::Select);
                return true;
            }
            if (IsKey(key, SKey::PageUp)) { MovePage(-1, mods.shift || mode == EditMode::Select); return true; }
            if (IsKey(key, SKey::PageDown)) { MovePage(1, mods.shift || mode == EditMode::Select); return true; }

            // 可打印字符（含 UTF-8 多字节）
            if (key.IsPrintableAscii()) {
                InsertText(std::string(1, key.Ascii()));
                return true;
            }
            if (key.IsUtf8Multibyte()) {
                InsertText(key.ToString());
                return true;
            }
            return false;
        }

        //---------- 鼠标 ----------
        // 点击定位光标（按显示宽度累计，宽字符占 2 格）；extend 用于拖选
        void SetCursorByPixel(const int mouseX, const int mouseY = -1, const bool extend = false) {
            Bounds inner = InnerBounds();
            if (inner.IsEmpty()) return;

            std::size_t line = 0;
            if (multiline) {
                const int relY = (mouseY < 0 ? 0 : mouseY) - inner.Top();
                const int row = std::clamp(scrollLine + relY, 0, std::max(LineCount() - 1, 0));
                line = static_cast<std::size_t>(row);
            }

            const int prefixW = Utf8::DisplayWidth(prefix);
            const int textX = inner.Left() + (line == 0 ? prefixW : 0);
            const int hs = (multiline && line != CaretLineCol().first) ? 0 : scroll;

            const auto lines = Lines();
            if (line < lines.size()) {
                std::size_t col = 0;
                int curX = textX;
                std::size_t begin = 0;
                int skipped = 0;
                while (begin < lines[line].size() && skipped < hs) {
                    skipped += password ? 1 : Utf8::CharWidth(lines[line][begin]);
                    ++begin;
                }
                col = begin;
                for (std::size_t i = begin; i < lines[line].size(); ++i) {
                    const int w = password ? 1 : Utf8::CharWidth(lines[line][i]);
                    if (mouseX < curX + w) break;
                    curX += w;
                    col = i + 1;
                }
                cursor = LineStart(line) + col;
            }
            if (!extend) selAnchor = cursor;
            Invalidate();
        }

        [[nodiscard]] bool IsHovered() const { return m_hover; }

        //---------- 编辑菜单（MENU 键） ----------
        [[nodiscard]] static const std::vector<std::string>& MenuItems() {
            static const std::vector<std::string> items{ "复制", "剪切", "粘贴", "撤销", "重做", "全选" };
            return items;
        }

        [[nodiscard]] bool MenuOpen() const { return m_menuOpen; }

        void RunMenu(const int index) {
            m_menuOpen = false;
            switch (index) {
            case 0: CopySelection(); break;
            case 1: CutSelection(); break;
            case 2: PasteClipboard(); break;
            case 3: Undo(); break;
            case 4: Redo(); break;
            case 5: SelectAll(); break;
            default: break;
            }
            Invalidate();
        }

        // 菜单条：贴着输入框内第一行横向排开（受元件裁剪限制，不做越界弹窗）
        [[nodiscard]] Bounds MenuRect() const {
            const Bounds inner = InnerBounds();
            if (inner.IsEmpty()) return Bounds{ Left(), Top(), Width(), 1 };
            return Bounds{ inner.Left(), inner.Top(), inner.width(), 1 };
        }

        // 菜单命中的项（菜单条里的列位置）
        [[nodiscard]] int MenuHit(const int mouseX) const {
            const Bounds r = MenuRect();
            int x = r.Left();
            const auto& items = MenuItems();
            for (int i = 0; i < static_cast<int>(items.size()); ++i) {
                const int w = Utf8::DisplayWidth(items[i]) + 1;
                if (mouseX >= x && mouseX < x + w) return i;
                x += w;
            }
            return -1;
        }

        void OnMouseEnter() override { m_hover = true; Invalidate(); }
        void OnMouseLeave() override { if (m_hover) { m_hover = false; Invalidate(); } }

        bool OnMouse(const xMouse& ev) override {
            if (!isEnabled) return false;
            if (m_menuOpen && ev.action == MouseAction::Down) {
                const Bounds r = MenuRect();
                if (r.Contains(Position{ ev.x, ev.y })) {
                    const int idx = MenuHit(ev.x);
                    if (idx >= 0) { RunMenu(idx); return true; }
                }
                m_menuOpen = false;
                Invalidate();
            }
            switch (ev.action) {
            case MouseAction::Move:
                if (!m_hover) { m_hover = true; Invalidate(); }
                if (m_dragging) SetCursorByPixel(ev.x, ev.y, true);
                return true;
            case MouseAction::Down:
            case MouseAction::DoubleClick:
                m_hover = true;
                m_dragging = true;
                SetCursorByPixel(ev.x, ev.y, false);
                return true;
            case MouseAction::Up:
                m_dragging = false;
                return true;
            default:
                return false;
            }
        }

        //---------- 绘制 ----------
        void OnRender(ConsoleBackBuffer& buf) override {
            const Theme& th = Theme::Current();
            const Color bg = EffectiveBackground();
            const Color borderColor = !isEnabled ? th.borderDisabled : (focused ? th.borderFocus : th.border);
            // 文字色取自身 style.fg（SetTextColor 可动态改，demo 的 RGB 滑杆就接在这里）
            const Color textColor = isEnabled ? style.fg : th.textDisabled;
            const Color mutedColor = isEnabled ? th.textMuted : th.textDisabled;

            buf.FillStyle(bounds, Style(textColor, bg));

            const int lw = LabelWidth();
            const int boxX = Left() + lw;
            const int boxW = Width() - lw;

            // 框外标签：文字 + 一个空格
            if (!label.empty()) {
                const std::string shown = Utf8::Truncate(label, std::max(Width() - 1, 1));
                buf.WriteText(Left(), Top() + (Height() - 1) / 2, shown, Style(mutedColor, bg), Width());
            }

            if (boxW <= 0) return;

            Bounds inner{ boxX, Top(), boxW, Height() };
            if (showFrame && boxW >= 3 && Height() >= 3) {
                buf.DrawBox(Bounds{ boxX, Top(), boxW, Height() }, frame, Style(borderColor, bg));
                inner = Bounds{ boxX + 1, Top() + 1, std::max(boxW - 2, 0), std::max(Height() - 2, 0) };
            }
            if (inner.IsEmpty()) return;

            const auto lines = Lines();
            const auto [caretLine, caretCol] = CaretLineCol();
            const int rows = inner.height();
            const int prefixW = Utf8::DisplayWidth(prefix);
            const int textW = std::max(inner.width() - prefixW, 1);

            // 多行：保证光标行在可视窗口内
            if (multiline) {
                const int total = static_cast<int>(lines.size());
                if (static_cast<int>(caretLine) < scrollLine) scrollLine = static_cast<int>(caretLine);
                else if (static_cast<int>(caretLine) >= scrollLine + rows) scrollLine = static_cast<int>(caretLine) - rows + 1;
                scrollLine = std::clamp(scrollLine, 0, std::max(total - rows, 0));
            } else {
                scrollLine = 0;
            }

            // 水平滚动：保证光标列可见，并尽量在光标左右留出上下文（不是贴边只剩 1 格）
            const int caretLineWidth = DisplayWidthOfLine(lines, caretLine, caretCol);
            const int margin = std::min(std::max(textW / 4, 1), 4);
            if (caretLineWidth - scroll > textW - 1 - margin) scroll = caretLineWidth - (textW - 1 - margin);
            if (caretLineWidth - scroll < margin) scroll = caretLineWidth - margin;
            if (scroll < 0) scroll = 0;
            int lineFullWidth = 0;
            for (const auto cp : lines[caretLine]) lineFullWidth += password ? 1 : Utf8::CharWidth(cp);
            const int maxScroll = std::max(lineFullWidth - (textW - 1), 0);
            if (scroll > maxScroll) scroll = maxScroll;

            const Style selStyle = Style(th.text, th.accent);
            const bool empty = value.empty();

            for (int row = 0; row < rows; ++row) {
                const std::size_t line = static_cast<std::size_t>(scrollLine + row);
                if (line >= lines.size()) break;
                const int y = inner.Top() + row;
                int x = inner.Left();

                if (line == 0 && !prefix.empty() && x < inner.Right()) {
                    x += buf.WriteText(x, y, Utf8::Truncate(prefix, inner.Right() - x),
                                       Style(mutedColor, bg), inner.Right() - x);
                }

                const int hs = (line == caretLine || !multiline) ? scroll : 0;
                const std::size_t lineStart = LineStart(line);
                // scroll 是「滚出的显示列」，不是码点下标：先把前 hs 列按宽度跳过
                std::size_t begin = 0;
                int skipped = 0;
                while (begin < lines[line].size() && skipped < hs) {
                    skipped += password ? 1 : Utf8::CharWidth(lines[line][begin]);
                    ++begin;
                }
                int used = 0;
                for (std::size_t i = begin; i < lines[line].size(); ++i) {
                    const std::uint32_t cp = lines[line][i];
                    const std::string s = password ? "*" : Utf8::Encode(cp);
                    const int w = password ? 1 : Utf8::CharWidth(cp);
                    if (used + w > inner.Right() - x) break;
                    const std::size_t gi = lineStart + i;
                    const Style st = (HasSelection() && gi >= SelBegin() && gi < SelEnd()) ? selStyle : Style(textColor, bg);
                    buf.WriteText(x + used, y, s, st, inner.Right() - (x + used));
                    used += w;
                }

                // 占位文字（空值时第一行）
                if (empty && line == 0 && !placeholder.empty()) {
                    const int px = x + (focused ? 1 : 0);
                    buf.WriteText(px, y, Utf8::Truncate(placeholder, inner.Right() - px),
                                  Style(mutedColor, bg), inner.Right() - px);
                }

                // 光标：反色块
                if (focused && line == caretLine) {
                    const int cx = x + DisplayWidthOfLine(lines, caretLine, caretCol) - hs;
                    if (cx >= x && cx < inner.Right()) {
                        buf.SetCell(cx, y, L' ', Style(bg, textColor).SetBold());
                    }
                }
            }

            // MENU 菜单条：横向排在输入框内第一行
            if (m_menuOpen) {
                const Bounds r = MenuRect();
                if (r.width() >= 3) {
                    buf.FillStyle(r, Style(th.text, th.surfaceAlt));
                    const auto& items = MenuItems();
                    int mx = r.Left();
                    for (int i = 0; i < static_cast<int>(items.size()) && mx < r.Right(); ++i) {
                        const std::string shown = items[i] + " ";
                        const bool hot = (i == m_menuIndex);
                        buf.WriteText(mx, r.Top(), shown,
                                      hot ? Style(th.accentText, th.accent).SetBold() : Style(th.text, th.surfaceAlt),
                                      r.Right() - mx);
                        mx += Utf8::DisplayWidth(shown);
                    }
                }
            }
        }

    private:
        struct Snapshot {
            std::string first;   // value
            std::size_t second;  // cursor
        };

        bool m_hover{ false };
        bool m_dragging{ false };
        bool m_menuOpen{ false };
        int m_menuIndex{ 0 };
        std::vector<Snapshot> undoStack{};
        std::vector<Snapshot> redoStack{};

        [[nodiscard]] Bounds InnerBounds() const {
            const int lw = LabelWidth();
            Bounds box{ Left() + lw, Top(), std::max(Width() - lw, 0), Height() };
            if (showFrame && box.width() >= 3 && box.height() >= 3) {
                box = Bounds{ box.Left() + 1, box.Top() + 1, box.width() - 2, box.height() - 2 };
            }
            return box;
        }

        // 一行前 col 个码点的显示宽度
        static int DisplayWidthOfLine(const std::vector<std::vector<std::uint32_t>>& lines,
                                      const std::size_t line, const std::size_t col) {
            if (line >= lines.size()) return 0;
            int w = 0;
            for (std::size_t i = 0; i < col && i < lines[line].size(); ++i) w += Utf8::CharWidth(lines[line][i]);
            return w;
        }

        void Rebuild(const std::vector<std::uint32_t>& cps) {
            std::string s;
            s.reserve(cps.size() * 2);
            for (const auto cp : cps) s += Utf8::Encode(cp);
            const int before = LineCount();
            value = s;
            const int after = LineCount();
            if (before != after) InvalidateLayout();
            Invalidate();
            NotifyChange();
        }
    };

    using UITextInput = TextInput;
}
