//
// Terminal / Components / TerminalBorder.hpp
// 边框容器：单线/双线/圆角/粗线/虚线/ASCII 六种样式，可带标题，子元素参与布局。
//

#pragma once

#include <string>

#include "../Core/UI.hpp"
#include "../Core/Layout.hpp"
#include "../Core/TerminalBuffer.hpp"
#include "../Utils/BoxChars.hpp"

namespace Terminal {

    class Border : public UI {
    public:
        

        BoxChars chars{ Box::Round };
        std::string title{};
        LayoutPos titleAlign{ LayoutPos::Left };
        bool showFrame{ true };

        // 自定义边框/标题颜色（默认跟随主题）
        bool useCustomBorderColor{ false };
        Color borderColor{ Colors::DarkGray };
        bool useCustomTitleColor{ false };
        Color titleColor{ Colors::Cyan };

        Border() { signature = "UIBorder"; layout.align = Align::Stretch; style = Style(Theme::Current().text, Theme::Current().background); }

        explicit Border(const std::string& caption) : Border() { title = caption; }

        Border& SetChars(const BoxChars& c) { chars = c; return *this; }
        Border& SetTitle(const std::string& t) { title = t; return *this; }
        Border& SetTitleAlign(const LayoutPos a) { titleAlign = a; return *this; }
        Border& SetFrameVisible(const bool b) { showFrame = b; return *this; }
        Border& SetBorderColor(const Color& c) { borderColor = c; useCustomBorderColor = true; return *this; }
        Border& SetTitleColor(const Color& c) { titleColor = c; useCustomTitleColor = true; return *this; }
        Border& SetPadding(const int all) { layout.Padding(all); return *this; }
        Border& SetFlow(const Flow f) { layout.FlowDir(f); return *this; }
        Border& SetGap(const int g) { layout.Gap(g); return *this; }

        //---------- 度量 ----------
        [[nodiscard]] int MeasureWidth() const override {
            if (layout.width >= 0) return layout.width;
            int cw = 0, ch = 0;
            Layout::MeasureChildren(*this, cw, ch);
            const int frame = showFrame ? 2 : 0;
            const int titleW = title.empty() ? 0 : Utf8::DisplayWidth(title) + 2;
            return std::max(cw, titleW) + layout.HorizontalPadding() + frame;
        }

        [[nodiscard]] int MeasureHeight() const override {
            if (layout.height >= 0) return layout.height;
            int cw = 0, ch = 0;
            Layout::MeasureChildren(*this, cw, ch);
            const int frame = showFrame ? 2 : 0;
            return ch + layout.VerticalPadding() + frame;
        }

        // 内容区：让出边框
        [[nodiscard]] Bounds ContentBounds() const override {
            Bounds inner = bounds;
            if (showFrame && Width() >= 2 && Height() >= 2) {
                inner = Bounds{ bounds.Left() + 1, bounds.Top() + 1,
                                std::max(Width() - 2, 0), std::max(Height() - 2, 0) };
            }
            return Bounds{ inner.Left() + layout.paddingLeft,
                           inner.Top() + layout.paddingTop,
                           std::max(inner.width() - layout.HorizontalPadding(), 0),
                           std::max(inner.height() - layout.VerticalPadding(), 0) };
        }

        void OnLayout() override { Layout::ArrangeChildren(*this); }

        //---------- 绘制 ----------
        void OnRender(ConsoleBackBuffer& buf) override {
            const Theme& th = Theme::Current();
            const Color bg = EffectiveBackground();
            if (style.opaque) buf.FillStyle(bounds, style);
            if (!showFrame) return;
            if (Width() < 2 || Height() < 2) return;

            const Color bc = useCustomBorderColor ? borderColor : th.border;
            const Color tc = useCustomTitleColor ? titleColor : th.accent;
            buf.DrawBox(bounds, chars, Style(bc, bg));

            if (title.empty()) return;

            const int innerW = Width() - 2;
            const std::string shown = Utf8::Truncate(" " + title + " ", innerW);
            const int w = Utf8::DisplayWidth(shown);
            int x = Left() + 1;
            if (titleAlign == LayoutPos::Center) x = Left() + 1 + std::max((innerW - w) / 2, 0);
            else if (titleAlign == LayoutPos::Right) x = Right() - 1 - w;
            x = std::clamp(x, Left() + 1, Right() - 1);
            buf.WriteText(x, Top(), shown, Style(tc, bg).SetBold(), Right() - 1 - x);
        }
    };

    using UIBorder = Border;
}
