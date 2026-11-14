//
// Created by Administrator on 2026/5/24.
// 修订：把原来的半成品 UIFocusable 换成可用的 FocusManager（焦点环 + 按键分发）
//

#pragma once

#include <algorithm>
#include <cstddef>
#include <memory>
#include <vector>

#include "UI.hpp"
#include "Input.hpp"

namespace Terminal {

    /**
     * @brief 焦点管理：深度优先收集可聚焦元件，Tab 轮转，其余按键交给当前焦点。
     */
    class FocusManager {
    public:
        static FocusManager& Instance() {
            static FocusManager mgr;
            return mgr;
        }

        void SetRoot(UI* root) {
            m_root = root;
            m_dirty = true;
        }

        [[nodiscard]] UI* Root() const { return m_root; }

        void Invalidate() { m_dirty = true; }

        void Clear() {
            m_items.clear();
            m_index = static_cast<size_t>(-1);
            m_root = nullptr;
            m_dirty = true;
        }

        [[nodiscard]] size_t Count() {
            EnsureCollected();
            return m_items.size();
        }

        [[nodiscard]] UI* Current() {
            EnsureCollected();
            if (m_items.empty() || m_index >= m_items.size()) return nullptr;
            return m_items[m_index];
        }

        bool Focus(UI* target) {
            EnsureCollected();
            const auto it = std::find(m_items.begin(), m_items.end(), target);
            if (it == m_items.end()) return false;
            return SetIndex(static_cast<size_t>(std::distance(m_items.begin(), it)));
        }

        void FocusFirst() {
            EnsureCollected();
            if (!m_items.empty()) SetIndex(0);
        }

        void FocusNext() {
            EnsureCollected();
            if (m_items.empty()) return;
            const size_t cur = (m_index >= m_items.size()) ? static_cast<size_t>(-1) : m_index;
            SetIndex(cur + 1 >= m_items.size() ? 0 : cur + 1);
        }

        void FocusPrev() {
            EnsureCollected();
            if (m_items.empty()) return;
            const size_t cur = (m_index >= m_items.size()) ? 0 : m_index;
            SetIndex(cur == 0 ? m_items.size() - 1 : cur - 1);
        }

        /**
         * @brief 分发按键。
         *        Tab(9) 切换焦点；其它按键先给当前焦点元件。
         * @return 是否被消费
         */
        bool Dispatch(const xChar& key) {
            EnsureCollected();
            if (key == 9) { // Tab
                FocusNext();
                return true;
            }
            if (UI* cur = Current()) {
                if (cur->OnKey(key)) return true;
            }
            return false;
        }

        void EnsureCollected() {
            if (!m_dirty) return;
            m_items.clear();
            if (m_root) Collect(m_root);
            m_dirty = false;
            if (m_items.empty()) {
                m_index = static_cast<size_t>(-1);
            } else if (m_index >= m_items.size()) {
                SetIndex(0);
            } else if (m_items[m_index]->focused) {
                // 保持
            } else {
                SetIndex(m_index);
            }
        }

    private:
        FocusManager() = default;

        void Collect(UI* ui) {
            if (!ui) return;
            if (ui->IsFocusable()) m_items.push_back(ui);
            for (auto& c : ui->children) {
                if (c) Collect(c.get());
            }
        }

        bool SetIndex(const size_t idx) {
            if (idx >= m_items.size()) return false;
            if (m_index < m_items.size() && m_items[m_index]) {
                m_items[m_index]->focused = false;
                m_items[m_index]->OnBlur();
                m_items[m_index]->Invalidate();
            }
            m_index = idx;
            UI* cur = m_items[m_index];
            cur->focused = true;
            cur->OnFocus();
            cur->Invalidate();
            return true;
        }

        UI* m_root{ nullptr };
        std::vector<UI*> m_items{};
        size_t m_index{ static_cast<size_t>(-1) };
        bool m_dirty{ true };
    };

    // 便捷：外部直接喂一个按键
    inline bool DispatchKey(const xChar& key) { return FocusManager::Instance().Dispatch(key); }
}
