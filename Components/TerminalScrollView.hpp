//
// Terminal / Components / TerminalScrollView.hpp
// 滚动容器：Column 排布 + 竖向滚动条 + 滚轮 / 拖动 / 键盘翻页。
// 子元素按完整内容高度排布，再整体上移 scroll 行；超出部分由 UI::Render 的裁剪挡住。
//

#pragma once

#include <algorithm>
#include <string>

#include "../Core/UI.hpp"
#include "../Core/Layout.hpp"
#include "../Core/TerminalBuffer.hpp"
#include "../Core/TerminalMessage.hpp"
#include "../Core/TerminalMouse.hpp"

namespace Terminal {

    class ScrollView : public UI {
    public:
        int scroll{ 0 };              // 当前滚动行（0 = 顶部）
        int wheelStep{ 3 };           // 滚轮一格的滚动行数
        bool showScrollbar{ true };
        bool fillBackground{ true };
        int scrollbarWidth{ 1 };

        ScrollView() {
            signature = "UIScrollView";
            layout.flow = Flow::Column;
            layout.align = Align::Stretch;
            style = Style(Theme::Current().text, Theme::Current().background);
            focusable = true;
        }

        ScrollView& SetWheelStep(const int n) { wheelStep = std::max(n, 1); return *this; }
        ScrollView& SetShowScrollbar(const bool b) { showScrollbar = b; InvalidateLayout(); return *this; }
        ScrollView& SetBackgroundColor(const Color& c) { style.bg = c; style.opaque = true; Invalidate(); return *this; }

        //---------- 度量 ----------
        int MeasureWidth() const override {
            if (layout.width >= 0) return layout.width;
            if (layout.grow > 0) return 1;
            int cw = 0, ch = 0;
            Layout::MeasureChildren(*this, cw, ch);
            return cw + layout.HorizontalPadding() + (showScrollbar ? scrollbarWidth : 0);
        }

        int MeasureHeight() const override {
            if (layout.height >= 0) return layout.height;
            // 参与主轴弹性分配时不要按内容高度抢空间，留给父级把剩余高度给它
            if (layout.grow > 0) return 1;
            int cw = 0, ch = 0;
            Layout::MeasureChildren(*this, cw, ch);
            return ch + layout.VerticalPadding();
        }

        [[nodiscard]] int ContentHeight() const {
            int gap = std::max(layout.gap, 0);
            int total = 0;
            int count = 0;
            for (const auto& c : children) {
                if (!c || !c->isVisible) continue;
                total += std::max(c->PreferredHeight(), 0);
                ++count;
            }
            if (count > 1) total += gap * (count - 1);
            return total;
        }

        [[nodiscard]] int ViewportHeight() const { return ContentBounds().height(); }

        [[nodiscard]] int MaxScroll() const {
            return std::max(ContentHeight() - ViewportHeight(), 0);
        }

        // 含 1 列滚动条时的可视宽度
        [[nodiscard]] int ContentWidth() const {
            const Bounds vp = ContentBounds();
            const int bar = (showScrollbar && MaxScroll() > 0) ? scrollbarWidth : 0;
            return std::max(vp.width() - bar, 0);
        }

        void SetScroll(const int value) {
            const int next = std::clamp(value, 0, MaxScroll());
            if (next == scroll) return;
            scroll = next;
            InvalidateLayout();   // 子元素位置变了，需要重新 Arrange
        }

        void ScrollBy(const int delta) { SetScroll(scroll + delta); }
        void Page(const int dir) { ScrollBy(dir * std::max(ViewportHeight() - 1, 1)); }

        //---------- 布局 ----------
        void OnLayout() override {
            Bounds vp = ContentBounds();
            if (vp.IsEmpty()) return;

            const int maxScroll = MaxScroll();
            scroll = std::clamp(scroll, 0, maxScroll);

            const int contentW = ContentWidth();
            const int gap = std::max(layout.gap, 0);
            int cursor = vp.Top() - scroll;

            for (auto& child : children) {
                if (!child || !child->isVisible) continue;
                const int prefH = std::max(child->PreferredHeight(), 0);
                int w = (child->layout.width >= 0) ? std::min(child->layout.width, contentW) : contentW;
                int x = vp.Left();
                if (w < contentW) {
                    switch (child->layout.align) {
                    case Align::Center: x += (contentW - w) / 2; break;
                    case Align::End:    x += contentW - w; break;
                    default: break;
                    }
                }
                child->Arrange(Bounds{ x, cursor, std::max(w, 0), prefH });
                cursor += prefH + gap;
            }
        }

        //---------- 交互 ----------
        // 滚动条几何（竖向，位于内容区最右一列）
        struct BarGeom {
            int x{ 0 };
            int top{ 0 };
            int h{ 0 };
            int thumbTop{ 0 };
            int thumbH{ 0 };
            bool valid{ false };
        };

