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
        // 8 个缩放方向：四条边 + 四个角（用位掩码组合）
        enum ResizeEdge : unsigned {
            ResizeNone = 0,
            ResizeLeft = 1u << 0,
            ResizeRight = 1u << 1,
            ResizeTop = 1u << 2,
            ResizeBottom = 1u << 3,
            ResizeTopLeft = ResizeLeft | ResizeTop,
            ResizeTopRight = ResizeRight | ResizeTop,
            ResizeBottomLeft = ResizeLeft | ResizeBottom,
            ResizeBottomRight = ResizeRight | ResizeBottom
        };

        std::string title{ "Window" };
        bool frameVisible{ true };
        bool closable{ true };
        bool draggable{ true };
        bool resizable{ false };
        bool open{ true };
        int minWidth{ 10 };
        int minHeight{ 6 };
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
        // 是否允许按住 Ctrl 拖边框缩放；默认关闭，避免普通弹窗被误缩放。
        Window& SetResizable(const bool on) {
            resizable = on;
            if (!on) {
                m_resizing = false;
                m_resizeEdge = ResizeNone;
                m_ctrlHint = false;
            }
            Invalidate();
            return *this;
        }
        Window& SetMinSize(const int w, const int h) {
            minWidth = std::max(w, 4);
            minHeight = std::max(h, 4);
            return *this;
        }
        Window& MoveTo(const int x, const int y) {
            windowPos = Position{ x, y };
            freePosition = true;
            if (parent != nullptr || !bounds.IsEmpty()) {
                // 拖动只改位置：先同步 bounds 供命中测试使用，子元素布局推迟到
                // 下一帧 OnUpdate() 做一次。否则每来一条 Move 都要 Arrange 全部子元素。
                SetBounds(Bounds{ x, y, Width(), Height() });
                m_layoutPending = true;
                Invalidate();       // SetBounds 只标自身 dirty，这里冒泡到根，主循环才会重绘
            } else {
                InvalidateLayout();
            }
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
            if (frameVisible) inner = Bounds{ inner.Left() + 1, inner.Top() + 3, std::max(inner.width() - 2, 0), std::max(inner.height() - 4, 0) };
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
            return ch + 4;
        }

        void OnLayout() override {
            m_layoutPending = false;
            layoutDirty = false;
            Layout::ArrangeChildren(*this);
        }

        void OnUpdate() override {
            // 只按鼠标移动判断 Ctrl 时，鼠标停在边框上再按 Ctrl 不会触发重绘；
            // 这里每帧补一次按键状态，保证“先按住 Ctrl 再拖”也能看到 8 个把手。
            if (!m_resizing && resizable && m_hover) {
                const bool ctrl = CtrlDown();
                if (ctrl != m_ctrlHint) {
                    m_ctrlHint = ctrl;
                    Invalidate();
                }
            }
            // 拖拽/缩放期间只改 bounds，子元素重排每帧最多一次
            if (m_layoutPending) OnLayout();
        }

        void Arrange(const Bounds& available) override {
            UI::Arrange(available);
            if (!freePosition) return;
            int x = std::clamp(windowPos.x, available.Left(), std::max(available.Right() - Width(), available.Left()));
            int y = std::clamp(windowPos.y, available.Top(), std::max(available.Bottom() - Height(), available.Top()));
            SetBounds(Bounds{ x, y, Width(), Height() });
            OnLayout();
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

        void OnMouseEnter() override { m_hover = true; Invalidate(); }
        void OnMouseLeave() override {
            if (!m_resizing) {
                m_hover = false;
                m_ctrlHint = false;
                m_resizeEdge = ResizeNone;
                Invalidate();
            }
        }

        // Ctrl 按住时，边框 8 个方向都是缩放热区；离开边框的 Ctrl 不拦截子控件点击。
        [[nodiscard]] unsigned HitResizeEdge(const int x, const int y) const {
            if (!resizable || !frameVisible || !open || !isVisible || bounds.IsEmpty()) return ResizeNone;
            if (x < Left() || x >= Right() || y < Top() || y >= Bottom()) return ResizeNone;
            unsigned edge = ResizeNone;
            if (x == Left()) edge |= ResizeLeft;
            if (x == Right() - 1) edge |= ResizeRight;
            if (y == Top()) edge |= ResizeTop;
            if (y == Bottom() - 1) edge |= ResizeBottom;
            return edge;
        }

        [[nodiscard]] MouseCursor CursorAt(const int x, const int y) const override {
            if (!open || !isVisible) return MouseCursor::Arrow;
            const unsigned edge = HitResizeEdge(x, y);
            const bool horizontal = (edge & (ResizeLeft | ResizeRight)) != 0;
            const bool vertical = (edge & (ResizeTop | ResizeBottom)) != 0;
            if (horizontal && vertical) return MouseCursor::SizeAll;
            if (horizontal) return MouseCursor::SizeWE;
            if (vertical) return MouseCursor::SizeNS;
            // 标题栏可拖动：显示移动指针；正文区域交给子控件
            if (draggable && frameVisible && y == Top()) return MouseCursor::SizeAll;
            return UI::CursorAt(x, y);
        }

        bool OnMouse(const xMouse& ev) override {
            if (!open || !isVisible) return false;

            if (ev.action == MouseAction::Move) {
                const bool inside = bounds.Contains(Position{ ev.x, ev.y });
                if (m_resizing) {
                    ResizeTo(ev.x, ev.y);
                    return true;
                }
                if (m_dragging) {
                    MoveTo(ev.x - m_dragDX, ev.y - m_dragDY);
                    return true;
                }
                if (!inside) {
                    if (m_ctrlHint || m_resizeEdge != ResizeNone) {
                        m_ctrlHint = false;
                        m_resizeEdge = ResizeNone;
                        Invalidate();
                    }
                    return false;
                }
                const bool hint = resizable && ev.ctrl;
                const unsigned edge = hint ? HitResizeEdge(ev.x, ev.y) : ResizeNone;
                if (hint != m_ctrlHint || edge != m_resizeEdge) {
                    m_ctrlHint = hint;
                    m_resizeEdge = edge;
                    Invalidate();
                }
                return edge != ResizeNone;
            }

            if (ev.action == MouseAction::Down && ev.IsLeft()) {
                const bool inTitle = ev.y == Top() + 1;
                const bool inClose = closable && inTitle && ev.x >= Right() - 4 && ev.x < Right() - 1;
                if (inClose) {
                    MessageQueue::PostFrom(this, this, "close");
                    return true;
                }
                const unsigned edge = (resizable && ev.ctrl) ? HitResizeEdge(ev.x, ev.y) : ResizeNone;
                if (edge != ResizeNone) {
                    m_resizing = true;
                    m_resizeEdge = edge;
                    m_resizeStart = bounds;
                    m_dragging = false;
                    m_ctrlHint = true;
                    Invalidate();
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
                if (m_resizing) {
                    m_resizing = false;
                    m_ctrlHint = ev.ctrl;
                    Invalidate();
                    return true;
                }
                m_dragging = false;
                return false;
            }
            return false;
        }

        void OnRender(ConsoleBackBuffer& buf) override {
            if (!open || bounds.IsEmpty()) return;
            const Color bg = EffectiveBackground();
            const bool showHandles = resizable && (m_resizing || m_ctrlHint || (m_hover && CtrlDown()));
            buf.FillStyle(bounds, Style(style.fg, bg));
            if (frameVisible) {
                const Color frameColor = showHandles ? Theme::Current().warning : borderColor;
                buf.DrawBox(bounds, Box::Effective(chars), Style(frameColor, bg));
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

                if (showHandles) {
                    // 8 个把手：4 个角 + 4 条边中点。
                    // 不画 ◆：它在部分 CJK 终端里按双宽渲染，缩放提示统一用单宽 +。
                    const wchar_t handle = L'+';
                    const Style hs(Theme::Current().accent, bg);
                    const int mx = Left() + Width() / 2;
                    const int my = Top() + Height() / 2;
                    const int rx = Right() - 1;
                    const int by = Bottom() - 1;
                    const int hx[8] = { Left(), rx, Left(), rx, mx, mx, Left(), rx };
                    const int hy[8] = { Top(), Top(), by, by, Top(), by, my, my };
                    for (int i = 0; i < 8; ++i) buf.SetCell(hx[i], hy[i], handle, hs);
                }
            }
        }

    private:
        static bool CtrlDown() {
            return (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        }

        void ApplyBounds(const Bounds& b) {
            windowPos = Position{ b.Left(), b.Top() };
            layout.width = b.width();
            layout.height = b.height();
            SetBounds(b);
            m_layoutPending = true;
            Invalidate();
        }

        void ResizeTo(const int px, const int py) {
            if (!m_resizing) return;
            int l = m_resizeStart.Left();
            int t = m_resizeStart.Top();
            int r = m_resizeStart.Right() - 1;
            int b = m_resizeStart.Bottom() - 1;

            if ((m_resizeEdge & ResizeLeft) != 0)
                l = std::min(px, r - (minWidth - 1));
            if ((m_resizeEdge & ResizeRight) != 0)
                r = std::max(px, l + (minWidth - 1));
            if ((m_resizeEdge & ResizeTop) != 0)
                t = std::min(py, b - (minHeight - 1));
            if ((m_resizeEdge & ResizeBottom) != 0)
                b = std::max(py, t + (minHeight - 1));

            // 不把窗口缩放到父容器之外
            if (parent != nullptr) {
                const Bounds pb = parent->GetBounds();
                l = std::max(l, pb.Left());
                t = std::max(t, pb.Top());
                r = std::min(r, pb.Right() - 1);
                b = std::min(b, pb.Bottom() - 1);
            }
            if (r - l + 1 < minWidth) r = l + minWidth - 1;
            if (b - t + 1 < minHeight) b = t + minHeight - 1;
            if (r < l || b < t) return;
            ApplyBounds(Bounds{ l, t, r - l + 1, b - t + 1 });
        }

        bool m_dragging{ false };
        bool m_hover{ false };
        bool m_ctrlHint{ false };
        bool m_layoutPending{ false };
        bool m_resizing{ false };
        unsigned m_resizeEdge{ ResizeNone };
        Bounds m_resizeStart{};
        int m_dragDX{ 0 };
        int m_dragDY{ 0 };
    };

    using UIDialog = Window;
    using UIPopup = Window;

} // namespace Terminal
