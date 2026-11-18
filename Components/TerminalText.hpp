//
// Created by Administrator on 2026/6/4.
// 修订：改造成可参与布局的 Text 元件 —— 支持多行、对齐、前后景配色、禁用态。
//

#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "../Core/UI.hpp"
#include "../Core/TerminalBuffer.hpp"
#include "../Core/TerminalMessage.hpp"
#include "../Utils/Outputs.hpp"

namespace Terminal {

    /**
     * @brief 静态文本。
     *        文本中的 '\n' 会被拆成多行；对齐由 align 控制（Left / Center / Right）。
     */
    class Text : public UI {
    public:
        

        Text() { signature = "UIText";
            style = Style(Theme::Current().text, Theme::Current().background);
            signature = "UIText";
        }

        explicit Text(const std::string& text) : Text() { signature = "UIText"; SetText(text); }

        Text(const std::string& text, const Position& pt) : Text() { signature = "UIText";
            SetText(text);
            SetPosition(pt);
        }

        // 兼容旧接口：用控制台属性 DWORD 指定颜色
        Text(const std::string& text, const Position& pt, const DWORD attr) : Text() { signature = "UIText";
            SetText(text);
            SetPosition(pt);
            style = Style::FromAttr(attr);
        }

        Text(const std::string& text, const Position& pt, const Style& st) : Text() { signature = "UIText";
            SetText(text);
            SetPosition(pt);
            style = st;
        }

        //---------- 内容 ----------
        void SetText(const std::string& text) {
            if (m_text == text) return;
            const int oldW = MeasureWidth();
            const std::size_t oldLines = m_lines.size();
            m_text = text;
            RebuildLines();
            // 文本高度/自然宽度没变时只重绘这一块，避免每帧歌词变化都把
            // layoutDirty 冒泡到根节点、触发整页重排。
            if (m_lines.size() != oldLines || MeasureWidth() != oldW) InvalidateLayout();
            else Invalidate();
        }

        [[nodiscard]] std::string GetText() const { return m_text; }

        void SetLines(const std::vector<std::string>& ls) {
            m_lines = ls;
            m_text.clear();
            for (size_t i = 0; i < m_lines.size(); ++i) {
                if (i) m_text += '\n';
                m_text += m_lines[i];
            }
        }

        [[nodiscard]] const std::vector<std::string>& Lines() const { return m_lines; }

        //---------- 对齐 ----------
        void SetAlign(const LayoutPos a) { if (align == a) return; align = a; Invalidate(); }
        [[nodiscard]] LayoutPos GetAlign() const { return align; }

        // 是否用禁用色渲染（跟随 isEnabled 自动）
        [[nodiscard]] virtual bool UseDisabledColor() const { return !isEnabled; }

        [[nodiscard]] Style EffectiveStyle() const {
            if (UseDisabledColor()) {
                Style s = style;
                s.fg = Theme::Current().textDisabled;
                return s;
            }
            return style;
        }

        //---------- 度量 ----------
        [[nodiscard]] int MeasureWidth() const override {
            if (layout.width >= 0) return layout.width;
            int w = 0;
            for (const auto& line : m_lines) w = std::max(w, Utf8::DisplayWidth(line));
            return w;
        }

        [[nodiscard]] int MeasureHeight() const override {
            if (layout.height >= 0) return layout.height;
            return std::max(static_cast<int>(m_lines.size()), m_lines.empty() ? 1 : 0);
        }

        //---------- 绘制 ----------
        void OnRender(ConsoleBackBuffer& buf) override {
            if (m_lines.empty()) return;
            Style st = EffectiveStyle();
            if (!style.opaque) st.bg = EffectiveBackground();   // 底色继承父容器，不自己取主题色

            const int h = Height();
            int y = Top();
            const int lineCount = static_cast<int>(m_lines.size());
            if (h > lineCount && align == LayoutPos::Center) y += (h - lineCount) / 2;

            for (int i = 0; i < lineCount && y < Bottom(); ++i, ++y) {
                const std::string& line = m_lines[static_cast<size_t>(i)];
                if (line.empty()) continue;
                int x = Left();
                const int textW = Utf8::DisplayWidth(line);
                switch (align) {
                case LayoutPos::Right:
                case LayoutPos::RightTop:
                case LayoutPos::RightBottom:
                    x = Right() - textW;
                    break;
                case LayoutPos::Center:
                    x = Left() + (Width() - textW) / 2;
                    break;
                default:
                    x = Left();
                    break;
                }
                buf.WriteText(x, y, line, st, Right() - x);
            }
        }

        // 文本可聚焦（用于可滚动文本等场景，默认关闭）
        bool OnKey(const xChar& /*key*/) override { return false; }

        // 消息：settleText / "setText" → 改文字；"append" 追加
        bool OnMessage(const Message& msg) override {
            if (msg.type == "setText") { SetText(msg.text); return true; }
            if (msg.type == "append") { SetText(GetText() + msg.text); return true; }
            if (msg.type == "clear") { SetText(""); return true; }
            return UI::OnMessage(msg);
        }

    private:
        void RebuildLines() {
            m_lines.clear();
            std::string cur;
            for (const char c : m_text) {
                if (c == '\n') {
                    m_lines.push_back(cur);
                    cur.clear();
                } else if (c != '\r') {
                    cur.push_back(c);
                }
            }
            m_lines.push_back(cur);
        }

        std::string m_text{};
        std::vector<std::string> m_lines{ std::string{} };
        LayoutPos align{ LayoutPos::Left };
    };

    using UIText = Text;
}
