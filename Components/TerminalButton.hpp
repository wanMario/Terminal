//
// Created by Administrator on 2026/5/24.
// 实现：可聚焦按钮 —— 边框 + 居中标签 + 常态/焦点/按下/禁用四态配色，Enter/空格触发。
//

#pragma once

#include <functional>
#include <string>

#include "../Core/UI.hpp"
#include "../Core/TerminalBuffer.hpp"
#include "../Core/TerminalMessage.hpp"
#include "../Core/TerminalMouse.hpp"
#include "../Utils/BoxChars.hpp"

namespace Terminal {

    class Button : public UI {
    public:

        std::string label{ "Button" };
        std::function<void()> onClick{};

        BoxChars frame{ Box::Round };
        bool showFrame{ true };

        // 说明：按钮底色（bgc）刻意「不开放对外入口」—— 它永远继承所在容器的背景，
        //       否则按钮会变成卡片里一块突兀的色砖。下面的配色字段仍然保留（属性还在），
        //       只是不再提供改写背景的公开方法；要强调按钮请改边框/文字，不要改底。
        bool useCustomColors{ false };
        Color normalBg{ Colors::Black };
        Color normalFg{ Colors::Gray };      // 默认跟随主题，可自行 SetTextColor
        Color focusBg{ Colors::Cyan };
        Color focusFg{ Colors::Black };
        Color pressBg{ Colors::DarkCyan };
        Color pressFg{ Colors::White };

        // 三态配色完全可配；库的默认值跟随主题（不写死某种颜色），
        // demo 里再显式设成参考图的红框红字 / 青色交互框。
        Color borderColor{ Colors::Gray };
        Color hotBorderColor{ Colors::Cyan };
        bool thickBorderOnFocus{ true };

        Button() {
            signature = "UIButton";
            focusable = true;
            layout.minHeight = 1;
            const Theme& th = Theme::Current();
            normalFg = th.text;
            borderColor = th.border;
            hotBorderColor = th.accent;
        }

        explicit Button(const std::string& text) : Button() { label = text; }

        Button(const std::string& text, std::function<void()> action) : Button(text) {
            onClick = std::move(action);
        }

        //---------- 配置 ----------
        Button& SetLabel(const std::string& text) { label = text; return *this; }
        [[nodiscard]] const std::string& GetLabel() const { return label; }
        Button& OnClick(std::function<void()> action) { onClick = std::move(action); return *this; }
        Button& SetFrame(const BoxChars& c) { frame = c; return *this; }
        Button& SetShowFrame(const bool b) { showFrame = b; return *this; }

        // 只允许改「文字色」；背景色不提供入口（要保持容器一致，见上方字段注释）
        Button& SetTextColor(const Color& fg) {
            useCustomColors = true;
            normalFg = fg;
            if (!borderColorSet) borderColor = fg;   // 默认边框跟随文字色（图1：红字红框）
            return *this;
        }

        // 边框色：常态 / 交互（图中交互为红色互补的青）
        Button& SetBorderColor(const Color& c) { borderColor = c; borderColorSet = true; Invalidate(); return *this; }
        Button& SetHotBorderColor(const Color& c) { hotBorderColor = c; Invalidate(); return *this; }
        Button& SetThickBorderOnFocus(const bool b) { thickBorderOnFocus = b; Invalidate(); return *this; }
        // 一次配好文字 / 常态边框 / 交互边框
        Button& SetColors(const Color& fg, const Color& border, const Color& hot) {
            SetTextColor(fg);
            SetBorderColor(border);
            SetHotBorderColor(hot);
            return *this;
        }
        // 边框和文字同色：一个入口同时设常态文字色与常态边框色，
        // 聚焦只加粗、交互仍走 hotBorderColor。
        Button& SetColor(const Color& c) {
            SetTextColor(c);
            SetBorderColor(c);
            return *this;
        }

        //---------- 度量 ----------
        [[nodiscard]] int MeasureWidth() const override {
            if (layout.width >= 0) return layout.width;
            const int textW = Utf8::DisplayWidth(label);
            return textW + (showFrame ? 6 : 2);
        }

