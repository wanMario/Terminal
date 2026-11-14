//
// Terminal / Components / TerminalPageHolder.hpp
// 可嵌套页面容器：每个页面有独立的局部消息队列。
// 页面内消息先由本页处理；目标对象没有消费时，消息会冒泡到上一层 PageHolder。
//

#pragma once

#include <memory>
#include <string>

#include "../Core/TerminalMessage.hpp"
#include "TerminalPanel.hpp"

namespace Terminal {

    class PageHolder : public Panel {
    public:
        std::string pageName{};
        bool active{ true };
        PageHolder* parentPage{ nullptr };
        LocalMessageQueue messages{};

        PageHolder() {
            signature = "UIPageHolder";
            style = Style(Theme::Current().text, Theme::Current().background);
        }

        explicit PageHolder(const std::string& name) : PageHolder() { pageName = name; }

        PageHolder& SetName(const std::string& name) { pageName = name; return *this; }

        // 页面级显隐；消息队列不会因为隐藏而清空
        PageHolder& SetActive(const bool on) {
            active = on;
            isVisible = on;
            if (!on && parent) parent->InvalidateLayout();
            Invalidate();
            return *this;
        }

        void OnMount() override {
            parentPage = nullptr;
            for (UI* p = parent; p != nullptr; p = p->parent) {
                if (auto* page = dynamic_cast<PageHolder*>(p)) {
                    parentPage = page;
                    break;
                }
            }
            Panel::OnMount();
        }

        //---------- 局部消息 ----------
        void PostLocal(const Message& msg) {
            Message copy = msg;
            if (copy.sender == nullptr) copy.sender = this;
            messages.Post(copy);
        }

        void PostLocal(UI* target, const std::string& type, Json payload = MsgJson::Object()) {
            PostLocal(Message{ target, type, std::move(payload), this });
        }

        void PostLocal(UI* target, const std::string& type, const std::string& text,
                       const double number = 0.0) {
            Message msg{ target, type, text, number };
            msg.sender = this;
            messages.Post(msg);
        }

        // 把一条未消费的消息交给上一层；根页面返回 false
        bool SendUp(Message msg) {
            if (parentPage == nullptr) return false;
            msg.target = parentPage;
            parentPage->messages.Post(msg);
            return true;
        }

        // 处理本页队列；target 返回 false 的消息自动冒泡到 parentPage
        std::size_t ProcessLocal(const std::size_t maxCount = 0) {
            std::size_t n = 0;
            Message msg;
            while (messages.Pop(msg)) {
                ++n;
                const bool handled = msg.target != nullptr && msg.target->OnMessage(msg);
                if (!handled) SendUp(msg);
                if (maxCount != 0 && n >= maxCount) break;
            }
            return n;
        }

        // PageHolder 自身收到的页面级消息
        bool OnMessage(const Message& msg) override {
            if (msg.type == "page:activate") { SetActive(true); return true; }
            if (msg.type == "page:deactivate") { SetActive(false); return true; }
            if (msg.type == "page:sendUp") { return SendUp(msg); }
            return Panel::OnMessage(msg);
        }

        [[nodiscard]] std::size_t PendingLocal() const { return messages.Size(); }
    };

    using UIPage = PageHolder;

} // namespace Terminal
