//
// Terminal / Components / TerminalSlider.hpp
// 滑杆元件：标签 + 已填充轨道 + 滑块 + 数值，键盘驱动，配色可定制。
//

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>

#include "../Core/UI.hpp"
#include "../Core/TerminalBuffer.hpp"
#include "../Core/TerminalMessage.hpp"
#include "../Core/TerminalMouse.hpp"
#include "../Utils/BoxChars.hpp"

namespace Terminal {

    class Slider : public UI {
    public:
        

        std::string label{};
        double min{ 0.0 };
        double max{ 100.0 };
        double value{ 50.0 };
        double step{ 1.0 };

        int barWidth{ 20 };      // <=0 表示占满剩余宽度
        bool showValue{ true };
        int precision{ 0 };      // 小数位

        std::wstring trackEmpty{ L'─' };
        std::wstring trackFilled{ L'━' };
        std::wstring thumbChar{};     // 空 = 使用 Glyphs::Dot()（兼容模式自动变 ·）
        wchar_t bracketLeft{ L'[' };
        wchar_t bracketRight{ L']' };

        std::function<void(double)> onChanged{};
        std::function<void(double)> onCommitted{};

        Slider() { signature = "UISlider";
            focusable = true;
            mouseCursor = MouseCursor::Hand;
            style = Style(Theme::Current().text, Theme::Current().background);
        }

        Slider(const std::string& text, const double lo, const double hi, const double val)
            : Slider() { signature = "UISlider";
            label = text;
            min = lo;
            max = hi;
            SetValue(val);
        }

        //---------- 配置 ----------
        Slider& SetLabel(const std::string& text) { label = text; return *this; }
        Slider& SetRange(const double lo, const double hi) {
            min = lo;
            max = hi;
            SetValue(value);
            return *this;
        }
        Slider& SetStep(const double s) { step = (s > 0.0 ? s : 1.0); return *this; }
        Slider& SetBarWidth(const int w) { barWidth = w; return *this; }
        Slider& SetPrecision(const int p) { precision = std::max(p, 0); return *this; }
        Slider& OnChange(std::function<void(double)> cb) { onChanged = std::move(cb); return *this; }
        // 拖拽抬起后触发：需要提交到外部（例如媒体 seek）的逻辑放这里，避免拖动中每格都阻塞。
        Slider& OnCommit(std::function<void(double)> cb) { onCommitted = std::move(cb); return *this; }
        [[nodiscard]] bool IsDragging() const { return m_dragging; }

        void SetValue(const double v, const bool notify = true) {
            const double lo = std::min(min, max);
            const double hi = std::max(min, max);
            const double next = std::clamp(v, lo, hi);
            if (std::fabs(next - value) < 1e-9) return;
            value = next;
            Invalidate();
            if (notify && onChanged) onChanged(value);
        }

        [[nodiscard]] double GetValue() const { return value; }
        [[nodiscard]] double Ratio() const {
            const double span = max - min;
            if (std::fabs(span) < 1e-9) return 0.0;
            return std::clamp((value - min) / span, 0.0, 1.0);
        }

        //---------- 度量 ----------
        [[nodiscard]] int MeasureWidth() const override {
            if (layout.width >= 0) return layout.width;
            const int labelW = label.empty() ? 0 : Utf8::DisplayWidth(label) + 1;
            const int bar = barWidth > 0 ? barWidth : 20;
            const int valueW = showValue ? 7 : 0;
            return labelW + bar + 2 + valueW;
        }

        [[nodiscard]] int MeasureHeight() const override {
            if (layout.height >= 0) return layout.height;
            return 1;
        }

        //---------- 交互 ----------
        void Step(const int times) {
            const double lo = std::min(min, max);
            const double hi = std::max(min, max);
            const double delta = step * times;
            if (delta == 0.0) return;
            const double next = std::clamp(value + delta, lo, hi);
            if (std::fabs(next - value) > 1e-9) {
                value = next;
                Invalidate();
                if (onChanged) onChanged(value);
            }
        }

        void SetByRatio(const double r) {
            const double span = max - min;
            SetValue(min + span * std::clamp(r, 0.0, 1.0));
        }

        bool OnKey(const xChar& key) override {
            if (!isEnabled) return false;
            if (IsKey(key, SKey::LeftArrow) || key == '-' || key == 'h' || key == 'H') { Step(-1); return true; }
            if (IsKey(key, SKey::RightArrow) || key == '+' || key == 'l' || key == 'L') { Step(1); return true; }
            if (IsKey(key, SKey::PageUp) || IsKey(key, SKey::PageDown)) {
                const double big = std::max(step, (max - min) / 10.0);
                const int times = static_cast<int>(std::round(big / step));
                Step(IsKey(key, SKey::PageUp) ? times : -times);
                return true;
            }
            if (IsKey(key, SKey::Home)) { SetValue(min); return true; }
            if (IsKey(key, SKey::End)) { SetValue(max); return true; }
            return false;
        }

        // 消息："setValue" 设置数值（会触发 OnChange），"step" 走步进
        bool OnMessage(const Message& msg) override {
            if (!isEnabled) return false;
            if (msg.type == "setValue") { SetValue(msg.number); return true; }
            if (msg.type == "step") { Step(static_cast<int>(msg.number)); return true; }
            return UI::OnMessage(msg);
        }

