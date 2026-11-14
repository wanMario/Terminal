//
// Created by Administrator on 2026/3/25.
//

#pragma once


#include <iostream>
#include <string>
#include <mutex>
#include <Windows.h>


class SafeHandle {
    HANDLE m_handle;
public:
    explicit constexpr SafeHandle(const HANDLE h = nullptr) noexcept : m_handle(h) {}
    ~SafeHandle() noexcept {
        if (IsValid()) {
            CloseHandle(m_handle);
            m_handle = nullptr;
        }
    }
    SafeHandle(const SafeHandle&) = delete;
    SafeHandle& operator=(const SafeHandle&) = delete;
    SafeHandle(SafeHandle&& other) noexcept : m_handle(other.m_handle) {
        other.m_handle = nullptr;
    }
    SafeHandle& operator=(SafeHandle&& other) noexcept {
        if (this != &other) {
            if (IsValid()) CloseHandle(m_handle);
            m_handle = other.m_handle;
            other.m_handle = nullptr;
        }
        return *this;
    }
    bool IsValid() const noexcept { return m_handle != nullptr && m_handle != INVALID_HANDLE_VALUE; }
    HANDLE Get() const noexcept { return m_handle; }
    void Release() noexcept {
        if (IsValid()) {
            CloseHandle(m_handle);
            m_handle = nullptr;
        }
    }
    HANDLE Detach() noexcept {
        const HANDLE temp = m_handle;
        m_handle = nullptr;
        return temp;
    }
};

namespace Terminal {
    // 全局缓存标准输出句柄，避免频繁系统调用
    inline const HANDLE g_hStdIn = GetStdHandle(STD_INPUT_HANDLE);
    inline const HANDLE g_hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    inline const HANDLE g_hStdErr = GetStdHandle(STD_ERROR_HANDLE);
    inline std::mutex g_consoleMtx;

    inline void CursorVisible(const bool bVisible) {
        CONSOLE_CURSOR_INFO CursorInfo;
        GetConsoleCursorInfo(g_hStdOut, &CursorInfo);
        CursorInfo.bVisible = bVisible;
        SetConsoleCursorInfo(g_hStdOut, &CursorInfo);
    }

    inline HANDLE GetStdinHandle() noexcept { return g_hStdIn; }
    inline HANDLE GetStdoutHandle() noexcept { return g_hStdOut; }
    inline HANDLE GetStderrHandle() noexcept { return g_hStdErr; }

    inline void RemoveQuickEdit() {
        DWORD mode;
        GetConsoleMode(g_hStdIn, &mode);
        mode &= ~ENABLE_QUICK_EDIT_MODE;
        SetConsoleMode(g_hStdIn, mode);
    }

    inline void SetStdCP() {
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
    }

    inline void SetCursorPosition(const int x, const int y, HANDLE hConsole = g_hStdOut) {
        const COORD pos = { static_cast<SHORT>(x), static_cast<SHORT>(y) };
        SetConsoleCursorPosition(hConsole, pos);
    }

    inline bool GetCursorPosition(int& x, int& y, HANDLE hConsole = g_hStdOut) {
        CONSOLE_SCREEN_BUFFER_INFO csbinfo;
        if (GetConsoleScreenBufferInfo(hConsole, &csbinfo)) {
            x = csbinfo.dwCursorPosition.X;
            y = csbinfo.dwCursorPosition.Y;
        } else return false;
        return true;
    }

    inline bool GetCursorPosition(POINT& pt, HANDLE hConsole = g_hStdOut) {
        CONSOLE_SCREEN_BUFFER_INFO csbinfo;
        if (GetConsoleScreenBufferInfo(hConsole, &csbinfo)) {
            pt.x = csbinfo.dwCursorPosition.X;
            pt.y = csbinfo.dwCursorPosition.Y;
        } else return false;
        return true;
    }

