//
// Terminal / Components / TerminalChoiceGroup.hpp
// 单选 / 多选组：键盘上下移动，Space/Enter 切换；鼠标点击行即可选中。
// 选择变化通过消息队列发送（"change"），交互动作本身也走队列（"toggle"）。
//

#pragma once

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "../Core/TerminalMessage.hpp"
#include "../Core/TerminalMouse.hpp"
#include "TerminalText.hpp"

namespace Terminal {

    class ChoiceGroup : public UI {
    public:
        std::vector<std::string> options{};
        std::vector<bool> selected{};
        int cursor{ 0 };
        bool multi{ false };
        bool showHint{ true };
        std::function<void(const std::vector<int>&)> onChange{};

        ChoiceGroup() {
            signature = "UIChoiceGroup";
            focusable = true;
            layout.height = 0;
            style = Style(Theme::Current().text, Theme::Current().background);
        }

        explicit ChoiceGroup(const std::vector<std::string>& items) : ChoiceGroup() {
            SetOptions(items);
        }

        ChoiceGroup& SetOptions(const std::vector<std::string>& items) {
            options = items;
            selected.assign(items.size(), false);
            cursor = std::clamp(cursor, 0, std::max(static_cast<int>(options.size()) - 1, 0));
            InvalidateLayout();
            return *this;
        }

        ChoiceGroup& SetMulti(const bool on) {
            multi = on;
            if (!multi && !selected.empty()) {
                // 单选模式保留第一个已选项，其余清空
                int keep = -1;
                for (int i = 0; i < static_cast<int>(selected.size()); ++i)
                    if (selected[i]) { keep = i; break; }
                std::fill(selected.begin(), selected.end(), false);
                if (keep >= 0) selected[keep] = true;
            }
            Invalidate();
            return *this;
        }

        ChoiceGroup& SetSelected(const int index, const bool on = true) {
            if (index < 0 || index >= static_cast<int>(selected.size())) return *this;
            if (!multi) std::fill(selected.begin(), selected.end(), false);
            selected[index] = on;
            Invalidate();
            return *this;
        }

        [[nodiscard]] std::vector<int> SelectedIndices() const {
            std::vector<int> out;
            for (int i = 0; i < static_cast<int>(selected.size()); ++i)
                if (selected[i]) out.push_back(i);
            return out;
        }

        [[nodiscard]] int SelectedIndex() const {
            for (int i = 0; i < static_cast<int>(selected.size()); ++i)
                if (selected[i]) return i;
            return -1;
        }

        [[nodiscard]] int MeasureWidth() const override {
            if (layout.width >= 0) return layout.width;
            int w = 4;
            for (const auto& s : options) w = std::max(w, Utf8::DisplayWidth(s) + 5);
            return w;
        }
        [[nodiscard]] int MeasureHeight() const override {
            if (layout.height >= 0) return layout.height;
            return std::max(static_cast<int>(options.size()), 1);
        }

        bool OnKey(const xChar& key) override {
            if (options.empty()) return false;
            if (key == SKey::UpArrow) {
                cursor = std::max(cursor - 1, 0);
                Invalidate();
                return true;
            }
            if (key == SKey::DownArrow) {
                cursor = std::min(cursor + 1, static_cast<int>(options.size()) - 1);
                Invalidate();
                return true;
            }
            if (key == SKey::Space || key == SKey::Enter) {
                Json payload = MsgJson::Object();
                payload["index"] = Json(static_cast<double>(cursor));
                MessageQueue::PostFrom(this, this, "toggle", std::move(payload));
                return true;
            }
            return false;
        }

        bool OnMouse(const xMouse& ev) override {
            if (ev.action != MouseAction::Down || !ev.IsLeft() || options.empty()) return false;
            const int row = ev.y - Top();
            if (row < 0 || row >= static_cast<int>(options.size())) return false;
            cursor = row;
            Json payload = MsgJson::Object();
            payload["index"] = Json(static_cast<double>(row));
            MessageQueue::PostFrom(this, this, "toggle", std::move(payload));
            return true;
        }

        bool OnMessage(const Message& msg) override {
            if (msg.type == "toggle") {
                int index = cursor;
                if (msg.payload.IsObject() && msg.payload.contains_key("index")) {
                    const Json v = msg.payload.get("index");
                    if (v.IsNumber()) index = static_cast<int>(v.GetNumber());
                }
                if (index < 0 || index >= static_cast<int>(selected.size())) return true;
                cursor = index;
                if (multi) {
                    selected[index] = !selected[index];
                } else {
                    const bool next = !selected[index];
                    std::fill(selected.begin(), selected.end(), false);
                    selected[index] = next;
                }
                Invalidate();
                if (onChange) onChange(SelectedIndices());

                Message changed;
                changed.sender = this;
                changed.target = this;
                changed.type = "change";
                changed.payload = MsgJson::Object();
                Json arr = MsgJson::Array();
                for (int i : SelectedIndices()) arr.GetArray().push_back(Json(static_cast<double>(i)));
                changed.payload["indices"] = arr;
                changed.payload["index"] = Json(static_cast<double>(cursor));
                changed.payload["multi"] = Json(multi);
                MessageQueue::Post(changed);
                return true;
            }
            if (msg.type == "set") {
                int index = cursor;
                bool on = true;
                if (msg.payload.IsObject()) {
                    if (msg.payload.contains_key("index")) {
                        const Json v = msg.payload.get("index");
                        if (v.IsNumber()) index = static_cast<int>(v.GetNumber());
                    }
                    if (msg.payload.contains_key("value")) {
                        const Json v = msg.payload.get("value");
                        if (v.IsBool()) on = v.GetBool();
                    }
                }
                SetSelected(index, on);
                if (onChange) onChange(SelectedIndices());
                return true;
            }
            return UI::OnMessage(msg);
        }

        void OnRender(ConsoleBackBuffer& buf) override {
            const Theme& th = Theme::Current();
            const Color bg = EffectiveBackground();
            buf.FillStyle(bounds, Style(th.text, bg));
            for (int i = 0; i < static_cast<int>(options.size()); ++i) {
                const int y = Top() + i;
                if (y >= Bottom()) break;
                const Color fg = selected[i] ? th.accent : th.text;
                const bool rowFocus = focused && i == cursor;
                if (rowFocus) buf.FillStyle(Bounds{ Left(), y, Width(), 1 }, Style(fg, th.hoverBg));
                const std::string mark = multi
                    ? (selected[i] ? "[x]" : "[ ]")
                    : (selected[i] ? "(*)" : "( )");
                const std::string line = mark + " " + options[i];
                buf.WriteText(Left(), y, Utf8::Truncate(line, Width()),
                              Style(fg, rowFocus ? th.hoverBg : bg).SetBold(rowFocus), Width());
            }
            if (options.empty() && showHint) {
                buf.WriteText(Left(), Top(), Utf8::Truncate("(empty)", Width()), Style(th.textMuted, bg), Width());
            }
        }
    };

    using UISingleChoice = ChoiceGroup;
    using UIMultiChoice = ChoiceGroup;
    using UISelect = ChoiceGroup;
    using UICheck = ChoiceGroup;

} // namespace Terminal