        //---------- 鼠标 ----------
        // 轨道几何：与 OnRender 里的算法保持一致，供点击/拖拽换算比例
        struct TrackGeom {
            int trackX{ 0 };
            int trackW{ 0 };
            bool valid{ false };
        };

        [[nodiscard]] TrackGeom Track() const {
            int x = Left();
            if (!label.empty()) {
                const std::string shown = Utf8::Truncate(label, std::max(Right() - x - 4, 1));
                x += Utf8::DisplayWidth(shown) + 1;
            }
            const int remaining = Right() - x;
            if (remaining <= 2) return TrackGeom{ x + 1, 1, false };

            const int valueW = showValue ? 7 : 0;
            int bar = barWidth > 0 ? std::min(barWidth, remaining - valueW - 2) : remaining - valueW - 2;
            bar = std::max(bar, 1);
            return TrackGeom{ x + 1, bar, true };
        }

        // 鼠标位置 -> 比例。cellX 是亚格坐标（原始输入可用时带小数），
        // 与绘制保持一致：轨道占 [trackX, trackX + trackW)，thumb 落在 round(r * trackW)。
        void SetByPosition(const double cellX) {
            const TrackGeom g = Track();
            if (!g.valid) return;
            const double r = (cellX - static_cast<double>(g.trackX)) / static_cast<double>(std::max(g.trackW, 1));
            SetByRatio(r);
        }

        // 只有单元格坐标时的入口（等价于整数亚格坐标）
        void SetByPixel(const int mouseX) { SetByPosition(static_cast<double>(mouseX)); }

        void OnMouseEnter() override { m_hover = true; Invalidate(); }
        // 拖拽被捕获后指针可能滑出元件范围：只清悬停，不能顺手断掉拖拽，
        // 否则拖出轨道边界就“滑不动了”（比例夹取交给 SetByPosition 的 clamp）。
        void OnMouseLeave() override {
            if (m_hover) { m_hover = false; Invalidate(); }
        }

        bool OnMouse(const xMouse& ev) override {
            if (!isEnabled) return false;
            switch (ev.action) {
            case MouseAction::Move:
                if (!m_hover) { m_hover = true; Invalidate(); }
                if (m_dragging) { SetByPosition(ev.FineX()); return true; }
                return true;
            case MouseAction::Down:
            case MouseAction::DoubleClick:
                m_hover = true;
                m_dragging = true;
                SetByPosition(ev.FineX());
                Invalidate();
                return true;
            case MouseAction::Up:
                if (m_dragging) {
                    // 抬起时不再按单元格坐标补一次，避免把亚格精度拖到最后又弹回格边界
                    m_dragging = false;
                    Invalidate();
                    if (onCommitted) onCommitted(value);
                    return true;
                }
                return false;
            default:
                return false;
            }
        }

        //---------- 绘制 ----------
        void OnRender(ConsoleBackBuffer& buf) override {
            const Theme& th = Theme::Current();
            const Color bg = EffectiveBackground();   // 底色继承父容器
            if (style.opaque) buf.FillStyle(bounds, style);

            Color labelColor = isEnabled ? (focused || m_dragging ? th.borderFocus : (m_hover ? th.text : th.textMuted)) : th.textDisabled;
            Color filled = isEnabled ? th.trackFill : th.borderDisabled;
            Color empty = isEnabled ? th.track : th.borderDisabled;
            Color thumbColor = isEnabled ? th.thumb : th.textDisabled;
            Color valueColor = isEnabled ? th.text : th.textDisabled;

            const int y = Top();

            int x = Left();
            if (!label.empty()) {
                const std::string shown = Utf8::Truncate(label, std::max(Right() - x - 4, 1));
                x += buf.WriteText(x, y, shown, Style(labelColor, bg), Right() - x);
                x += 1;
            }

            int remaining = Right() - x;
            if (remaining <= 2) return;

            const int valueW = showValue ? 7 : 0;
            int bar = barWidth > 0 ? std::min(barWidth, remaining - valueW - 2) : remaining - valueW - 2;
            bar = std::max(bar, 1);

            // [ 轨道 ]
            if (x < Right()) buf.SetCell(x, y, bracketLeft, Style(empty, bg));
            const int trackX = x + 1;
            const int trackW = bar;
            const int filledW = static_cast<int>(std::lround(Ratio() * trackW));
            const int thumbX = trackX + std::clamp(filledW, 0, trackW - 1);

            for (int i = 0; i < trackW; ++i) {
                const bool isFill = i < filledW;
                const std::wstring& glyph = isFill ? trackFilled : trackEmpty;
                buf.SetCell(trackX + i, y, glyph.empty() ? L' ' : glyph[0],
                            Style(isFill ? filled : empty, bg));
            }
            // 滑块
            buf.SetCell(thumbX, y, thumbChar.empty() ? Glyphs::Dot() : thumbChar[0],
                        Style(isEnabled ? thumbColor : empty, bg).SetBold());
            buf.SetCell(x + 1 + trackW, y, bracketRight, Style(empty, bg));

            // 数值
            if (showValue) {
                char number[32];
                if (precision > 0)
                    std::snprintf(number, sizeof(number), " %.*f", precision, value);
                else
                    std::snprintf(number, sizeof(number), " %.0f", value);
                int vx = x + 1 + trackW + 2;
                if (vx < Right())
                    buf.WriteText(vx, y, std::string(number), Style(valueColor, bg), Right() - vx);
            }
        }

    private:
        bool m_hover{ false };
        bool m_dragging{ false };
    };

    using UISlider = Slider;
}