    inline POINT GetCursorPosition(HANDLE hConsole = g_hStdOut) {
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        if (GetConsoleScreenBufferInfo(hConsole, &csbi)) {
            return { csbi.dwCursorPosition.X, csbi.dwCursorPosition.Y };
        }
        return { -1, -1 };
    }

    inline void ClearScreen() {
        const HANDLE hStdOut = g_hStdOut;
        constexpr COORD coord = { 0, 0 };
        DWORD count;
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        GetConsoleScreenBufferInfo(hStdOut, &csbi);
        FillConsoleOutputCharacter(hStdOut, ' ', csbi.dwSize.X * csbi.dwSize.Y, coord, &count);
        SetConsoleCursorPosition(hStdOut, coord);
    }

    namespace TextStyle {
        constexpr DWORD TextStyleNone = 0;
        constexpr DWORD TextLight = FOREGROUND_INTENSITY;
        constexpr DWORD TextRed = FOREGROUND_RED;
        constexpr DWORD TextGreen = FOREGROUND_GREEN;
        constexpr DWORD TextBlue = FOREGROUND_BLUE;
        constexpr DWORD TextYellow = FOREGROUND_RED | FOREGROUND_GREEN;
        constexpr DWORD TextCyan = FOREGROUND_GREEN | FOREGROUND_BLUE;
        constexpr DWORD TextMagenta = FOREGROUND_RED | FOREGROUND_BLUE;
        constexpr DWORD TextGray = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
        constexpr DWORD TextBlack = 0;
        constexpr DWORD TextLightRed = FOREGROUND_RED | FOREGROUND_INTENSITY;
        constexpr DWORD TextLightGreen = FOREGROUND_GREEN | FOREGROUND_INTENSITY;
        constexpr DWORD TextLightBlue = FOREGROUND_BLUE | FOREGROUND_INTENSITY;
        constexpr DWORD TextLightYellow = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
        constexpr DWORD TextLightCyan = FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
        constexpr DWORD TextLightMagenta = FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
        constexpr DWORD TextWhite = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
        constexpr DWORD BGStyleNone = 0;
        constexpr DWORD BGLight = BACKGROUND_INTENSITY;
        constexpr DWORD BGRed = BACKGROUND_RED;
        constexpr DWORD BGGreen = BACKGROUND_GREEN;
        constexpr DWORD BGBlue = BACKGROUND_BLUE;
        constexpr DWORD BGYellow = BACKGROUND_RED | BACKGROUND_GREEN;
        constexpr DWORD BGCyan = BACKGROUND_GREEN | BACKGROUND_BLUE;
        constexpr DWORD BGMagenta = BACKGROUND_RED | BACKGROUND_BLUE;
        constexpr DWORD BGGray = BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE;
        constexpr DWORD BGBlack = 0;
        constexpr DWORD BGLightRed = BACKGROUND_RED | BACKGROUND_INTENSITY;
        constexpr DWORD BGLightGreen = BACKGROUND_GREEN | BACKGROUND_INTENSITY;
        constexpr DWORD BGLightBlue = BACKGROUND_BLUE | BACKGROUND_INTENSITY;
        constexpr DWORD BGLightYellow = BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_INTENSITY;
        constexpr DWORD BGLightCyan = BACKGROUND_GREEN | BACKGROUND_BLUE | BACKGROUND_INTENSITY;
        constexpr DWORD BGLightMagenta = BACKGROUND_RED | BACKGROUND_BLUE | BACKGROUND_INTENSITY;
        constexpr DWORD BGWhite = BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE | BACKGROUND_INTENSITY;

        inline DWORD NowTextStyle = TextWhite | BGBlack;

        inline void SetCoutStyle(const DWORD style) {
            NowTextStyle = style;
            SetConsoleTextAttribute(g_hStdOut, style);
        }

        inline void StyleOutput(const char* str, const DWORD style) {
            std::lock_guard<std::mutex> lock(g_consoleMtx);
            SetConsoleTextAttribute(g_hStdOut, style);
            printf("%s", str);
            SetConsoleTextAttribute(g_hStdOut, NowTextStyle);
        }

