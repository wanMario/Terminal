//
// Terminal / Components / TerminalPanel.hpp
// 布局容器：Row/Column 弹性排布、可选标题、可选底色，支持任意嵌套。
//

#pragma once

#include <string>

#include "../Core/UI.hpp"
#include "../Core/Layout.hpp"
#include "../Core/TerminalBuffer.hpp"

namespace Terminal {

    class Panel : public UI {
    public:
        

        std::string title{};       // 可选标题行（占 1 行，不影响子元素布局顺序）
        bool showTitle{ false };
        bool fillBackground{ true };

        Panel() { signature = "UIPanel"; layout.align = Align::Stretch;
            style = Style(Theme::Current().text, Theme::Current().background);
            layout.flow = Flow::Column;
        }

        Panel(const Flow flow) : Panel() { signature = "UIPanel"; layout.align = Align::Stretch; layout.flow = flow; }

        Panel& SetFlow(const Flow f) { layout.flow = f; return *this; }
        Panel& SetGap(const int g) { layout.gap = g; return *this; }
        Panel& SetPadding(const int all) { layout.Padding(all); return *this; }
        Panel& SetPadding(const int h, const int v) { layout.Padding(h, v); return *this; }
        Panel& SetTitle(const std::string& t) { title = t; showTitle = true; return *this; }
        Panel& SetFill(const bool b) { fillBackground = b; return *this; }
        Panel& SetBackgroundColor(const Color& c) { style.bg = c; style.opaque = true; return *this; }

        //---------- 度量 ----------
        [[nodiscard]] int MeasureWidth() const override {
            if (layout.width >= 0) return layout.width;
            int cw = 0, ch = 0;
            Layout::MeasureChildren(*this, cw, ch);
            const int titleW = showTitle ? Utf8::DisplayWidth(title) : 0;
            return std::max(cw, titleW) + layout.HorizontalPadding();
        }

        [[nodiscard]] int MeasureHeight() const override {
            if (layout.height >= 0) return layout.height;
            int cw = 0, ch = 0;
            Layout::MeasureChildren(*this, cw, ch);
            return ch + layout.VerticalPadding() + (showTitle ? 1 : 0);
        }

        // 有标题时，内容区从标题下一行开始
        [[nodiscard]] Bounds ContentBounds() const override {
            Bounds inner = UI::ContentBounds();
            if (showTitle) {
                inner = Bounds{ inner.Left(), inner.Top() + 1,
                                inner.width(), std::max(inner.height() - 1, 0) };
            }
            return inner;
        }

        void OnLayout() override { Layout::ArrangeChildren(*this); }

        void OnRender(ConsoleBackBuffer& buf) override {
            const Theme& th = Theme::Current();
            const Color bg = EffectiveBackground();   // 自己设了背景就用自己，否则继承父级
            if (fillBackground) buf.FillStyle(bounds, Style(style.fg, bg));

            if (showTitle && !title.empty() && Width() > 0 && Height() > 0) {
                const std::string shown = Utf8::Truncate(title, Width());
                buf.WriteText(Left(), Top(), shown, Style(th.accent, bg).SetBold(), Width());
                if (Width() > Utf8::DisplayWidth(shown)) {
                    const int lineY = Top() + 0;
                    for (int x = Left() + Utf8::DisplayWidth(shown) + 1; x < Right(); ++x) {
                        buf.SetCell(x, lineY, L'─', Style(th.border, bg));
                    }
                }
            }
        }
    };

    using UIPanel = Panel;
}
