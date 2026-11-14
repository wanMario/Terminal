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
        int scrollX{ 0 };             // 当前横向滚动列（0 = 最左）
        int wheelStep{ 3 };           // 滚轮一格的滚动行数
        int wheelStepX{ 2 };          // Shift+滚轮一格的横向列数
        bool showScrollbar{ true };
        bool showHScrollbar{ true };  // 内容比视口宽时自动出现底部横条
        bool fillBackground{ true };
        int scrollbarWidth{ 1 };
        int scrollbarHeight{ 1 };

        ScrollView() {
            signature = "UIScrollView";
            layout.flow = Flow::Column;
            layout.align = Align::Stretch;
            style = Style(Theme::Current().text, Theme::Current().background);
            focusable = true;
        }

        ScrollView& SetWheelStep(const int n) { wheelStep = std::max(n, 1); return *this; }
        ScrollView& SetWheelStepX(const int n) { wheelStepX = std::max(n, 1); return *this; }
        ScrollView& SetShowScrollbar(const bool b) { showScrollbar = b; InvalidateLayout(); return *this; }
        ScrollView& SetShowHScrollbar(const bool b) { showHScrollbar = b; InvalidateLayout(); return *this; }
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

        // 子内容自然宽度：有显式宽度就用显式宽度，否则按子元素自己量。
        [[nodiscard]] int NaturalWidth() const {
            int total = 0;
            for (const auto& c : children) {
                if (!c || !c->isVisible) continue;
                total = std::max(total, c->layout.width >= 0 ? c->layout.width : c->PreferredWidth());
            }
            return total;
        }

        [[nodiscard]] bool HasVBar() const {
            const Bounds vp = ContentBounds();
            return showScrollbar && vp.height() > 0 && ContentHeight() > vp.height() - (HasHBar() ? scrollbarHeight : 0);
        }

        [[nodiscard]] bool HasHBar() const {
            const Bounds vp = ContentBounds();
            const int vbar = (showScrollbar && ContentHeight() > vp.height()) ? scrollbarWidth : 0;
            return showHScrollbar && vp.width() > 0 && NaturalWidth() > vp.width() - vbar;
        }

        [[nodiscard]] int ViewportWidth() const {
            const Bounds vp = ContentBounds();
            return std::max(vp.width() - (HasVBar() ? scrollbarWidth : 0), 0);
        }

        [[nodiscard]] int ViewportHeightActual() const {
            const Bounds vp = ContentBounds();
            return std::max(vp.height() - (HasHBar() ? scrollbarHeight : 0), 0);
        }

        [[nodiscard]] int MaxScroll() const {
            return std::max(ContentHeight() - ViewportHeightActual(), 0);
        }

        [[nodiscard]] int MaxScrollX() const {
            return std::max(NaturalWidth() - ViewportWidth(), 0);
        }

        // 含滚动条时的可视宽度
        [[nodiscard]] int ContentWidth() const { return ViewportWidth(); }

        void SetScroll(const int value) {
            const int next = std::clamp(value, 0, MaxScroll());
            if (next == scroll) return;
            scroll = next;
            // 滚动只影响自己的内容区，不需要让父级整树重排：立刻把子元素按新
            // scroll 摆放，再重绘。否则拖动侧边滑杆时只会动滚动条，列表内容要等
            // 下一次 Window/根容器 Arrange 才更新。
            if (!bounds.IsEmpty()) {
                OnLayout();
                layoutDirty = false;
            } else {
                InvalidateLayout();
            }
            Invalidate();
        }

        void SetScrollX(const int value) {
            const int next = std::clamp(value, 0, MaxScrollX());
            if (next == scrollX) return;
            scrollX = next;
            if (!bounds.IsEmpty()) {
                OnLayout();
                layoutDirty = false;
            } else {
                InvalidateLayout();
            }
            Invalidate();
        }

        void ScrollBy(const int delta) { SetScroll(scroll + delta); }
        void ScrollByX(const int delta) { SetScrollX(scrollX + delta); }
        void Page(const int dir) { ScrollBy(dir * std::max(ViewportHeightActual() - 1, 1)); }
        void PageX(const int dir) { ScrollByX(dir * std::max(ViewportWidth() - 1, 1)); }

        //---------- 布局 ----------
        void OnLayout() override {
            Bounds vp = ContentBounds();
            if (vp.IsEmpty()) return;

            const int maxScroll = MaxScroll();
            const int maxScrollX = MaxScrollX();
            scroll = std::clamp(scroll, 0, maxScroll);
            scrollX = std::clamp(scrollX, 0, maxScrollX);

            const int viewW = ViewportWidth();
            const int contentW = std::max(viewW, NaturalWidth());
            const int gap = std::max(layout.gap, 0);
            int cursor = vp.Top() - scroll;
            const int baseX = vp.Left() - scrollX;

            for (auto& child : children) {
                if (!child || !child->isVisible) continue;
                const int prefH = std::max(child->PreferredHeight(), 0);
                int w = (child->layout.width >= 0) ? std::min(child->layout.width, contentW) : contentW;
                int x = baseX;
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
            if (!HasVBar() || vp.width() <= 0) return BarGeom{};
            const int h = ViewportHeightActual();
            if (h <= 0) return BarGeom{};
            const int contentH = std::max(ContentHeight(), 1);
            const int thumbH = std::clamp(h * h / contentH, 1, h);
            const int travel = h - thumbH;
            const int thumbTop = vp.Top() + (travel > 0 && MaxScroll() > 0
                ? static_cast<int>(std::lround(static_cast<double>(scroll) / MaxScroll() * travel))
                : 0);
            return BarGeom{ vp.Right() - 1, vp.Top(), h, thumbTop, thumbH, true };
        }

        struct HBarGeom {
            int y{ 0 };
            int left{ 0 };
            int w{ 0 };
            int thumbLeft{ 0 };
            int thumbW{ 0 };
            bool valid{ false };
        };

        [[nodiscard]] HBarGeom HBar() const {
            const Bounds vp = ContentBounds();
            if (!HasHBar() || vp.width() <= 0) return HBarGeom{};
            const int w = ViewportWidth();
            if (w <= 0) return HBarGeom{};
            const int contentW = std::max(NaturalWidth(), 1);
            const int thumbW = std::clamp(w * w / contentW, 1, w);
            const int travel = w - thumbW;
            const int thumbLeft = vp.Left() + (travel > 0 && MaxScrollX() > 0
                ? static_cast<int>(std::lround(static_cast<double>(scrollX) / MaxScrollX() * travel))
                : 0);
            return HBarGeom{ vp.Top() + ViewportHeightActual(), vp.Left(), w, thumbLeft, thumbW, true };
        }

        void ScrollToThumbY(const double cellY) {
            const BarGeom g = Bar();
            if (!g.valid) return;
            const int travel = g.h - g.thumbH;
            if (travel <= 0) { SetScroll(0); return; }
            const double ratio = std::clamp((cellY - static_cast<double>(g.thumbTop)) / travel, 0.0, 1.0);
            SetScroll(static_cast<int>(std::lround(ratio * MaxScroll())));
        }

        void ScrollToThumbX(const double cellX) {
            const HBarGeom g = HBar();
            if (!g.valid) return;
            const int travel = g.w - g.thumbW;
            if (travel <= 0) { SetScrollX(0); return; }
            const double ratio = std::clamp((cellX - static_cast<double>(g.thumbLeft)) / travel, 0.0, 1.0);
            SetScrollX(static_cast<int>(std::lround(ratio * MaxScrollX())));
        }

        // 滚动条是“覆盖在内容上”的热区，必须优先于列表行命中。
        [[nodiscard]] bool HitTestOverlay(const int x, const int y) const override {
            if (!isVisible || !isEnabled) return false;
            const BarGeom g = Bar();
            if (g.valid && x == g.x && y >= g.top && y < g.top + g.h) return true;
            const HBarGeom hg = HBar();
            if (hg.valid && y == hg.y && x >= hg.left && x < hg.left + hg.w) return true;
            return false;
        }

        [[nodiscard]] MouseCursor CursorAt(const int x, const int y) const override {
            if (!isVisible || !isEnabled) return MouseCursor::Arrow;
            const BarGeom g = Bar();
            if (g.valid && x == g.x && y >= g.top && y < g.top + g.h) return MouseCursor::SizeNS;
            const HBarGeom hg = HBar();
            if (hg.valid && y == hg.y && x >= hg.left && x < hg.left + hg.w) return MouseCursor::SizeWE;
            return UI::CursorAt(x, y);
        }

        bool OnMouse(const xMouse& ev) override {
            if (!isEnabled) return false;

            if (ev.action == MouseAction::Wheel) {
                const int raw = ev.wheelDelta;
                int lines = raw / 120;
                if (lines == 0) lines = (raw > 0 ? 1 : -1);
                if (ev.shift && HasHBar()) {
                    ScrollByX(-lines * wheelStepX);
                } else if (MaxScroll() > 0) {
                    ScrollBy(-lines * wheelStep);
                } else if (HasHBar()) {
                    ScrollByX(-lines * wheelStepX);
                }
                return true;
            }

            const BarGeom g = Bar();
            const HBarGeom hg = HBar();
            const bool inV = g.valid && ev.x == g.x && ev.y >= g.top && ev.y < g.top + g.h;
            const bool inH = hg.valid && ev.y == hg.y && ev.x >= hg.left && ev.x < hg.left + hg.w;

            switch (ev.action) {
            case MouseAction::Down:
                if (inH) {
                    m_draggingH = true;
                    m_dragging = false;
                    if (ev.x < hg.thumbLeft || ev.x >= hg.thumbLeft + hg.thumbW) {
                        const int travel = hg.w - hg.thumbW;
                        if (travel > 0) {
                            const double r = std::clamp(static_cast<double>(ev.x - hg.thumbLeft) / travel, 0.0, 1.0);
                            SetScrollX(static_cast<int>(std::lround(r * MaxScrollX())));
                        }
                        m_grabOffsetX = hg.thumbW / 2;
                    } else {
                        m_grabOffsetX = ev.x - hg.thumbLeft;
                    }
                    Invalidate();
                    return true;
                }
                if (!inV) return false;
                m_dragging = true;
                m_draggingH = false;
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
                if (m_draggingH) {
                    const HBarGeom now = HBar();
                    if (!now.valid) return true;
                    const int travel = now.w - now.thumbW;
                    if (travel > 0) {
                        const double wantLeft = ev.FineX() - static_cast<double>(m_grabOffsetX);
                        const double left = std::clamp(wantLeft,
                            static_cast<double>(now.left),
                            static_cast<double>(now.left + travel));
                        const double r = (left - static_cast<double>(now.left)) /
                            static_cast<double>(travel);
                        SetScrollX(static_cast<int>(std::lround(r * MaxScrollX())));
                    }
                    return true;
                }
                if (m_dragging) {
                    const BarGeom now = Bar();
                    if (!now.valid) return true;
                    const int travel = now.h - now.thumbH;
                    if (travel > 0) {
                        // 用亚格坐标直接换算比例，避免高 DPI 下必须跨一整格才动一档
                        const double wantTop = ev.FineY() - static_cast<double>(m_grabOffset);
                        const double top = std::clamp(wantTop,
                            static_cast<double>(now.top),
                            static_cast<double>(now.top + travel));
                        const double r = (top - static_cast<double>(now.top)) /
                            static_cast<double>(travel);
                        SetScroll(static_cast<int>(std::lround(r * MaxScroll())));
                    }
                    return true;
                }
                return inV || inH;
            case MouseAction::Up:
                if (m_dragging || m_draggingH) {
                    m_dragging = false;
                    m_draggingH = false;
                    Invalidate();
                    return true;
                }
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
            if (IsKey(key, SKey::LeftArrow)) { ScrollByX(-1); return true; }
            if (IsKey(key, SKey::RightArrow)) { ScrollByX(1); return true; }
            return false;
        }

        // 消息："scrollTo" / "scrollBy" / "scrollToX" / "scrollByX" / "page"
        bool OnMessage(const Message& msg) override {
            if (msg.type == "scrollTo") { SetScroll(static_cast<int>(msg.number)); return true; }
            if (msg.type == "scrollBy") { ScrollBy(static_cast<int>(msg.number)); return true; }
            if (msg.type == "scrollToX") { SetScrollX(static_cast<int>(msg.number)); return true; }
            if (msg.type == "scrollByX") { ScrollByX(static_cast<int>(msg.number)); return true; }
            if (msg.type == "page") { Page(msg.number >= 0 ? 1 : -1); return true; }
            if (msg.type == "pageX") { PageX(msg.number >= 0 ? 1 : -1); return true; }
            return UI::OnMessage(msg);
        }

        //---------- 绘制 ----------
        void OnRender(ConsoleBackBuffer& buf) override {
            const Color bg = EffectiveBackground();
            if (fillBackground) buf.FillStyle(bounds, Style(style.fg, bg));
        }

        // 滚动条必须画在子元素之后，否则滚动到最后一行的内容会盖住横条/竖条。
        void OnRenderOverlay(ConsoleBackBuffer& buf) override {
            const Theme& th = Theme::Current();
            const Color bg = EffectiveBackground();
            const BarGeom g = Bar();
            if (g.valid) {
                for (int y = g.top; y < g.top + g.h; ++y) {
                    const bool thumb = (y >= g.thumbTop && y < g.thumbTop + g.thumbH);
                    buf.SetCell(g.x, y, thumb ? L'█' : L'│',
                                Style(thumb ? th.thumb : th.border, bg));
                }
            }

            const HBarGeom hg = HBar();
            if (hg.valid) {
                for (int x = hg.left; x < hg.left + hg.w; ++x) {
                    const bool thumb = (x >= hg.thumbLeft && x < hg.thumbLeft + hg.thumbW);
                    buf.SetCell(x, hg.y, thumb ? L'█' : L'─',
                                Style(thumb ? th.thumb : th.border, bg));
                }
            }
        }

    private:
        bool m_dragging{ false };
        bool m_draggingH{ false };
        int m_grabOffset{ 0 };
        int m_grabOffsetX{ 0 };
    };

    using UIScrollView = ScrollView;
}
