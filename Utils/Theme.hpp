//
// Terminal / Utils / Theme.hpp
// 主题：把整套 UI 的配色集中管理，元件默认从这里取色
//

#pragma once

#include <string>
#include "Color.hpp"

namespace Terminal {

    struct Theme {
        std::string name{ "dark" };

        // 文本
        Color text{ Colors::Gray };
        Color textMuted{ Colors::DarkGray };
        Color textDisabled{ Colors::DarkGray };
        Color textStrong{ Colors::White };

        // 背景
        Color background{ Colors::Black };
        Color surface{ Color::OfIndex(0) };      // 面板底色
        Color surfaceAlt{ Color::OfIndex(8) };   // 交替行/输入框底色

        // 强调
        Color accent{ Colors::Cyan };
        Color accentText{ Colors::Black };
        Color primary{ Colors::Cyan };
        Color success{ Colors::Green };
        Color warning{ Colors::Yellow };
        Color danger{ Colors::Red };
        Color info{ Colors::Blue };

        // 边框
        Color border{ Colors::DarkGray };
        Color borderFocus{ Colors::Cyan };
        Color borderDisabled{ Colors::DarkGray };

        // 交互态
        Color hoverBg{ Color::OfIndex(8) };
        Color activeBg{ Colors::Cyan };
        Color activeText{ Colors::Black };

        // 滑杆
        Color track{ Color::OfIndex(8) };
        Color trackFill{ Colors::Cyan };
        Color thumb{ Colors::Yellow };

        // ---------------- 预置主题 ----------------
        static Theme Dark() {
            Theme t;
            t.name = "dark";
            // 参考现代终端（GitHub Dark）的层次：深底、卡片略亮、边框极淡、单一冷色强调
            t.background = Color::Parse("#0D1117");
            t.surface = Color::Parse("#161B22");
            t.surfaceAlt = Color::Parse("#21262D");
            t.text = Color::Parse("#C9D1D9");
            t.textMuted = Color::Parse("#8B949E");
            t.textDisabled = Color::Parse("#484F58");
            t.textStrong = Color::Parse("#F0F6FC");
            t.accent = Color::Parse("#58A6FF");
            t.accentText = Color::Parse("#0D1117");
            t.primary = Color::Parse("#58A6FF");
            t.success = Color::Parse("#3fb94f");
            t.warning = Color::Parse("#D29922");
            t.danger = Color::Parse("#F85149");
            t.info = Color::Parse("#79C0FF");
            t.border = Color::Parse("#30363D");
            t.borderFocus = Color::Parse("#58A6FF");
            t.borderDisabled = Color::Parse("#21262D");
            t.hoverBg = Color::Parse("#21262D");
            t.activeBg = Color::Parse("#1F6FEB");
            t.activeText = Color::Parse("#F0F6FC");
            t.track = Color::Parse("#21262D");
            t.trackFill = Color::Parse("#2F81F7");
            t.thumb = Color::Parse("#F0F6FC");
            return t;
        }

        static Theme Light() {
            Theme t;
            t.name = "light";
            t.text = Colors::Black;
            t.textMuted = Color::OfIndex(8);
            t.textDisabled = Color::OfIndex(7);
            t.textStrong = Colors::Black;
            t.background = Colors::Gray;
            t.surface = Color::OfIndex(7);
            t.surfaceAlt = Color::OfIndex(15);
            t.accent = Colors::DarkBlue;
            t.accentText = Colors::White;
            t.primary = Colors::DarkBlue;
            t.success = Colors::DarkGreen;
            t.warning = Colors::DarkYellow;
            t.danger = Colors::DarkRed;
            t.border = Color::OfIndex(8);
            t.borderFocus = Colors::DarkBlue;
            t.hoverBg = Color::OfIndex(15);
            t.activeBg = Colors::DarkBlue;
            t.activeText = Colors::White;
            t.track = Color::OfIndex(8);
            t.trackFill = Colors::DarkBlue;
            t.thumb = Colors::DarkRed;
            return t;
        }

        static Theme Neon() {
            Theme t = Dark();
            t.name = "neon";
            t.text = Color::OfRGB(210, 214, 255);
            t.textMuted = Color::OfRGB(110, 116, 160);
            t.background = Color::OfRGB(12, 12, 24);
            t.surface = Color::OfRGB(20, 20, 38);
            t.surfaceAlt = Color::OfRGB(30, 28, 56);
            t.accent = Color::OfRGB(255, 0, 200);
            t.primary = Color::OfRGB(0, 230, 255);
            t.success = Color::OfRGB(0, 255, 150);
            t.warning = Color::OfRGB(255, 190, 0);
            t.danger = Color::OfRGB(255, 60, 90);
            t.border = Color::OfRGB(60, 60, 110);
            t.borderFocus = Color::OfRGB(0, 230, 255);
            t.hoverBg = Color::OfRGB(40, 36, 78);
            t.activeBg = Color::OfRGB(255, 0, 200);
            t.activeText = Color::OfRGB(10, 10, 20);
            t.track = Color::OfRGB(34, 32, 60);
            t.trackFill = Color::OfRGB(0, 230, 255);
            t.thumb = Color::OfRGB(255, 0, 200);
            return t;
        }

        // ---------------- 全局当前主题 ----------------
        static Theme& Current() {
            static Theme theme = Dark();
            return theme;
        }

        static void Apply(const Theme& t) { Current() = t; }
        static void UseDark() { Current() = Dark(); }
        static void UseLight() { Current() = Light(); }
        static void UseNeon() { Current() = Neon(); }

        static bool ApplyByName(const std::string& n) {
            if (n == "dark") { UseDark(); return true; }
            if (n == "light") { UseLight(); return true; }
            if (n == "neon") { UseNeon(); return true; }
            return false;
        }
    };

    // 便捷：以当前主题构造文本样式
    inline Style ThemedTextStyle(const Color& fg) {
        return Style(fg, Theme::Current().background);
    }
}
