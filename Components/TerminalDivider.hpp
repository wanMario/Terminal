//
// Terminal / Components / TerminalDivider.hpp
// Divider：水平分隔线，可带小标题，用于把界面切成区块（装饰元件）
//

#pragma once

#include <string>

#include "../Core/UI.hpp"
#include "../Core/TerminalBuffer.hpp"
#include "../Utils/Theme.hpp"

namespace Terminal {

    class Divider : public UI {
    public:
        std::string title{};            // 空 = 纯横线
        BoxChars chars{ Box::Single };    // ─ / ═ / ━ / - 等
        bool useCustomColors{ false };
        Color lineColor{ Colors::DarkGray };
        Color titleColor{ Colors::Cyan };

        Divider() {
            signature = "UIDivider";
            layout.height = 1;
            layout.minHeight = 1;
        }

        explicit Divider(const std::string& t) : Divider() { title = t; }

        Divider& SetTitle(const std::string& t) { title = t; InvalidateLayout(); return *this; }
        Divider& SetChars(const BoxChars& c) { chars = c; Invalidate(); return *this; }
        Divider& SetColors(const Color& line, const Color& tit) {
            useCustomColors = true;
            lineColor = line;
            titleColor = tit;
            Invalidate();
            return *this;
        }

        [[nodiscard]] int MeasureWidth() const override { return layout.width >= 0 ? layout.width : 0; }
        [[nodiscard]] int MeasureHeight() const override { return 1; }

        void OnRender(ConsoleBackBuffer& buf) override {
            const Theme& th = Theme::Current();
            const Color bg = EffectiveBackground();
            const int y = Top();
            const int x0 = Left();
            const int x1 = Right();
            if (x1 <= x0) return;

            const Color lc = useCustomColors ? lineColor : th.border;
            const Color tc = useCustomColors ? titleColor : th.accent;
            const BoxChars& glyphs = Box::Effective(chars);

            if (title.empty()) {
                for (int x = x0; x < x1; ++x) buf.SetCell(x, y, glyphs.horizontal, Style(lc, bg));
                return;
            }

            // ── 标题 ─────────────
            const std::string label = " " + title + " ";
            const int labelW = Utf8::DisplayWidth(label);
            if (labelW + 2 >= x1 - x0) {
                buf.WriteText(x0, y, Utf8::Truncate(label, x1 - x0), Style(tc, bg).SetBold(), x1 - x0);
                return;
            }
            buf.SetCell(x0, y, glyphs.horizontal, Style(lc, bg));
            buf.SetCell(x0 + 1, y, glyphs.horizontal, Style(lc, bg));
            buf.WriteText(x0 + 2, y, label, Style(tc, bg).SetBold(), x1 - (x0 + 2));
            for (int x = x0 + 2 + labelW; x < x1; ++x) buf.SetCell(x, y, glyphs.horizontal, Style(lc, bg));
        }
    };

    using UIDivider = Divider;
}