        inline void outPut(const std::string& str) {
            StyleOutput(str.c_str(), NowTextStyle);
        }

        inline DWORD StyleBG(const DWORD style) {
            return style & 0xf0;
        }

        inline DWORD StyleText(const DWORD style) {
            return style & 0x0f;
        }

        inline DWORD ReverseStyle(const DWORD style) {
            return (style & 0xf) << 4 | style >> 4;
        }

        inline DWORD ChangeStyleBG(const DWORD style, const DWORD styleBG) {
            return (style & 0x0f) | styleBG;
        }

        inline DWORD ChangeStyleText(const DWORD style, const DWORD styleText) {
            return (style & 0xf0) | styleText;
        }
    }
}

namespace TS {
    /*
    0x0	0b0000	Black 黑色	    无 RGB 通道
    0x1	0b0001	Blue 蓝色	    蓝
    0x2	0b0010	Green 绿色	    绿
    0x3	0b0011	Cyan 青色	    蓝 + 绿
    0x4	0b0100	Red 红色	    红
    0x5	0b0101	Purple 紫色	    红 + 蓝
    0x6	0b0110	Yellow 黄色	    红 + 绿
    0x7	0b0111	White 灰白色	红 + 绿 + 蓝

    */
    constexpr unsigned char Black = 0;
    constexpr unsigned char Blue = FOREGROUND_BLUE;
    constexpr unsigned char Green = FOREGROUND_GREEN;
    constexpr unsigned char Cyan = FOREGROUND_BLUE | FOREGROUND_GREEN;
    constexpr unsigned char Red = FOREGROUND_RED;
    constexpr unsigned char Purple = FOREGROUND_RED | FOREGROUND_BLUE;
    constexpr unsigned char Yellow = FOREGROUND_GREEN | FOREGROUND_RED;
    constexpr unsigned char Gray = FOREGROUND_GREEN | FOREGROUND_RED | FOREGROUND_BLUE;

    constexpr unsigned char Light = FOREGROUND_INTENSITY;
    constexpr unsigned char LWhite = FOREGROUND_INTENSITY | Gray;
    constexpr unsigned char Normal = Gray;

    inline unsigned char toAbbi(unsigned char txt, unsigned char bg) {
        return static_cast<unsigned char>(txt | (bg << 4));
    }

    static unsigned char char2hex(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return 0;
    }

    inline void outPut(const std::string& str) {
        std::lock_guard<std::mutex> lock(Terminal::g_consoleMtx);
        HANDLE hConsole = Terminal::g_hStdOut;
        SetConsoleTextAttribute(hConsole, Normal);
        size_t i = 0;
        const size_t len = str.size();
        while (i < len) {
            if (str[i] == '^' && i + 1 < len && str[i + 1] == '[') {
                i += 2;
                if (i >= len) break;
                if (str[i] == 'r' || str[i] == 'R') {
                    SetConsoleTextAttribute(hConsole, Normal);
                    i++;
                } else if (i + 1 < len) {
                    unsigned char high = char2hex(str[i]);
                    unsigned char low = char2hex(str[i + 1]);
                    unsigned char attr = static_cast<unsigned char>(high << 4 | low);
                    SetConsoleTextAttribute(hConsole, attr);
                    i += 2;
                }
                continue;
            }
            std::cout << str[i];
            i++;
        }
        SetConsoleTextAttribute(hConsole, Normal);
        std::cout.flush();
    }

    inline void outPut(const char* str) {
        outPut(std::string(str));
    }

    inline void outPrintln(const std::string& str) {
        outPut(str);
        std::cout << '\n';
    }
    inline void outPrintln(const char* str) {
        outPut(str);
        std::cout << '\n';
    }
}

namespace StringHelper {
    inline std::string operator*(const std::string& str1, int x) {
        std::string str2;
        str2.reserve(str1.size() * x);
        while (x--)
            str2 += str1;
        return str2;
    }
}
