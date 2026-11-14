//
// Terminal / Components / TerminalBadge.hpp
// Badge：胶囊/标签（带状态点），用于状态栏、版本号、提示条（装饰元件）
//

#pragma once

#include <string>

#include "../Core/UI.hpp"
#include "../Core/TerminalBuffer.hpp"
#include "../Utils/BoxChars.hpp"
#include "../Utils/Theme.hpp"

namespace Terminal {

    class Badge : public UI {
    public:
        std::string text{ "BADGE" };
        bool showDot{ true };
        bool useCustomColors{ false };
        Color bg{ Colors::Cyan };
        Color fg{ Colors::Black };

        Badge() {
            signature = "UIBadge";
            layout.height = 1;
            layout.minHeight = 1;
        }

        explicit Badge(const std::string& t) : Badge() { text = t; }

        Badge& SetText(const std::string& t) { text = t; InvalidateLayout(); return *this; }
        Badge& SetDot(const bool b) { showDot = b; InvalidateLayout(); return *this; }
        Badge& SetColors(const Color& background, const Color& foreground) {
            useCustomColors = true;
            bg = background;
            fg = foreground;
            Invalidate();
            return *this;
        }

        [[nodiscard]] int MeasureWidth() const override {
            if (layout.width >= 0) return layout.width;
            return Utf8::DisplayWidth(text) + 2 + (showDot ? Glyphs::DotWidth() : 0);
        }
        [[nodiscard]] int MeasureHeight() const override { return 1; }

        void OnRender(ConsoleBackBuffer& buf) override {
            const Theme& th = Theme::Current();
            const Color back = useCustomColors ? bg : th.accent;
            const Color fore = useCustomColors ? fg : th.accentText;

            buf.FillStyle(bounds, Style(fore, back));

            const int y = Top();
            int x = Left() + 1;
            if (showDot) {
                buf.SetCell(x, y, Glyphs::Dot(), Style(fore, back).SetBold());
                x += Glyphs::DotWidth();
            }
            const std::string shown = Utf8::Truncate(text, std::max(Right() - x - 1, 1));
            buf.WriteText(x, y, shown, Style(fore, back).SetBold(), Right() - x);
        }
    };

    using UIBadge = Badge;
}
