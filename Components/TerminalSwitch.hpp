//
// Terminal / Components / TerminalSwitch.hpp
// 开关控件：显示 ON/OFF，Space/Enter 切换，状态变化通过消息队列通知。
//

#pragma once

#include <functional>
#include <string>

#include "../Core/TerminalMessage.hpp"
#include "../Core/TerminalMouse.hpp"
#include "../Utils/TerminalChar.hpp"
#include "TerminalText.hpp"

namespace Terminal {

    class Switch : public UI {
    public:
        bool value{ false };
        std::string label{ "开关" };
        std::string onText{ "ON" };
        std::string offText{ "OFF" };
        Color onColor{ Theme::Current().accent };
        Color offColor{ Theme::Current().textMuted };
        std::function<void(bool)> onChange{};

        Switch() {
            signature = "UISwitch";
            focusable = true;
            mouseCursor = MouseCursor::Hand;
            layout.height = 1;
            style = Style(Theme::Current().text, Theme::Current().background);
        }

        explicit Switch(const bool on) : Switch() { value = on; }

        Switch& SetLabel(const std::string& text) { label = text; InvalidateLayout(); return *this; }
        Switch& SetValue(const bool on, const bool notify = false) {
            if (value == on) return *this;
            value = on;
            Invalidate();
            if (notify && onChange) onChange(value);
            return *this;
        }
        Switch& SetColors(const Color& on, const Color& off) {
            onColor = on;
            offColor = off;
            Invalidate();
            return *this;
        }

        [[nodiscard]] int MeasureWidth() const override {
            return layout.width >= 0 ? layout.width
                                      : Utf8::DisplayWidth(label) + 1 + 2 + 1 + Utf8::DisplayWidth(onText) + 1 + 1;
        }
        [[nodiscard]] int MeasureHeight() const override { return 1; }

        bool OnKey(const xChar& key) override {
            if (key == SKey::Enter || key == SKey::Space) {
                MessageQueue::PostFrom(this, this, "toggle");
                return true;
            }
            return false;
        }

        bool OnMessage(const Message& msg) override {
            if (msg.type == "toggle") {
                value = !value;
                Invalidate();
                if (onChange) onChange(value);
                Message changed;
                changed.sender = this;
                changed.target = this;
                changed.type = "change";
                changed.payload = MsgJson::Object();
                changed.payload["value"] = Json(value);
                MessageQueue::Post(changed);
                return true;
            }
            if (msg.type == "set") {
                bool next = value;
                if (msg.payload.IsObject() && msg.payload.contains_key("value")) {
                    const Json v = msg.payload.get("value");
                    if (v.IsBool()) next = v.GetBool();
                    else if (v.IsNumber()) next = v.GetNumber() != 0.0;
                }
                if (next != value) {
                    value = next;
                    Invalidate();
                    if (onChange) onChange(value);
                }
                return true;
            }
            return UI::OnMessage(msg);
        }

        //---------- 鼠标 ----------
        // 整行都是热区：悬停高亮，左键按下即投递 toggle（点击的落点不在文字上也算）
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
                if (!ev.IsLeft()) return false;
                m_hover = true;
                m_pressed = true;
                Invalidate();
                return true;
            case MouseAction::Up:
                if (!m_pressed) return false;
                m_pressed = false;
                Invalidate();
                if (HitTest(ev.x, ev.y)) MessageQueue::PostFrom(this, this, "toggle");
                return true;
            default:
                return false;
            }
        }

        void OnRender(ConsoleBackBuffer& buf) override {
            const Theme& th = Theme::Current();
            const Color bg = EffectiveBackground();
            const Color fg = value ? onColor : offColor;
            buf.FillStyle(bounds, Style(th.text, bg));
            if (focused || m_hover) buf.FillStyle(bounds, Style(th.text, th.hoverBg));
            const std::string shown = label.empty()
                ? std::string("[ ") + (value ? onText : offText) + " ]"
                : label + " [ " + (value ? onText : offText) + " ]";
            buf.WriteText(Left(), Top(), Utf8::Truncate(shown, Width()), Style(fg, bg).SetBold(), Width());
        }

    private:
        bool m_hover{ false };
        bool m_pressed{ false };
    };

    using UISwitch = Switch;

} // namespace Terminal
