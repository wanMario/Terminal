//
// Terminal / Core / TerminalMouse.hpp
// 鼠标支持：Win32 控制台鼠标事件（ReadConsoleInputW）+ 统一事件泵 + 命中测试路由
//
//  * Mouse::Enable()      打开 ENABLE_MOUSE_INPUT（顺手关掉 QuickEdit，避免点击被"选中"吃掉）
//  * Mouse::PollMouse()   非阻塞取一条鼠标事件（坐标已换算成窗口单元格坐标）
//  * Mouse::PollKey()     与鼠标共用的按键泵：先取队列、再读控制台记录，键盘/鼠标不会互相抢
//  * MouseRouter          命中测试（顶层优先）→ 焦点 / 按压捕获 / 悬停 / 拖拽 / 滚轮
//

#pragma once

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <deque>
#include <string>

#include "../Utils/TerminalChar.hpp"
#include "../Utils/Utf8.hpp"
#include "UI.hpp"
#include "Input.hpp"
#include "TerminalKeyFocus.hpp"

namespace Terminal {

    //======================================================================
    // 事件模型
    //======================================================================
    enum class MouseButton { None = 0, Left, Right, Middle, X1, X2 };

    enum class MouseAction {
        Move = 0,       // 指针移动（按住某键时即拖拽）
        Down,           // 按键按下
        Up,             // 按键抬起
        DoubleClick,
        Wheel,          // 滚轮：wheelDelta > 0 向上
        Leave           // 指针离开窗口（内部合成）
    };

    struct xMouse {
        int x{ 0 };                    // 窗口单元格坐标（0 基）
        int y{ 0 };
        double fx{ 0.0 };              // 亚格坐标（原始输入可用时有效，单位=字符格）
        double fy{ 0.0 };
        bool fine{ false };            // true 表示 fx/fy 来自原始鼠标输入（精度高于单元格）
        MouseButton button{ MouseButton::None };
        MouseAction action{ MouseAction::Move };
        int wheelDelta{ 0 };
        bool shift{ false };
        bool ctrl{ false };
        bool alt{ false };

        [[nodiscard]] bool IsLeft() const { return button == MouseButton::Left; }
        [[nodiscard]] bool IsPress() const { return action == MouseAction::Down || action == MouseAction::DoubleClick; }
        [[nodiscard]] bool IsRelease() const { return action == MouseAction::Up; }

        // 需要亚格精度的元件用它取横坐标：有原始输入就用小数格，否则退回整数格
        [[nodiscard]] double FineX() const { return fine ? fx : static_cast<double>(x); }
        [[nodiscard]] double FineY() const { return fine ? fy : static_cast<double>(y); }

        // 按像素精度换算的比例（元件内部用）
        [[nodiscard]] double Fraction(const int from, const int length) const {
            if (length <= 0) return 0.0;
            const double r = (FineX() - static_cast<double>(from)) / static_cast<double>(length);
            return r < 0.0 ? 0.0 : (r > 1.0 ? 1.0 : r);
        }
    };

    //======================================================================
    // 控制台鼠标开关 + 事件泵
    //======================================================================
    namespace Mouse {

        namespace Detail {
            inline std::deque<xChar>& KeyQueue() {
                static std::deque<xChar> q;
                return q;
            }

            inline bool& EnabledFlag() {
                static bool enabled = false;
                return enabled;
            }

            inline bool& ModeSaved() {
                static bool saved = false;
                return saved;
            }

            inline DWORD& SavedMode() {
                static DWORD mode = 0;
                return mode;
            }

            inline HANDLE Input() {
                static HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
                return h;
            }

