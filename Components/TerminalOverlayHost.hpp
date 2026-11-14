//
// Terminal / Components / TerminalOverlayHost.hpp
// 悬浮层：自身不占布局高度，只把子窗口铺到整块画布上。
// 命中测试只覆盖可见子窗口，因此窗口之外的鼠标仍会落到下层的普通控件。
//

#pragma once

#include <algorithm>
#include "../Core/UI.hpp"
#include "../Core/Layout.hpp"
#include "../Core/TerminalBuffer.hpp"
#include "TerminalPanel.hpp"

namespace Terminal {

    class OverlayHost : public Panel {
    public:
        OverlayHost() {
            signature = "UIOverlay";
            layout.width = 0;
            layout.height = 0;
            layout.paddingLeft = 0;
            layout.paddingTop = 0;
            layout.paddingRight = 0;
            layout.paddingBottom = 0;
            style.opaque = false;
            fillBackground = false;
        }

        [[nodiscard]] int MeasureWidth() const override { return 0; }
        [[nodiscard]] int MeasureHeight() const override { return 0; }

        // 画布尺寸交给子窗口，自身 bounds 保持整块画布：
        // Window 缩放时用 parent->GetBounds() 做边界夹取，不能把父级缩成窗口本身。
        void Arrange(const Bounds& available) override {
            const Bounds canvas = parent != nullptr ? parent->GetBounds() : available;
            SetBounds(canvas);
            for (auto& c : children) {
                if (c && c->isVisible) c->Arrange(canvas);
            }
            layoutDirty = false;
        }

        void OnUpdate() override {
            // 子元素由 UI::Update 统一驱动，这里只同步“是否还有可见窗口”。
            const bool anyVisible = std::any_of(children.begin(), children.end(),
                [](const std::shared_ptr<UI>& c) { return c && c->isVisible; });
            if (isVisible != anyVisible) isVisible = anyVisible;
        }

        // 覆盖层本身不拦截鼠标：只有落在某个可见子容器里才算命中。
        [[nodiscard]] bool HitTest(const int x, const int y) const override {
            if (!isVisible) return false;
            for (auto it = children.rbegin(); it != children.rend(); ++it) {
                if (*it && (*it)->isVisible && (*it)->HitTest(x, y)) return true;
            }
            return false;
        }

        void OnRender(ConsoleBackBuffer& buf) override {
            (void)buf;   // 不画自己的背景，只让子窗口覆盖
        }
    };

} // namespace Terminal
