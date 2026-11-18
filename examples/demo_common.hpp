//
// Terminal / examples / demo_common.hpp
// 多个 demo 共用的主循环 / 单帧渲染 / 布局转储 / 性能基准。
// 每个 demo 只需要提供 BuildUI()，不再各自复制一份交互循环。
//

#pragma once

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "../Terminal.hpp"

#include "../Core/UI.hpp"
#include "../Core/Layout.hpp"
#include "../Core/TerminalBuffer.hpp"
#include "../Core/TerminalKeyFocus.hpp"
#include "../Core/Input.hpp"
#include "../Core/TerminalMouse.hpp"

#include "../Utils/Color.hpp"
#include "../Utils/Theme.hpp"
#include "../Utils/Utf8.hpp"

namespace Terminal::Demo {

    inline int g_colorMode = 0;          // 0=自动  1=强制真彩  2=强制 16 色
    inline bool g_quitRequested = false;

    class DemoRoot : public Panel {
    public:
        using Panel::Panel;
        bool OnMessage(const Message& msg) override {
            if (msg.type == "quit") { g_quitRequested = true; return true; }
            return Panel::OnMessage(msg);
        }
    };

    inline std::string AssetPath(const std::string& name) {
        namespace fs = std::filesystem;
        const fs::path headerDir = fs::path(__FILE__).parent_path();
        const fs::path nearSource = headerDir.parent_path() / "assets" / name;
        if (fs::exists(nearSource)) return nearSource.string();
        const fs::path cwd1 = fs::path("assets") / name;
        if (fs::exists(cwd1)) return cwd1.string();
        const fs::path cwd2 = fs::path("..") / "assets" / name;
        if (fs::exists(cwd2)) return cwd2.string();
        return nearSource.string();
    }

    inline std::filesystem::path U8Path(const std::string& text) {
        const std::u8string u8(reinterpret_cast<const char8_t*>(text.data()), text.size());
        return std::filesystem::path(u8);
    }

    inline std::string U8Text(const std::filesystem::path& path) {
        const auto text = path.u8string();
        return std::string(reinterpret_cast<const char*>(text.data()), text.size());
    }

    inline void DumpTree(const UI& ui, const int depth) {
        const auto& b = ui.GetBounds();
        std::string pad(static_cast<size_t>(depth * 2), ' ');
        std::printf("%s%s bounds=(%d,%d %dx%d) pref=%dx%d\n", pad.c_str(), ui.signature.c_str(),
            b.Left(), b.Top(), b.width(), b.height(), ui.PreferredWidth(), ui.PreferredHeight());
        for (const auto& c : ui.children) if (c) DumpTree(*c, depth + 1);
    }

    inline int RunDump(const std::shared_ptr<Panel>& root) {
        ConsoleBackBuffer buf(80, 24);
        root->Arrange(Rect2Di{ 0, 0, buf.Width(), buf.Height() });
        root->Mount();
        DumpTree(*root, 0);
        return 0;
    }

    inline int RunFrame(std::shared_ptr<Panel> root, const int width = 80, const int height = 30) {
        ConsoleBackBuffer buf(width, height);
        buf.SetTrueColor(true);
        root->Arrange(Rect2Di{ 0, 0, buf.Width(), buf.Height() });
        root->Mount();
        FocusManager::Instance().SetRoot(root.get());
        FocusManager::Instance().FocusFirst();

        buf.Clear(Style(Theme::Current().text, Theme::Current().background));
        root->Render(buf);
        std::cout << buf.ToAnsiText();
        FocusManager::Instance().Clear();
        return 0;
    }