            // 控制台记录 -> xChar（只处理按下事件；UTF-8 中文按宽度正确拼字节）
            inline bool KeyRecordToXChar(const KEY_EVENT_RECORD& rec, xChar& out) {
                if (!rec.bKeyDown) return false;
                const DWORD cs = rec.dwControlKeyState;
                Input::SetLastModifiers((cs & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0,
                                        (cs & SHIFT_PRESSED) != 0,
                                        (cs & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) != 0);
                if (rec.uChar.UnicodeChar != 0) {
                    const std::uint32_t cp = static_cast<std::uint32_t>(rec.uChar.UnicodeChar);
                    out = xChar{ Utf8::Encode(cp) };
                    return true;
                }
                // 无字符的特殊键：还原 conio 的「前缀 + 扫描码」序列
                const WORD vk = rec.wVirtualKeyCode;
                const char prefix = (vk >= VK_F1 && vk <= VK_F10) ? 0 : static_cast<char>(0xE0);
                if (rec.wVirtualScanCode == 0) return false;
                out = xChar{ std::string(1, prefix) + std::string(1, static_cast<char>(rec.wVirtualScanCode)) };
                return true;
            }

            inline MouseButton ButtonOf(const DWORD state, const MouseButton fallback) {
                if (state & FROM_LEFT_1ST_BUTTON_PRESSED) return MouseButton::Left;
                if (state & RIGHTMOST_BUTTON_PRESSED) return MouseButton::Right;
                if (state & FROM_LEFT_2ND_BUTTON_PRESSED) return MouseButton::Middle;
                if (state & FROM_LEFT_3RD_BUTTON_PRESSED) return MouseButton::X1;
                if (state & FROM_LEFT_4TH_BUTTON_PRESSED) return MouseButton::X2;
                return fallback;
            }

            inline void FillModifiers(const DWORD ctrlState, xMouse& ev) {
                ev.alt = (ctrlState & LEFT_ALT_PRESSED) || (ctrlState & RIGHT_ALT_PRESSED);
                ev.ctrl = (ctrlState & LEFT_CTRL_PRESSED) || (ctrlState & RIGHT_CTRL_PRESSED);
                ev.shift = (ctrlState & SHIFT_PRESSED) != 0;
            }

            // 控制台缓冲坐标 -> 窗口单元格坐标
            inline void WindowOrigin(int& ox, int& oy, const HANDLE hOut) {
                ox = 0;
                oy = 0;
                CONSOLE_SCREEN_BUFFER_INFO info{};
                if (GetConsoleScreenBufferInfo(hOut, &info)) {
                    ox = info.srWindow.Left;
                    oy = info.srWindow.Top;
                }
            }

            //==============================================================
            // 原始输入（Raw Input）：控制台鼠标事件只精确到「字符格」，
            // 滑杆这类要连续取值的元件靠它拿到「像素级 → 亚格坐标」。
            //
            // 做法：每次收到控制台鼠标事件时，用事件里的准确单元格 + 当时的
            // 屏幕像素位置「定锚」；此后 WM_INPUT 报来的像素位移按字符格尺寸
            // 折算成小数格偏移。这样不需要知道控制台窗口在屏幕上的原点
            // （Windows Terminal / ConPTY 下 GetConsoleWindow 给的是伪窗口，
            // 拿不到可靠原点），且在事件到来时自动校正漂移。
            //
            // 拿不到（无窗口 / 注册失败）时静默退回单元格坐标，不影响其它功能。
            //==============================================================
            namespace Raw {

                struct State {
                    bool started{ false };
                    HWND hwnd{ nullptr };

                    double screenX{ 0.0 };      // 屏幕像素坐标（由 WM_INPUT 增量维护）
                    double screenY{ 0.0 };
                    bool has{ false };

                    // 锚点：最近一次控制台鼠标事件给的单元格 + 当时的屏幕像素
                    bool anchored{ false };
                    double anchorCellX{ 0.0 };
                    double anchorCellY{ 0.0 };
                    double anchorPxX{ 0.0 };
                    double anchorPxY{ 0.0 };

                    // 用连续两次控制台事件「实测」一格有多少像素（自适应 DPI / 终端缩放）
                    bool calibrated{ false };
                    double cellWpx{ 0.0 };
                    double cellHpx{ 0.0 };
                    bool prevValid{ false };
                    double prevCellX{ 0.0 };
                    double prevCellY{ 0.0 };
                    double prevPxX{ 0.0 };
                    double prevPxY{ 0.0 };

                    // 上一次合成 Move 时的亚格坐标（防止重复上报同一位置）
                    bool emitted{ false };
                    double emitCellX{ 0.0 };
                    double emitCellY{ 0.0 };
                };

                inline State& S() {
                    static State s;
                    return s;
                }

                // 同一锚点内：屏幕像素位移 -> 亚格坐标（cellPx = 一个字符格的像素宽/高）
                inline double CellFromPixels(const double baseCell, const double basePx,
                                             const double px, const int cellPx) {
                    if (cellPx <= 0) return baseCell;
                    return baseCell + (px - basePx) / static_cast<double>(cellPx);
                }

                inline LRESULT CALLBACK WndProc(const HWND hwnd, const UINT msg, const WPARAM wParam, const LPARAM lParam) {
                    return DefWindowProcW(hwnd, msg, wParam, lParam);
                }

                // 建立消息窗口并注册原始鼠标输入（RIDEV_INPUTSINK：窗口不在前台也能收到）
                inline bool Start() {
                    State& s = S();
                    if (s.started) return true;

                    static const wchar_t kClass[] = L"TerminalUIRawMouseWindow";
                    HINSTANCE inst = GetModuleHandleW(nullptr);

                    WNDCLASSEXW wc{};
                    wc.cbSize = sizeof(wc);
                    wc.lpfnWndProc = &WndProc;
                    wc.hInstance = inst;
                    wc.lpszClassName = kClass;
                    if (RegisterClassExW(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

                    s.hwnd = CreateWindowExW(0, kClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, inst, nullptr);
                    if (s.hwnd == nullptr) return false;

                    RAWINPUTDEVICE rid{};
                    rid.usUsagePage = 0x01;      // Generic Desktop
                    rid.usUsage = 0x02;          // Mouse
                    rid.dwFlags = RIDEV_INPUTSINK;
                    rid.hwndTarget = s.hwnd;
                    if (!RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
                        DestroyWindow(s.hwnd);
                        s.hwnd = nullptr;
                        return false;
                    }
                    s.started = true;
                    return true;
                }

                inline void Stop() {
                    State& s = S();
                    if (!s.started) return;
                    RAWINPUTDEVICE rid{};
                    rid.usUsagePage = 0x01;
                    rid.usUsage = 0x02;
                    rid.dwFlags = RIDEV_REMOVE;
                    rid.hwndTarget = nullptr;
                    RegisterRawInputDevices(&rid, 1, sizeof(rid));
                    if (s.hwnd != nullptr) {
                        DestroyWindow(s.hwnd);
                        s.hwnd = nullptr;
                    }
                    s.started = false;
                    s.has = false;
                    s.anchored = false;
                    s.emitted = false;
                    s.prevValid = false;
                    s.calibrated = false;
                    s.cellWpx = 0.0;
                    s.cellHpx = 0.0;
                }

                // 抽干窗口队列里的 WM_INPUT，更新屏幕像素坐标
                inline void Pump() {
                    State& s = S();
                    if (!s.started) return;

                    MSG msg{};
                    while (PeekMessageW(&msg, s.hwnd, WM_INPUT, WM_INPUT, PM_REMOVE)) {
                        RAWINPUT ri{};
                        UINT size = sizeof(ri);
                        const UINT got = GetRawInputData(reinterpret_cast<HRAWINPUT>(msg.lParam), RID_INPUT,
                                                         &ri, &size, sizeof(RAWINPUTHEADER));
                        if (got == static_cast<UINT>(-1) || ri.header.dwType != RIM_TYPEMOUSE) continue;

                        const RAWMOUSE& m = ri.data.mouse;
                        if ((m.usFlags & MOUSE_MOVE_ABSOLUTE) != 0) {
                            // 绝对报文各家归一化口径不一致（虚拟桌面 / 主显示器 / 设备坐标），
                            // 直接用系统光标位置当真相，避免偏移
                            POINT p{ 0, 0 };
                            if (GetCursorPos(&p)) {
                                s.screenX = static_cast<double>(p.x);
                                s.screenY = static_cast<double>(p.y);
                                s.has = true;
                            }
                        } else if (m.lLastX != 0 || m.lLastY != 0) {
                            if (!s.has) {
                                POINT p{ 0, 0 };
                                if (GetCursorPos(&p)) {
                                    s.screenX = static_cast<double>(p.x);
                                    s.screenY = static_cast<double>(p.y);
                                    s.has = true;
                                }
                            }
                            s.screenX += static_cast<double>(m.lLastX);
                            s.screenY += static_cast<double>(m.lLastY);
                        }
                    }
                }

                // 一个字符格的像素宽高（拿不到就用 8x16 兜底）
                inline void CellPixels(const HANDLE hOut, int& cellW, int& cellH) {
                    cellW = 8;
                    cellH = 16;
                    CONSOLE_FONT_INFOEX fi{};
                    fi.cbSize = sizeof(fi);
                    if (GetCurrentConsoleFontEx(hOut, FALSE, &fi) && fi.dwFontSize.X > 0 && fi.dwFontSize.Y > 0) {
                        cellW = fi.dwFontSize.X;
                        cellH = fi.dwFontSize.Y;
                    }
                }

                // 收到控制台鼠标事件：用它的准确单元格重新定锚，并顺便实测格宽
                inline void Anchor(const xMouse& ev) {
                    State& s = S();
                    if (!s.has) {
                        s.anchored = false;
                        return;
                    }

                    // 与上一条事件比较：格变了 + 像素也走了 → 一格 = 多少像素
                    if (s.prevValid) {
                        const double dcx = std::fabs(static_cast<double>(ev.x) - s.prevCellX);
                        const double dcy = std::fabs(static_cast<double>(ev.y) - s.prevCellY);
                        const double dpx = std::fabs(s.screenX - s.prevPxX);
                        const double dpy = std::fabs(s.screenY - s.prevPxY);
                        if (dcx >= 1.0 && dpx >= 2.0) {
                            const double est = dpx / dcx;
                            if (est >= 3.0 && est <= 64.0) {
                                s.cellWpx = s.calibrated ? s.cellWpx * 0.8 + est * 0.2 : est;
                            }
                        }
                        if (dcy >= 1.0 && dpy >= 2.0) {
                            const double est = dpy / dcy;
                            if (est >= 6.0 && est <= 96.0) {
                                s.cellHpx = s.calibrated ? s.cellHpx * 0.8 + est * 0.2 : est;
                            }
                        }
                        if (s.cellWpx > 0.0 && s.cellHpx > 0.0) s.calibrated = true;
                    }

                    s.prevValid = true;
                    s.prevCellX = static_cast<double>(ev.x);
                    s.prevCellY = static_cast<double>(ev.y);
                    s.prevPxX = s.screenX;
                    s.prevPxY = s.screenY;

                    s.anchorCellX = static_cast<double>(ev.x);
                    s.anchorCellY = static_cast<double>(ev.y);
                    s.anchorPxX = s.screenX;
                    s.anchorPxY = s.screenY;
                    s.anchored = true;
                    s.emitted = true;          // 事件本身已经上报过这个位置
                    s.emitCellX = s.anchorCellX;
                    s.emitCellY = s.anchorCellY;
                }

                // 给刚读到的控制台事件定锚（事件本身仍用整数格）
                inline void Attach(xMouse& ev) {
                    Pump();
                    Anchor(ev);
                }

                // 亚格坐标 -> 合成一条 Move：让滑杆在「格内移动」时也能连续更新，
                // 否则控制台只在跨格时才发事件，精度仍然是一格一档。
                inline bool PollFineMove(xMouse& ev, const HANDLE hOut) {
                    Pump();
                    State& s = S();
                    if (!s.started || !s.has || !s.anchored) return false;

                    int cellW = 8, cellH = 16;
                    if (s.calibrated) {
                        cellW = static_cast<int>(std::lround(s.cellWpx));
                        cellH = static_cast<int>(std::lround(s.cellHpx));
                        if (cellW <= 0) cellW = 8;
                        if (cellH <= 0) cellH = 16;
                    } else {
                        CellPixels(hOut, cellW, cellH);
                    }

                    const double cx = CellFromPixels(s.anchorCellX, s.anchorPxX, s.screenX, cellW);
                    const double cy = CellFromPixels(s.anchorCellY, s.anchorPxY, s.screenY, cellH);

                    // 注意：这里不再「超出 1.5 格就丢弃锚点」。
                    // 拖拽滑出台面时控制台不会再发 MOUSE_MOVED，只剩 WM_INPUT 的增量；
                    // 丢掉锚点会让拖拽直接停住（用户看到的就是“滑出范围就不动了”）。
                    if (s.emitted && std::fabs(cx - s.emitCellX) < 0.01 && std::fabs(cy - s.emitCellY) < 0.01) {
                        return false;
                    }

                    s.emitted = true;
                    s.emitCellX = cx;
                    s.emitCellY = cy;

                    ev = xMouse{};
                    ev.x = static_cast<int>(std::lround(cx));
                    ev.y = static_cast<int>(std::lround(cy));
                    ev.fx = cx;
                    ev.fy = cy;
                    ev.fine = true;
                    ev.action = MouseAction::Move;
                    return true;
                }
            } // namespace Raw

            inline bool MouseRecordToEvent(const MOUSE_EVENT_RECORD& rec, xMouse& ev,
                                           const DWORD prevButtons, const HANDLE hOut) {
                int ox = 0, oy = 0;
                WindowOrigin(ox, oy, hOut);

                ev.x = rec.dwMousePosition.X - ox;
                ev.y = rec.dwMousePosition.Y - oy;
                ev.fx = static_cast<double>(ev.x);   // 默认退回单元格坐标，拿得到原始输入再覆盖
                ev.fy = static_cast<double>(ev.y);
                ev.fine = false;
                FillModifiers(rec.dwControlKeyState, ev);
                ev.wheelDelta = 0;

                const DWORD flags = rec.dwEventFlags;
                if (flags & MOUSE_WHEELED) {
                    ev.action = MouseAction::Wheel;
                    ev.button = MouseButton::None;
                    ev.wheelDelta = static_cast<int>(static_cast<short>(HIWORD(rec.dwButtonState)));
                    return true;
                }
                if (flags & MOUSE_MOVED) {
                    ev.action = MouseAction::Move;
                    ev.button = ButtonOf(rec.dwButtonState, MouseButton::None);
                    return true;
                }
                if (flags & DOUBLE_CLICK) {
                    ev.action = MouseAction::DoubleClick;
                    ev.button = ButtonOf(rec.dwButtonState, MouseButton::Left);
                    return true;
                }
                // 普通按下 / 抬起：靠前后按钮位变化判断
                ev.action = (rec.dwButtonState != 0) ? MouseAction::Down : MouseAction::Up;
                ev.button = ButtonOf(rec.dwButtonState, ButtonOf(prevButtons, MouseButton::Left));
                return true;
            }
        } // namespace Detail

        /**
         * @brief 打开鼠标输入（并关闭 QuickEdit，保证点击不会被控制台"选择文本"截走）。
         */
        inline bool Enable(const HANDLE hIn = Detail::Input()) {
            DWORD mode = 0;
            if (!GetConsoleMode(hIn, &mode)) return false;
            if (!Detail::ModeSaved()) {
                Detail::SavedMode() = mode;
                Detail::ModeSaved() = true;
            }
            mode |= ENABLE_MOUSE_INPUT | ENABLE_EXTENDED_FLAGS;
            mode &= ~static_cast<DWORD>(ENABLE_QUICK_EDIT_MODE);
            // 关掉 processed input：否则 Ctrl+C / Ctrl+S 会被系统截走，
            // 输入框拿不到 3（Ctrl+C）这类控制码，复制粘贴快捷键就失效。
            mode &= ~static_cast<DWORD>(ENABLE_PROCESSED_INPUT);
            if (!SetConsoleMode(hIn, mode)) return false;
            Detail::EnabledFlag() = true;
            // 原始输入是「可选的增强」：注册失败就继续用单元格坐标，不影响鼠标功能
            Detail::Raw::Start();
            return true;
        }

        /** @brief 关闭鼠标输入并恢复原控制台模式。 */
        inline bool Disable(const HANDLE hIn = Detail::Input()) {
            if (!Detail::EnabledFlag()) return false;
            Detail::Raw::Stop();
            DWORD mode = 0;
            if (GetConsoleMode(hIn, &mode)) {
                mode &= ~static_cast<DWORD>(ENABLE_MOUSE_INPUT);
                if (Detail::ModeSaved()) mode = Detail::SavedMode();
                SetConsoleMode(hIn, mode);
            }
            Detail::EnabledFlag() = false;
            return true;
        }

        [[nodiscard]] inline bool IsEnabled() { return Detail::EnabledFlag(); }

        /** @brief 原始输入是否可用（可用时鼠标事件带 fx/fy 亚格坐标）。 */
        [[nodiscard]] inline bool HasFinePosition() { return Detail::Raw::S().started && Detail::Raw::S().has; }

        /** @brief 手动打开/关闭原始输入（Enable 已自动尝试）。 */
        inline bool EnableFinePosition() { return Detail::Raw::Start(); }
        inline void DisableFinePosition() { Detail::Raw::Stop(); }

        /**
         * @brief 非阻塞取一条鼠标事件。期间读到的键盘事件会被放进队列，由 PollKey 取走。
         */
        inline bool PollMouse(xMouse& ev, const HANDLE hIn = Detail::Input(),
                              const HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE)) {
            if (!Detail::EnabledFlag()) return false;

            // 原始输入报来的「格内位移」先合成一条 Move：控制台只在跨格时才发事件，
            // 没有这一步，亚格坐标根本到不了上层，精度还是一格一档。
            xMouse fine;
            if (Detail::Raw::PollFineMove(fine, hOut)) {
                ev = fine;
                return true;
            }

            DWORD count = 0;
            while (GetNumberOfConsoleInputEvents(hIn, &count) && count > 0) {
                INPUT_RECORD rec{};
                DWORD got = 0;
                if (!ReadConsoleInputW(hIn, &rec, 1, &got) || got == 0) return false;

                if (rec.EventType == KEY_EVENT) {
                    xChar key;
                    if (Detail::KeyRecordToXChar(rec.Event.KeyEvent, key)) Detail::KeyQueue().push_back(key);
                    continue;
                }
                if (rec.EventType == MOUSE_EVENT) {
                    static DWORD prevButtons = 0;
                    xMouse out;
                    bool ok = Detail::MouseRecordToEvent(rec.Event.MouseEvent, out, prevButtons, hOut);
                    prevButtons = rec.Event.MouseEvent.dwButtonState;
                    if (ok) {
                        Detail::Raw::Attach(out);   // 用事件的准确单元格重新定锚
                        ev = out;
                        return true;
                    }
                }
                // 窗口/缓冲/焦点等其它记录忽略
            }
            return false;
        }

        /**
         * @brief 按键泵（鼠标模式下使用）：先取内部队列，队列空则回落到 conio。
         */
        inline bool PollKey(xChar& key, const HANDLE hIn = Detail::Input(),
                            const HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE)) {
            if (!Detail::EnabledFlag()) return Input::PollKey(key);
            if (!Detail::KeyQueue().empty()) {
                key = Detail::KeyQueue().front();
                Detail::KeyQueue().pop_front();
                return true;
            }
            // 借 PollMouse 把键盘记录搬进队列
            xMouse ignored;
            PollMouse(ignored, hIn, hOut);
            if (!Detail::KeyQueue().empty()) {
                key = Detail::KeyQueue().front();
                Detail::KeyQueue().pop_front();
                return true;
            }
            return false;
        }

