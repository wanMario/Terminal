//
// Terminal / Utils / BoxChars.hpp
// 边框字符集预设，供 Border / Button / Panel 等元件复用
//

#pragma once

#include <cstdlib>
#include <cstring>
#include <string>
#include <windows.h>

namespace Terminal {

    struct BoxChars {
        wchar_t horizontal{ L'─' };
        wchar_t vertical{ L'│' };
        wchar_t topLeft{ L'┌' };
        wchar_t topRight{ L'┐' };
        wchar_t bottomLeft{ L'└' };
        wchar_t bottomRight{ L'┘' };
        wchar_t leftTee{ L'├' };
        wchar_t rightTee{ L'┤' };
        wchar_t topTee{ L'┬' };
        wchar_t bottomTee{ L'┴' };
        wchar_t cross{ L'┼' };

        [[nodiscard]] bool IsAscii() const { return horizontal == L'-'; }
        [[nodiscard]] bool IsBlank() const { return horizontal == L' '; }
    };

    namespace Box {
        inline const BoxChars Single{ L'─', L'│', L'┌', L'┐', L'└', L'┘', L'├', L'┤', L'┬', L'┴', L'┼' };
        inline const BoxChars Double{ L'═', L'║', L'╔', L'╗', L'╚', L'╝', L'╠', L'╣', L'╦', L'╩', L'╬' };
        inline const BoxChars Round { L'─', L'│', L'╭', L'╮', L'╰', L'╯', L'├', L'┤', L'┬', L'┴', L'┼' };
        inline const BoxChars Heavy { L'━', L'┃', L'┏', L'┓', L'┗', L'┛', L'┣', L'┫', L'┳', L'┻', L'╋' };
        inline const BoxChars Dashed{ L'╌', L'╎', L'┌', L'┐', L'└', L'┘', L'├', L'┤', L'┬', L'┴', L'┼' };
        inline const BoxChars Ascii { L'-', L'|', L'+', L'+', L'+', L'+', L'+', L'+', L'+', L'+', L'+' };
        inline const BoxChars Blank { L' ', L' ', L' ', L' ', L' ', L' ', L' ', L' ', L' ', L' ', L' ' };

        // 0=自动  1=强制 ASCII  -1=强制 Unicode
        inline int g_asciiFallbackMode = 0;
        inline bool g_asciiFallbackAutoResult = false;
        inline bool g_asciiFallbackDetected = false;

        // Windows 11（NT 10.0 build 22000+）的 conhost 已经能稳定显示 Unicode
        // 制表符；此时不应该再因为「没有 WT_SESSION 环境变量」就退回 ASCII。
        inline bool IsWindows11OrLater() {
            OSVERSIONINFOW info{};
            info.dwOSVersionInfoSize = sizeof(info);
            HRESULT(WINAPI* raw)(OSVERSIONINFOW*) = nullptr;
            FARPROC proc = GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
            if (proc == nullptr) return false;
            static_assert(sizeof(raw) == sizeof(proc), "function pointer size mismatch");
            std::memcpy(&raw, &proc, sizeof(raw));
            const LONG status = raw(&info);
            return status >= 0 && info.dwMajorVersion >= 10 && info.dwBuildNumber >= 22000;
        }

        // 当前输出是否已经打开 VT（说明是现代终端，而不是只支持 16 色属性的老 conhost）
        inline bool OutputSupportsVt() {
            const HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
            if (h == nullptr || h == INVALID_HANDLE_VALUE) return false;
            DWORD mode = 0;
            if (!GetConsoleMode(h, &mode)) return false;
            return (mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
        }

        // 是否为支持 ANSI / Unicode 制表符的现代终端。
        // 只看终端能力，不再按帧扫描缓冲区、猜测“有没有排版错乱”。
        inline bool IsAnsiSupported() {
            if (const char* wt = std::getenv("WT_SESSION"); wt && *wt) return true;
            if (const char* tp = std::getenv("TERM_PROGRAM"); tp && *tp) return true;
            if (const char* ce = std::getenv("ConEmuANSI"); ce && *ce) return true;
            if (const char* ansi = std::getenv("ANSICON"); ansi && *ansi) return true;
            if (const char* term = std::getenv("TERM"); term && *term) {
                const std::string t(term);
                if (t.find("xterm") != std::string::npos ||
                    t.find("screen") != std::string::npos ||
                    t.find("tmux") != std::string::npos ||
                    t.find("cygwin") != std::string::npos) return true;
            }

            // Win11 / 已开启 VT 的现代终端：保留 Unicode 边框。
            // 注意不要先检查 GetConsoleOutputCP()：Win11 的 ANSI 终端完全可能在 936 代码页下
            // 运行，先按代码页判断会把正常环境误判成兼容模式。
            if (IsWindows11OrLater()) return true;
            if (OutputSupportsVt()) return true;
            return false;
        }

        // 旧 conhost（包括 Win10 默认控制台）默认没有 ANSI/VT 能力，
        // 这类环境统一走 ASCII 边框，避免制表符变方块。
        inline bool DetectAsciiFallback() {
            if (const char* v = std::getenv("TERMINAL_BOXES"); v && *v) {
                const std::string s(v);
                if (s == "ascii" || s == "0") return true;
                if (s == "unicode" || s == "utf8" || s == "1") return false;
            }
            return !IsAnsiSupported();
        }

        inline bool AsciiFallback() {
            if (g_asciiFallbackMode == 1) return true;
            if (g_asciiFallbackMode == -1) return false;
            if (!g_asciiFallbackDetected) {
                g_asciiFallbackAutoResult = DetectAsciiFallback();
                g_asciiFallbackDetected = true;
            }
            return g_asciiFallbackAutoResult;
        }

        // 显式覆盖：true=全部 ASCII，false=保留 Unicode；AutoAsciiFallback() 恢复自动
        inline void SetAsciiFallback(const bool enable) { g_asciiFallbackMode = enable ? 1 : -1; }
        inline void AutoAsciiFallback() { g_asciiFallbackMode = 0; }

        // 渲染时取实际字符集：自动兼容模式 / 显式覆盖下统一替换成 Box::Ascii
        inline const BoxChars& Effective(const BoxChars& chars) {
            if (!AsciiFallback() || chars.IsBlank()) return chars;
            return Ascii;
        }

        // 由名称取样式（Json/配置里用）
        inline const BoxChars& FromName(const std::string& name) {
            if (name == "double") return Double;
            if (name == "round")  return Round;
            if (name == "heavy")  return Heavy;
            if (name == "dashed") return Dashed;
            if (name == "ascii")  return Ascii;
            if (name == "blank")  return Blank;
            return Single;
        }
    }

    // 圆点等“宽度不稳定”的装饰字符：兼容模式用窄圆点 · 代替 ●
    namespace Glyphs {
        inline bool CompatMode() { return Box::AsciiFallback(); }

        inline wchar_t Dot() { return CompatMode() ? L'·' : L'●'; }

        inline int DotWidth() { return CompatMode() ? 1 : 2; }

        // UTF-8 版本，方便直接拼进 Text / 状态栏字符串
        inline std::string DotUtf8() {
            return CompatMode() ? std::string("\xC2\xB7") : std::string("\xE2\x97\x8F");
        }
    }
}
