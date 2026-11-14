//
// Terminal / Components / TerminalWindow.hpp
// 轻量弹窗容器：带标题栏、边框、关闭按钮，内容区按 Column 布局。
// 标题栏按住左键可拖动；关闭动作通过消息队列（"close"）处理。
//

#pragma once

#include <algorithm>
#include <string>

#include "../Core/Layout.hpp"
#include "../Core/TerminalMessage.hpp"
#include "../Core/TerminalMouse.hpp"
#include "../Core/TerminalBuffer.hpp"
#include "../Utils/BoxChars.hpp"

namespace Terminal {

    class Window : public UI {
    public:
        std::string title{ "Window" };
        bool frameVisible{ true };
        bool closable{ true };
        bool draggable{ true };
        bool open{ true };
        bool freePosition{ false };
        Position windowPos{ 0, 0 };
        BoxChars chars{ Box::Round };
        Color borderColor{ Theme::Current().border };
        Color titleColor{ Theme::Current().accent };
        Color titleBg{ Theme::Current().surfaceAlt };

        Window() {
            signature = "UIWindow";
            style = Style(Theme::Current().text, Theme::Current().surface);
            style.opaque = true;
            layout.width = 40;
            layout.height = 10;
            layout.flow = Flow::Column;
            layout.paddingLeft = 1;
            layout.paddingTop = 2;
            layout.paddingRight = 1;
            layout.paddingBottom = 1;
            focusable = true;
        }

        Window(const std::string& text, const int w = 40, const int h = 10) : Window() {
            title = text;
            layout.width = w;
            layout.height = h;
        }

        Window& SetTitle(const std::string& text) { title = text; Invalidate(); return *this; }
        Window& SetFrame(const BoxChars& box) { chars = box; Invalidate(); return *this; }
        Window& SetBorderColor(const Color& c) { borderColor = c; Invalidate(); return *this; }
        Window& SetTitleColors(const Color& fg, const Color& bg) {
            titleColor = fg;
            titleBg = bg;
            Invalidate();
            return *this;
        }
        Window& SetDraggable(const bool on) { draggable = on; return *this; }
        Window& SetClosable(const bool on) { closable = on; return *this; }
        Window& MoveTo(const int x, const int y) {
            windowPos = Position{ x, y };
            freePosition = true;
            InvalidateLayout();
            return *this;
        }
        Window& Show(const bool on = true) {
            open = on;
            isVisible = on;
            InvalidateLayout();
            return *this;
        }

        [[nodiscard]] Bounds ContentBounds() const override {
            Bounds inner = bounds;
            if (frameVisible) inner = Bounds{ inner.Left() + 1, inner.Top() + 2, std::max(inner.width() - 2, 0), std::max(inner.height() - 3, 0) };
            return inner;
        }

        [[nodiscard]] int MeasureWidth() const override {
            if (layout.width >= 0) return layout.width;
            int cw = 0, ch = 0;
            Layout::MeasureChildren(*this, cw, ch);
            return cw + 2;
        }
        [[nodiscard]] int MeasureHeight() const override {
            if (layout.height >= 0) return layout.height;
            int cw = 0, ch = 0;
            Layout::MeasureChildren(*this, cw, ch);
            return ch + 3;
        }

        void OnLayout() override { Layout::ArrangeChildren(*this); }

        void Arrange(const Bounds& available) override {
            UI::Arrange(available);
            if (!freePosition) return;
            int x = std::clamp(windowPos.x, available.Left(), std::max(available.Right() - Width(), available.Left()));
            int y = std::clamp(windowPos.y, available.Top(), std::max(available.Bottom() - Height(), available.Top()));
            SetBounds(Bounds{ x, y, Width(), Height() });
        }

        bool OnMessage(const Message& msg) override {
            if (msg.type == "close") {
                if (!closable) return true;
                open = false;
                isVisible = false;
                Invalidate();
                Message done;
                done.sender = this;
                done.target = this;
                done.type = "closed";
                done.payload = MsgJson::Object();
                done.payload["title"] = Json(title);
                MessageQueue::Post(done);
                return true;
            }
            if (msg.type == "open") {
                open = true;
                isVisible = true;
                InvalidateLayout();
                return true;
            }
            if (msg.type == "move") {
                int x = windowPos.x, y = windowPos.y;
                if (msg.payload.IsObject()) {
                    if (msg.payload.contains_key("x")) {
                        const Json v = msg.payload.get("x");
                        if (v.IsNumber()) x = static_cast<int>(v.GetNumber());
                    }
                    if (msg.payload.contains_key("y")) {
                        const Json v = msg.payload.get("y");
                        if (v.IsNumber()) y = static_cast<int>(v.GetNumber());
                    }
                }
                MoveTo(x, y);
                return true;
            }
            if (msg.type == "title") {
                if (msg.payload.IsObject() && msg.payload.contains_key("value")) {
                    const Json v = msg.payload.get("value");
                    if (v.IsString()) SetTitle(v.GetString());
                }
                return true;
            }
            return UI::OnMessage(msg);
        }

        bool OnMouse(const xMouse& ev) override {
            if (!open || !isVisible) return false;
            const bool inTitle = ev.y == Top() + 1;
            const bool inClose = closable && inTitle && ev.x >= Right() - 4 && ev.x < Right() - 1;
            if (ev.action == MouseAction::Down && ev.IsLeft()) {
                if (inClose) {
                    MessageQueue::PostFrom(this, this, "close");
                    return true;
                }
                if (draggable && inTitle) {
                    m_dragging = true;
                    m_dragDX = ev.x - Left();
                    m_dragDY = ev.y - Top();
                    return true;
                }
            }
            if (ev.action == MouseAction::Up) {
                m_dragging = false;
                return false;
            }
            if (m_dragging && ev.action == MouseAction::Move) {
                MoveTo(ev.x - m_dragDX, ev.y - m_dragDY);
                SetBounds(Bounds{ windowPos.x, windowPos.y, Width(), Height() });
                OnLayout();
                Invalidate();
                return true;
            }
            return false;
        }

        void OnRender(ConsoleBackBuffer& buf) override {
            if (!open || bounds.IsEmpty()) return;
            const Color bg = EffectiveBackground();
            buf.FillStyle(bounds, Style(style.fg, bg));
            if (frameVisible) {
                buf.DrawBox(bounds, Box::Effective(chars), Style(borderColor, bg));
                const int ty = Top() + 1;
                for (int x = Left() + 1; x < Right() - 1; ++x)
                    buf.SetCell(x, ty, L' ', Style(titleColor, titleBg));
                buf.WriteText(Left() + 2, ty, Utf8::Truncate(title, std::max(Width() - 4, 0)),
                              Style(titleColor, titleBg).SetBold(), std::max(Width() - 4, 0));
                if (closable) {
                    buf.WriteText(Right() - 4, ty, "[x]", Style(Theme::Current().danger, titleBg), 3);
                }
                for (int x = Left() + 1; x < Right() - 1; ++x)
                    buf.SetCell(x, Top() + 2, L'─', Style(borderColor, bg));
            }
        }

    private:
        bool m_dragging{ false };
        int m_dragDX{ 0 };
        int m_dragDY{ 0 };
    };

    using UIDialog = Window;
    using UIPopup = Window;

} // namespace Terminal
