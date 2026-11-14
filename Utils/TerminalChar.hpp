//
// Created by Administrator on 2026/6/1.
// 修订：改用 enum class SKey（避免枚举被隐式转成 char 而误配到初始化列表构造），
//      修复 map 的 CTAD 与缺失头文件，补齐按键表，增加 ASCII / UTF-8 判定。
//

#pragma once

#include <compare>
#include <conio.h>
#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace Terminal {
    enum class SKey {
        Invalid = 0,
        ASIIC,          // 可打印 ASCII 与普通单字节
        Unknow,
        UpArrow,        // -32 | 72
        DownArrow,      // -32 | 80
        LeftArrow,      // -32 | 75
        RightArrow,     // -32 | 77
        BackSpace,      // 8
        FastBackSpace,  // 127
        Delete,         // -32 | 83
        F1,             // 0 | 59
        F2, F3, F4, F5, F6, F7, F8, F9, F10,
        F11, F12, F13, F14, F15, F16,
        Insert,         // -32 | 82
        Home,           // -32 | 71
        End,            // -32 | 79
        PageUp,         // -32 | 73
        PageDown,       // -32 | 81
        PadHome,        // 0 | 71
        PadUpArrow,     // 0 | 72
        PadPgUp,        // 0 | 73
        PadLeftArrow,   // 0 | 75
        PadRightArrow,  // 0 | 77
        PadEnd,         // 0 | 79
        PadDownArrow,   // 0 | 80
        PadPgDn,        // 0 | 81
        PadIns,         // 0 | 82
        PadDel,         // 0 | 83
        Esc,            // 27
        Tab,            // 9
        Enter,          // 13
        Space,          // 32
        Menu,           // VK_APPS 菜单键：-32 | 93
    };

    /**
     * @brief 一次按键事件：单字节 ASCII、UTF-8 多字节序列，或 -32/0 前缀 + 扫描码。
     */
    class xChar {
        std::vector<char> ch;

    public:
        xChar() = default;
        virtual ~xChar() = default;

        xChar(const std::initializer_list<char> lst) {
            for (const char c : lst) ch.emplace_back(c);
        }
        explicit xChar(const std::string& s) : ch(s.begin(), s.end()) {}
        xChar(SKey sk) { *this = sk; }
        xChar(const char c) { ch.emplace_back(c); }
        xChar(const int c) { ch.emplace_back(static_cast<char>(c)); }

        xChar& operator=(SKey sk);

        [[nodiscard]] char GetXChar(const size_t x) const { return ch[x]; }
        void SetXChar(const size_t x, const char c) { ch[x] = c; }

        [[nodiscard]] size_t Size() const noexcept { return ch.size(); }
        [[nodiscard]] bool Empty() const noexcept { return ch.empty(); }
        [[nodiscard]] const std::vector<char>& Raw() const noexcept { return ch; }
        [[nodiscard]] std::string ToString() const { return { ch.begin(), ch.end() }; }

        void Clear() { ch.clear(); }
        void Push(const char c) { ch.emplace_back(c); }

        // map 需要严格弱序；不用 <=> 以兼容老编译器
        bool operator<(const xChar& rhs) const { return ch < rhs.ch; }
        bool operator==(const xChar& rhs) const { return ch == rhs.ch; }
        bool operator!=(const xChar& rhs) const { return ch != rhs.ch; }

        bool operator==(const char c) const { return ch.size() == 1 && ch[0] == c; }
        bool operator==(const int c) const { return ch.size() == 1 && ch[0] == c; }
        bool operator==(SKey sk) const;

        [[nodiscard]] bool IsPrintableAscii() const {
            return ch.size() == 1 && ch[0] >= 0x20 && ch[0] < 0x7F;
        }
        [[nodiscard]] bool IsAscii() const {
            for (const char c : ch) if (static_cast<unsigned char>(c) >= 0x80) return false;
            return !ch.empty();
        }
        [[nodiscard]] bool IsUtf8Multibyte() const {
            return !ch.empty() && static_cast<unsigned char>(ch[0]) >= 0xC0;
        }
        [[nodiscard]] char Ascii() const { return ch.empty() ? '\0' : ch[0]; }

        static xChar GetOutCh();

        void GetCh() {
            const int c = _getch();
            ch.assign(1, static_cast<char>(c));
            if (c == 0xE0 || c == 0) {
                ch.emplace_back(static_cast<char>(_getch()));
            }
        }

        static SKey GetSKey(const xChar& c);
    };

    //======================================================================
    // 按键表：SKey -> 原始字节序列
    //======================================================================
    inline std::map<SKey, xChar> SKey_xChar{
        { SKey::Invalid, xChar{} },
        { SKey::UpArrow, { -32, 72 } },
        { SKey::DownArrow, { -32, 80 } },
        { SKey::LeftArrow, { -32, 75 } },
        { SKey::RightArrow, { -32, 77 } },
        { SKey::BackSpace, { 8 } },
        { SKey::FastBackSpace, { 127 } },
        { SKey::Delete, { -32, 83 } },
        { SKey::F1, { 0, 59 } },
        { SKey::F2, { 0, 60 } },
        { SKey::F3, { 0, 61 } },
        { SKey::F4, { 0, 62 } },
        { SKey::F5, { 0, 63 } },
        { SKey::F6, { 0, 64 } },
        { SKey::F7, { 0, 65 } },
        { SKey::F8, { 0, 66 } },
        { SKey::F9, { 0, 67 } },
        { SKey::F10, { 0, 68 } },
        { SKey::F11, { 0, -123 } },
        { SKey::F12, { -32, -122 } },
        { SKey::Insert, { -32, 82 } },
        { SKey::Home, { -32, 71 } },
        { SKey::End, { -32, 79 } },
        { SKey::PageUp, { -32, 73 } },
        { SKey::PageDown, { -32, 81 } },
        { SKey::PadHome, { 0, 71 } },
        { SKey::PadUpArrow, { 0, 72 } },
        { SKey::PadPgUp, { 0, 73 } },
        { SKey::PadLeftArrow, { 0, 75 } },
        { SKey::PadRightArrow, { 0, 77 } },
        { SKey::PadEnd, { 0, 79 } },
        { SKey::PadDownArrow, { 0, 80 } },
        { SKey::PadPgDn, { 0, 81 } },
        { SKey::PadIns, { 0, 82 } },
        { SKey::PadDel, { 0, 83 } },
        { SKey::Esc, { 27 } },
        { SKey::Tab, { 9 } },
        { SKey::Enter, { 13 } },
        { SKey::Space, { 32 } },
        { SKey::Menu, { -32, 93 } }
    };

    //======================================================================
    // 反向表：原始字节序列 -> SKey
    //======================================================================
    inline std::map<xChar, SKey> xChar_SKey{
        { xChar{}, SKey::Invalid },
        { xChar{ -32, 72 }, SKey::UpArrow },
        { xChar{ -32, 80 }, SKey::DownArrow },
        { xChar{ -32, 75 }, SKey::LeftArrow },
        { xChar{ -32, 77 }, SKey::RightArrow },
        { xChar{ 8 }, SKey::BackSpace },
        { xChar{ 127 }, SKey::FastBackSpace },
        { xChar{ -32, 83 }, SKey::Delete },
        { xChar{ 0, 59 }, SKey::F1 },
        { xChar{ 0, 60 }, SKey::F2 },
        { xChar{ 0, 61 }, SKey::F3 },
        { xChar{ 0, 62 }, SKey::F4 },
        { xChar{ 0, 63 }, SKey::F5 },
        { xChar{ 0, 64 }, SKey::F6 },
        { xChar{ 0, 65 }, SKey::F7 },
        { xChar{ 0, 66 }, SKey::F8 },
        { xChar{ 0, 67 }, SKey::F9 },
        { xChar{ 0, 68 }, SKey::F10 },
        { xChar{ 0, -123 }, SKey::F11 },
        { xChar{ -32, -122 }, SKey::F12 },
        { xChar{ -32, 82 }, SKey::Insert },
        { xChar{ -32, 71 }, SKey::Home },
        { xChar{ -32, 79 }, SKey::End },
        { xChar{ -32, 73 }, SKey::PageUp },
        { xChar{ -32, 81 }, SKey::PageDown },
        { xChar{ 0, 71 }, SKey::PadHome },
        { xChar{ 0, 72 }, SKey::PadUpArrow },
        { xChar{ 0, 73 }, SKey::PadPgUp },
        { xChar{ 0, 75 }, SKey::PadLeftArrow },
        { xChar{ 0, 77 }, SKey::PadRightArrow },
        { xChar{ 0, 79 }, SKey::PadEnd },
        { xChar{ 0, 80 }, SKey::PadDownArrow },
        { xChar{ 0, 81 }, SKey::PadPgDn },
        { xChar{ 0, 82 }, SKey::PadIns },
        { xChar{ 0, 83 }, SKey::PadDel },
        { xChar{ 27 }, SKey::Esc },
        { xChar{ 9 }, SKey::Tab },
        { xChar{ 13 }, SKey::Enter },
        { xChar{ 32 }, SKey::Space },
        { xChar{ -32, 93 }, SKey::Menu }
    };

    inline xChar& xChar::operator=(const SKey sk) {
        const auto it = SKey_xChar.find(sk);
        ch = it == SKey_xChar.end() ? std::vector<char>{} : it->second.ch;
        return *this;
    }

    inline bool xChar::operator==(const SKey sk) const { return GetSKey(*this) == sk; }

    inline xChar xChar::GetOutCh() {
        const int c = _getch();
        if (c == 0xE0 || c == 0) {
            return xChar({ static_cast<char>(c), static_cast<char>(_getch()) });
        }
        return xChar({ static_cast<char>(c) });
    }

    inline SKey xChar::GetSKey(const xChar& c) {
        if (c.ch.empty()) return SKey::Invalid;
        // 只有 -32 / 0 前缀的双字节序列才是功能键，避免把 UTF-8 误判
        if (c.ch.size() == 2 && (c.ch[0] == -32 || c.ch[0] == 0)) {
            const auto it = xChar_SKey.find(c);
            return it == xChar_SKey.end() ? SKey::Unknow : it->second;
        }
        if (c.ch.size() == 1) {
            if (c.ch[0] == 27) return SKey::Esc;
            if (c.ch[0] == 9) return SKey::Tab;
            if (c.ch[0] == 13) return SKey::Enter;
            if (c.ch[0] == 32) return SKey::Space;
            if (c.ch[0] == 8) return SKey::BackSpace;
            if (c.ch[0] == 127) return SKey::FastBackSpace;
            return SKey::ASIIC;
        }
        return SKey::Unknow;
    }

    // 便捷判定
    inline bool IsKey(const xChar& key, const SKey sk) { return xChar::GetSKey(key) == sk; }
}