        [[nodiscard]] int MeasureHeight() const override {
            if (layout.height >= 0) return layout.height;
            return showFrame ? 3 : 1;
        }

        //---------- 状态 ----------
        void Trigger() {
            if (!isEnabled) return;
            m_pressed = true;
            if (onClick) onClick();
            m_pressed = false;
        }

        void OnFocus() override { focused = true; Invalidate(); }
        void OnBlur() override { focused = false; Invalidate(); }

        void OnMouseEnter() override { m_hover = true; Invalidate(); }
        void OnMouseLeave() override {
            if (m_hover || m_pressed) { m_hover = false; m_pressed = false; Invalidate(); }
        }

        bool OnMouse(const xMouse& ev) override {
            if (!isEnabled) return false;
            switch (ev.action) {
            case MouseAction::Move:
                if (!m_hover) { m_hover = true; Invalidate(); }
                return true;
            case MouseAction::Down:
            case MouseAction::DoubleClick:
                m_hover = true;
                m_pressed = true;
                Invalidate();
                return true;
            case MouseAction::Up: {
                const bool wasPressed = m_pressed;
                m_pressed = false;
                Invalidate();
                if (wasPressed && HitTest(ev.x, ev.y)) {
                    Trigger();
                    return true;
                }
                return wasPressed;
            }
            default:
                return false;
            }
        }

        [[nodiscard]] bool IsHovered() const { return m_hover; }

        bool OnKey(const xChar& key) override {
            if (!isEnabled) return false;
            if (key == 13 || key == 10) { // Enter
                Trigger();
                return true;
            }
            if (key == ' ' || key == 32) { // Space
                Trigger();
                return true;
            }
            return false;
        }

        // 消息：外部/延时点击都用队列驱动（"click"），不直接调 Trigger
        bool OnMessage(const Message& msg) override {
            if (!isEnabled) return false;
            if (msg.type == "click") { Trigger(); return true; }
            if (msg.type == "setText") { SetLabel(msg.text); return true; }
            return UI::OnMessage(msg);
        }

        //---------- 绘制 ----------
        // 底色一律继承父容器；图1 的状态只体现在「文字色 + 边框」上
        [[nodiscard]] std::pair<Color, Color> StateColors() const {
            const Color bg = EffectiveBackground();
            if (!isEnabled) return { Theme::Current().textDisabled, bg };
            return { normalFg, bg };
        }

        void OnRender(ConsoleBackBuffer& buf) override {
            const auto [fg, bg] = StateColors();
            const Theme& th = Theme::Current();

            Color edge = isEnabled ? borderColor : th.borderDisabled;
            if (isEnabled && (m_hover || m_pressed)) edge = hotBorderColor;

            buf.FillStyle(bounds, Style(fg, bg));

            // 文字：焦点 / 按下时加粗（不反色、不动背景）
            Style labelStyle = Style(fg, bg);
            if (isEnabled && (focused || m_pressed)) labelStyle = labelStyle.SetBold();

            const BoxChars& glyphs = (focused && thickBorderOnFocus && showFrame) ? Box::Double : frame;
            const Style borderStyle = Style(edge, bg);

            if (showFrame && Height() >= 3 && Width() >= 3) {
                buf.DrawBox(bounds, glyphs, borderStyle);
                const int innerX = Left() + 1;
                const int innerW = Width() - 2;
                const int labelY = Top() + Height() / 2;
                const std::string shown = Utf8::Truncate(label, innerW);
                const int lx = innerX + std::max((innerW - Utf8::DisplayWidth(shown)) / 2, 0);
                buf.WriteText(lx, labelY, shown, labelStyle, innerX + innerW - lx);
            } else {
                const std::string shown = Utf8::Truncate(label, Width());
                const int lx = Left() + std::max((Width() - Utf8::DisplayWidth(shown)) / 2, 0);
                buf.WriteText(lx, Top(), shown, labelStyle, Right() - lx);
            }
        }

    private:
        bool m_pressed{ false };
        bool m_hover{ false };
        bool borderColorSet{ false };
    };

    using UIButton = Button;
}
