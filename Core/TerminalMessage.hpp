//
// Terminal / Core / TerminalMessage.hpp
// 消息队列：UI 的交互回调（OnClick / OnChange / 鼠标等）只负责「投递消息」，
// 真正的状态变更统一在主 While() 里由消息携带的对象自己处理。
//
//   // 交互回调里（触发函数）
//   btn->onClick = [&]{ MessageQueue::Post(label.get(), "setText", "已提交"); };
//
//   // 主循环
//   while (running) {
//       MessageQueue::ProcessAll();   // 每条消息 -> msg.target->OnMessage(msg)
//       ...
//   }
//
// 消息自己带目标对象（UI*），处理时不需要再查表；对象实现 OnMessage 即可。
//

#pragma once

#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "TerminalJson.hpp"
#include "UI.hpp"

namespace Terminal {

    // JsonCore 承载；这里保留消息负载的工厂写法
    namespace MsgJson {
        inline Json Object() { return Json(std::unordered_map<std::string, Json>{}); }
        inline Json Array() { return Json(std::vector<Json>{}); }
        inline Json Number(const double v) { return Json(v); }
        inline Json Bool(const bool v) { return Json(v); }
    }

    //------------------------------------------------------------------
    // 消息
    //------------------------------------------------------------------
    struct Message {
        UI* sender{ nullptr };      // 发出消息的控件（地址）
        UI* target{ nullptr };      // 处理对象：处理时调它的 OnMessage
        std::string type;           // "click" / "change" / "toggle" / 自定义
        Json payload;               // 结构化负载（对象/数组/字符串都可以）
        std::string text;           // 兼容旧代码的文本负载
        double number{ 0.0 };       // 兼容旧代码的数值负载
        int x{ 0 };
        int y{ 0 };

        Message() = default;
        Message(UI* t, std::string ty, std::string tx = {}, const double n = 0.0)
            : target(t), type(std::move(ty)), text(std::move(tx)), number(n) {
            payload = MsgJson::Object();
            payload["text"] = Json(text);
            payload["number"] = Json(number);
        }
        Message(UI* t, std::string ty, Json data, UI* from = nullptr)
            : sender(from), target(t), type(std::move(ty)), payload(std::move(data)) {}
    };

    //------------------------------------------------------------------
    // 局部队列：PageHolder 每个页面一份，不经过全局单例
    //------------------------------------------------------------------
    class LocalMessageQueue {
    public:
        void Post(const Message& msg) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_queue.push_back(msg);
        }

        bool Pop(Message& out) {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_queue.empty()) return false;
            out = std::move(m_queue.front());
            m_queue.pop_front();
            return true;
        }

        [[nodiscard]] std::size_t Size() const {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_queue.size();
        }

        [[nodiscard]] bool Empty() const { return Size() == 0; }

        void Clear() {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_queue.clear();
        }

    private:
        mutable std::mutex m_mutex;
        std::deque<Message> m_queue;
    };

    //------------------------------------------------------------------
    // 队列（进程内单例，单线程主循环消费；投递侧加锁以便日后后台线程使用）
    //------------------------------------------------------------------
    namespace Detail {
        inline std::deque<Message>& MessageQueueStorage() {
            static std::deque<Message> q;
            return q;
        }

        inline std::mutex& MessageQueueMutex() {
            static std::mutex m;
            return m;
        }
    } // namespace Detail

    class MessageQueue {
    public:
        // 投递一条消息
        static void Post(const Message& msg) {
            std::lock_guard<std::mutex> lock(Detail::MessageQueueMutex());
            Detail::MessageQueueStorage().push_back(msg);
        }

        static void Post(UI* target, std::string type, std::string text = {}, const double number = 0.0) {
            Post(Message{ target, std::move(type), std::move(text), number });
        }

        // 带发送者 + Json 负载的新入口
        static void PostFrom(UI* sender, UI* target, std::string type, Json payload = MsgJson::Object()) {
            Post(Message{ target, std::move(type), std::move(payload), sender });
        }

        // 处理队首一条：交给消息携带的对象；对象为空/已失效则丢弃
        static bool ProcessOne() {
            Message msg;
            {
                std::lock_guard<std::mutex> lock(Detail::MessageQueueMutex());
                if (Detail::MessageQueueStorage().empty()) return false;
                msg = std::move(Detail::MessageQueueStorage().front());
                Detail::MessageQueueStorage().pop_front();
            }
            if (msg.target != nullptr) msg.target->OnMessage(msg);
            return true;
        }

        // 处理当前队列里的所有消息（含处理过程中新投递的），maxCount=0 表示不限
        static std::size_t ProcessAll(const std::size_t maxCount = 0) {
            std::size_t n = 0;
            while (ProcessOne()) {
                ++n;
                if (maxCount != 0 && n >= maxCount) break;
            }
            return n;
        }

        [[nodiscard]] static std::size_t Size() {
            std::lock_guard<std::mutex> lock(Detail::MessageQueueMutex());
            return Detail::MessageQueueStorage().size();
        }

        [[nodiscard]] static bool Empty() { return Size() == 0; }

        static void Clear() {
            std::lock_guard<std::mutex> lock(Detail::MessageQueueMutex());
            Detail::MessageQueueStorage().clear();
        }
    };

} // namespace Terminal
