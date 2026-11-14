//
// Terminal / Core / Input.hpp
// 键盘输入泵：非阻塞轮询 + UTF-8 多字节拼装（中文输入也能进输入框）
//

#pragma once

#include <conio.h>
#include <windows.h>

#include "../Utils/TerminalChar.hpp"

namespace Terminal {
    namespace Input {

        // 一次按键附带的修饰键状态（控制台记录里有，conio 只能靠控制码推断）
        struct KeyModifiers {
            bool ctrl{ false };
            bool shift{ false };
            bool alt{ false };
        };

        inline KeyModifiers& LastModifiers() {
            static KeyModifiers m;
            return m;
        }

        inline void SetLastModifiers(const bool ctrl, const bool shift, const bool alt) {
            LastModifiers() = KeyModifiers{ ctrl, shift, alt };
        }

        inline void ClearModifiers() { LastModifiers() = KeyModifiers{}; }

        inline bool KeyAvailable() {
            return _kbhit() != 0;
        }

        // 等待一个原始字节，timeoutMs < 0 表示一直等
        inline bool WaitByte(char& out, const int timeoutMs) {
            if (timeoutMs < 0) {
                out = static_cast<char>(_getch());
                return true;
            }
            const DWORD begin = GetTickCount();
            while (!_kbhit()) {
                if (static_cast<int>(GetTickCount() - begin) >= timeoutMs) return false;
                Sleep(1);
            }
            out = static_cast<char>(_getch());
            return true;
        }

        /**
         * @brief 读取一次按键。
         *        - 普通 ASCII：单字节
         *        - 功能键：-32/0 前缀 + 扫描码
         *        - UTF-8：自动把后续 1~3 个字节拼成一个 xChar
         */
        inline bool ReadKey(xChar& key, const int timeoutMs = 0) {
            ClearModifiers();   // conio 拿不到修饰键，Ctrl 只能靠控制码
            char first{};
            if (!WaitByte(first, timeoutMs)) return false;

            std::string seq{ first };
            const int c = static_cast<int>(static_cast<unsigned char>(first));

            if (c == 0xE0 || c == 0) { // 扩展键前缀（_getch 里是 -32 / 0）
                char second{};
                if (WaitByte(second, 20)) seq.push_back(second);
            } else if (c >= 0xC0) {
                // UTF-8 多字节：起始字节的高位决定长度
                int need = 0;
                if ((c & 0xE0) == 0xC0) need = 1;
                else if ((c & 0xF0) == 0xE0) need = 2;
                else if ((c & 0xF8) == 0xF0) need = 3;
                for (int i = 0; i < need; ++i) {
                    char next{};
                    if (!WaitByte(next, 20)) break;
                    seq.push_back(next);
                }
            }

            key = xChar{ seq };
            return true;
        }

        // 非阻塞：没有按键立即返回 false
        inline bool PollKey(xChar& key) { return ReadKey(key, 0); }

        // 清空挂起的输入
        inline void FlushPending() {
            xChar tmp;
            int guard = 0;
            while (_kbhit() && guard++ < 4096) ReadKey(tmp, 0);
        }
    }
}