        inline void FlushPending() {
            Detail::KeyQueue().clear();
            Input::FlushPending();
        }
    } // namespace Mouse

    //======================================================================
    // 鼠标路由：命中测试 + 悬停 / 捕获 / 焦点
    //======================================================================
    class MouseRouter {
    public:
        static MouseRouter& Instance() {
            static MouseRouter router;
            return router;
        }

        void SetRoot(UI* root) { m_root = root; m_hover = nullptr; m_capture = nullptr; }

        void Clear() { m_root = nullptr; m_hover = nullptr; m_capture = nullptr; }

        [[nodiscard]] UI* Root() const { return m_root; }
        [[nodiscard]] UI* Hover() const { return m_hover; }
        [[nodiscard]] UI* Capture() const { return m_capture; }

        // 命中测试：可见 + 落在矩形内，子元素优先（后加入的更靠上）
        static UI* HitTest(UI* root, const int x, const int y) {
            if (root == nullptr || !root->isVisible || !root->HitTest(x, y)) return nullptr;
            for (auto it = root->children.rbegin(); it != root->children.rend(); ++it) {
                if (UI* hit = HitTest(it->get(), x, y)) return hit;
            }
            return root;
        }

        /**
         * @brief 分发一条鼠标事件。
         * @return 有元件消费了事件
         */
        bool Dispatch(const xMouse& ev) {
            if (m_root == nullptr) return false;

            UI* target = HitTest(m_root, ev.x, ev.y);

            if (ev.action == MouseAction::Move) {
                if (target != m_hover) {
                    if (m_hover != nullptr) {
                        m_hover->OnMouseLeave();
                        m_hover->Invalidate();
                    }
                    m_hover = target;
                    if (m_hover != nullptr) {
                        m_hover->OnMouseEnter();
                        m_hover->Invalidate();
                    }
                }
                if (m_capture != nullptr) return Bubble(m_capture, ev);
                return target != nullptr && Bubble(target, ev);
            }

            if (ev.IsPress()) {
                if (target == nullptr) return false;
                if (target->IsFocusable()) FocusManager::Instance().Focus(target);
                m_capture = target;
                return Bubble(target, ev);
            }

            if (ev.action == MouseAction::Up) {
                UI* node = m_capture != nullptr ? m_capture : target;
                m_capture = nullptr;
                return node != nullptr && Bubble(node, ev);
            }

            if (ev.action == MouseAction::Wheel) {
                if (m_capture != nullptr) return Bubble(m_capture, ev);
                return target != nullptr && Bubble(target, ev);
            }
            return false;
        }

    private:
        MouseRouter() = default;

        // 从命中元件往上冒泡，直到有人消费（容器也能接住事件）
        static bool Bubble(UI* node, const xMouse& ev) {
            for (UI* n = node; n != nullptr; n = n->parent) {
                if (n->OnMouse(ev)) return true;
            }
            return false;
        }

        UI* m_root{ nullptr };
        UI* m_hover{ nullptr };
        UI* m_capture{ nullptr };
    };

} // namespace Terminal