    inline int RunInteractive(std::shared_ptr<Panel> root) {
        SetStdCP();
        RemoveQuickEdit();
        CursorVisible(false);

        const bool wantTrueColor = (g_colorMode == 1) ? true
            : (g_colorMode == 2) ? false
            : ConsoleBackBuffer::SupportsTrueColor();

        ConsoleBackBuffer buf = ConsoleBackBuffer::FromConsole();
        buf.SetTrueColor(wantTrueColor);

        root->Arrange(Rect2Di{ 0, 0, buf.Width(), buf.Height() });
        root->Mount();

        auto& focus = FocusManager::Instance();
        focus.SetRoot(root.get());
        focus.FocusFirst();

        Mouse::Enable();
        MouseRouter::Instance().SetRoot(root.get());

        bool running = true;
        g_quitRequested = false;
        MessageQueue::Clear();

        while (running) {
            if (MessageQueue::ProcessAll() > 0) root->Invalidate();
            if (g_quitRequested) { running = false; break; }

            int w = 0, h = 0;
            if (ConsoleBackBuffer::QueryConsoleSize(w, h) && (w != buf.Width() || h != buf.Height())) {
                buf.Resize(w, h);
                buf.SetTrueColor(wantTrueColor);
                root->Arrange(Rect2Di{ 0, 0, w, h });
                root->Invalidate();
            }

            if (root->layoutDirty) {
                root->Arrange(Rect2Di{ 0, 0, buf.Width(), buf.Height() });
                root->Invalidate();
            }

            // 鼠标事件优先；捕获中的 Move 合并，避免高 DPI 拖动把队列抽干。
            xMouse mouse;
            xMouse pendingDrag{};
            bool hasPendingDrag = false;
            int mouseBudget = 64;
            while (mouseBudget-- > 0 && Mouse::PollMouse(mouse)) {
                if (mouse.action == MouseAction::Move &&
                    MouseRouter::Instance().Capture() != nullptr) {
                    pendingDrag = mouse;
                    hasPendingDrag = true;
                    continue;
                }
                if (hasPendingDrag) {
                    MouseRouter::Instance().Dispatch(pendingDrag);
                    hasPendingDrag = false;
                }
                MouseRouter::Instance().Dispatch(mouse);
            }
            if (hasPendingDrag) MouseRouter::Instance().Dispatch(pendingDrag);

            if (MouseRouter::Instance().Capture() != nullptr) root->Invalidate();

            xChar key;
            while (Mouse::PollKey(key)) {
                if (IsKey(key, SKey::Esc)) {
                    running = false;
                    break;
                }
                if (focus.Dispatch(key)) continue;
                if (key.IsPrintableAscii() && (key.Ascii() == 'q' || key.Ascii() == 'Q')) running = false;
            }

            if (root->layoutDirty) {
                root->Arrange(Rect2Di{ 0, 0, buf.Width(), buf.Height() });
                root->Invalidate();
            }

            root->Update();
            MouseRouter::Instance().ApplyCursor();
            if (root->IsDirty()) {
                buf.Clear(Style(Theme::Current().text, Theme::Current().background));
                root->Render(buf);
                buf.FlushAuto();
            }

            Sleep(30);
        }

        focus.Clear();
        MouseRouter::Instance().Clear();
        ApplyMouseCursor(MouseCursor::Arrow);
        Mouse::Disable();
        CursorVisible(true);
        SetCursorPosition(0, buf.Height());
        return 0;
    }

    inline int RunBench(std::shared_ptr<Panel> root, const int frames) {
        ConsoleBackBuffer buf(120, 40);
        buf.SetTrueColor(true);
        root->Arrange(Rect2Di{ 0, 0, buf.Width(), buf.Height() });
        root->Mount();
        FocusManager::Instance().SetRoot(root.get());
        FocusManager::Instance().FocusFirst();

        const auto t0 = std::chrono::high_resolution_clock::now();
        int rendered = 0;
        for (int i = 0; i < frames; ++i) {
            if (root->IsDirty()) {
                buf.Clear(Style(Theme::Current().text, Theme::Current().background));
                root->Render(buf);
                ++rendered;
            }
        }
        const auto t1 = std::chrono::high_resolution_clock::now();
        const double msIdle = std::chrono::duration<double, std::milli>(t1 - t0).count();

        const auto t4 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < frames; ++i) {
            root->Invalidate();
            buf.Clear(Style(Theme::Current().text, Theme::Current().background));
            root->Render(buf);
        }
        const auto t5 = std::chrono::high_resolution_clock::now();
        const double msFull = std::chrono::duration<double, std::milli>(t5 - t4).count();

        const auto t2 = std::chrono::high_resolution_clock::now();
        std::size_t bytes = 0;
        for (int i = 0; i < frames; ++i) bytes += buf.ToAnsiText().size();
        const auto t3 = std::chrono::high_resolution_clock::now();
        const double msAnsi = std::chrono::duration<double, std::milli>(t3 - t2).count();

        std::cout << "[bench] 画面 " << buf.Width() << "x" << buf.Height() << "  frames=" << frames << "\n"
            << "[bench] 脏标记路径 : " << (msIdle / frames) << " ms/frame（实际重绘 " << rendered << " 次）\n"
            << "[bench] 全量重绘   : " << (msFull / frames) << " ms/frame（布局+渲染+缓冲写入）\n"
            << "[bench] ANSI 生成  : " << (msAnsi / frames) << " ms/frame, "
            << (bytes / static_cast<std::size_t>(frames)) << " bytes/frame\n";
        FocusManager::Instance().Clear();
        return 0;
    }

    // 统一参数：--frame / --dump / --bench N / --truecolor / --mono / --help。
    // build 在交互模式启动时调用一次；单帧/转储/基准也复用它。
    inline int RunMain(const int argc, char** argv, const std::shared_ptr<Panel>& root,
        const std::string& usage, const bool frameDefault = false) {
        bool frame = frameDefault;
        bool dump = false;
        int benchFrames = 0;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--frame") { frame = true; continue; }
            if (arg == "--dump") { dump = true; continue; }
            if (arg == "--truecolor" || arg == "--24bit") { g_colorMode = 1; continue; }
            if (arg == "--mono" || arg == "--16color") { g_colorMode = 2; continue; }
            if (arg == "--bench") {
                benchFrames = (i + 1 < argc) ? std::atoi(argv[++i]) : 200;
                continue;
            }
            if (arg == "--help" || arg == "-h") {
                std::cout << usage;
                return 0;
            }
        }
        if (!root) {
            std::cerr << "demo root is null\n";
            return 1;
        }
        if (dump) return RunDump(root);
        if (benchFrames > 0) return RunBench(root, benchFrames);
        if (frame) return RunFrame(root);
        return RunInteractive(root);
    }

} // namespace Terminal::Demo