        [[nodiscard]] BarGeom Bar() const {
            const Bounds vp = ContentBounds();
            if (!showScrollbar || MaxScroll() <= 0 || vp.height() <= 0) return BarGeom{};
            const int h = vp.height();
            const int contentH = std::max(ContentHeight(), 1);
            const int thumbH = std::clamp(h * h / contentH, 1, h);
            const int travel = h - thumbH;
            const int thumbTop = vp.Top() + (travel > 0
                ? static_cast<int>(std::lround(static_cast<double>(scroll) / MaxScroll() * travel))
                : 0);
            return BarGeom{ vp.Right() - 1, vp.Top(), h, thumbTop, thumbH, true };
        }

        void ScrollToThumbY(const double cellY) {
            const BarGeom g = Bar();
            if (!g.valid) return;
            const int travel = g.h - g.thumbH;
            if (travel <= 0) { SetScroll(0); return; }
            const double ratio = std::clamp((cellY - static_cast<double>(g.thumbTop)) / travel, 0.0, 1.0);
            SetScroll(static_cast<int>(std::lround(ratio * MaxScroll())));
        }

        bool OnMouse(const xMouse& ev) override {
            if (!isEnabled) return false;

            if (ev.action == MouseAction::Wheel) {
                const int raw = ev.wheelDelta;
                int lines = raw / 120 * wheelStep;
                if (lines == 0) lines = (raw > 0 ? wheelStep : -wheelStep);
                ScrollBy(-lines);      // 滚轮向上为正 -> 内容向上滚（scroll 减小）
                return true;
            }

            const BarGeom g = Bar();
            if (!g.valid) return false;
            const bool inBar = (ev.x == g.x && ev.y >= g.top && ev.y < g.top + g.h);

            switch (ev.action) {
            case MouseAction::Down:
                if (!inBar) return false;
                m_dragging = true;
                if (ev.y < g.thumbTop || ev.y >= g.thumbTop + g.thumbH) {
                    // 点轨道空白：直接翻到该位置
                    const int travel = g.h - g.thumbH;
                    if (travel > 0) {
                        const double r = std::clamp(static_cast<double>(ev.y - g.thumbTop) / travel, 0.0, 1.0);
                        SetScroll(static_cast<int>(std::lround(r * MaxScroll())));
                    }
                    m_grabOffset = g.thumbH / 2;
                } else {
                    m_grabOffset = ev.y - g.thumbTop;
                }
                Invalidate();
                return true;
            case MouseAction::Move:
                if (m_dragging) {
                    const BarGeom now = Bar();
                    if (!now.valid) return true;
                    const int travel = now.h - now.thumbH;
                    if (travel > 0) {
                        const int wantTop = static_cast<int>(std::lround(ev.FineY())) - m_grabOffset;
                        const int top = std::clamp(wantTop, now.top, now.top + travel);
                        const double r = static_cast<double>(top - now.top) / travel;
                        SetScroll(static_cast<int>(std::lround(r * MaxScroll())));
                    }
                    return true;
                }
                return inBar;
            case MouseAction::Up:
                if (m_dragging) { m_dragging = false; Invalidate(); return true; }
                return false;
            default:
                return false;
            }
        }

        bool OnKey(const xChar& key) override {
            if (!isEnabled) return false;
            if (IsKey(key, SKey::PageUp)) { Page(-1); return true; }
            if (IsKey(key, SKey::PageDown)) { Page(1); return true; }
            if (IsKey(key, SKey::Home)) { SetScroll(0); return true; }
            if (IsKey(key, SKey::End)) { SetScroll(MaxScroll()); return true; }
            return false;
        }

        // 消息："scrollTo" / "scrollBy" / "page"
        bool OnMessage(const Message& msg) override {
            if (msg.type == "scrollTo") { SetScroll(static_cast<int>(msg.number)); return true; }
            if (msg.type == "scrollBy") { ScrollBy(static_cast<int>(msg.number)); return true; }
            if (msg.type == "page") { Page(msg.number >= 0 ? 1 : -1); return true; }
            return UI::OnMessage(msg);
        }

        //---------- 绘制 ----------
        void OnRender(ConsoleBackBuffer& buf) override {
            const Theme& th = Theme::Current();
            const Color bg = EffectiveBackground();
            if (fillBackground) buf.FillStyle(bounds, Style(style.fg, bg));

            const BarGeom g = Bar();
            if (!g.valid) return;
            for (int y = g.top; y < g.top + g.h; ++y) {
                const bool thumb = (y >= g.thumbTop && y < g.thumbTop + g.thumbH);
                buf.SetCell(g.x, y, thumb ? L'█' : L'│',
                            Style(thumb ? th.thumb : th.border, bg));
            }
        }

    private:
        bool m_dragging{ false };
        int m_grabOffset{ 0 };
    };

    using UIScrollView = ScrollView;
}
