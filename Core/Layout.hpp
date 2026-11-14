//
// Terminal / Core / Layout.hpp
// 盒模型布局求解：Row / Column 弹性排布 + 交叉轴对齐 + 间距
//

#pragma once

#include <algorithm>
#include <vector>

#include "UI.hpp"

namespace Terminal {
    namespace Layout {

        [[nodiscard]] inline bool IsRow(const UI& ui) { return ui.layout.flow == Flow::Row; }

        [[nodiscard]] inline Bounds Content(const UI& ui) { return ui.ContentBounds(); }

        [[nodiscard]] inline int MainExtent(const Bounds& box, const Flow flow) {
            return flow == Flow::Row ? box.width() : box.height();
        }

        [[nodiscard]] inline int CrossExtent(const Bounds& box, const Flow flow) {
            return flow == Flow::Row ? box.height() : box.width();
        }

        /**
         * @brief 按父节点的 flow 排列其子元素。
         *        主轴：固定尺寸之和 + 弹性分配剩余空间；
         *        交叉轴：Stretch 拉满，其余按子元素自身 align 对齐。
         */
        inline void ArrangeChildren(UI& parent) {
            Bounds content = Content(parent);
            if (content.IsEmpty() || parent.children.empty()) return;

            const Flow flow = parent.layout.flow;
            const int gap = std::max(parent.layout.gap, 0);

            std::vector<UI*> list;
            list.reserve(parent.children.size());
            for (auto& c : parent.children) {
                if (c && c->isVisible) list.push_back(c.get());
            }
            if (list.empty()) return;

            const int n = static_cast<int>(list.size());
            const int mainTotal = MainExtent(content, flow);
            const int crossTotal = CrossExtent(content, flow);
            const int totalGap = gap * (n - 1);

            // 1. 统计固定尺寸与弹性权重
            int fixed = 0, weight = 0;
            std::vector<int> pref(static_cast<size_t>(n), 0);
            for (int i = 0; i < n; ++i) {
                UI* c = list[static_cast<size_t>(i)];
                const int p = flow == Flow::Row ? c->PreferredWidth() : c->PreferredHeight();
                pref[static_cast<size_t>(i)] = std::max(p, 0);
                fixed += pref[static_cast<size_t>(i)];
                weight += std::max(c->layout.grow, 0);
            }

            int space = mainTotal - totalGap;

            // 2. 压缩：空间不够时按比例收缩（保证可读性，最小 1）
            std::vector<int> size = pref;
            if (fixed > space && fixed > 0) {
                for (int i = 0; i < n; ++i) {
                    size[static_cast<size_t>(i)] = std::max(1, space * pref[static_cast<size_t>(i)] / fixed);
                }
                fixed = 0;
                for (int i = 0; i < n; ++i) fixed += size[static_cast<size_t>(i)];
            }

            // 3. 弹性扩展
            const int extra = std::max(space - fixed, 0);
            if (extra > 0 && weight > 0) {
                int rest = extra;
                int restWeight = weight;
                for (int i = 0; i < n; ++i) {
                    UI* c = list[static_cast<size_t>(i)];
                    const int g = std::max(c->layout.grow, 0);
                    if (g == 0) continue;
                    const int add = (restWeight == g) ? rest : extra * g / weight;
                    size[static_cast<size_t>(i)] += add;
                    rest -= add;
                    restWeight -= g;
                    if (rest <= 0) break;
                }
                fixed = space;
            }

            // 4. 主轴剩余空间的对齐（无弹性元素时）
            int used = totalGap;
            for (int i = 0; i < n; ++i) used += size[static_cast<size_t>(i)];
            int cursor = 0;
            if (weight == 0 && mainTotal > used) {
                const int slack = mainTotal - used;
                switch (parent.layout.align) {
                case Align::Center: cursor = slack / 2; break;
                case Align::End:    cursor = slack; break;
                default:            cursor = 0; break;
                }
            }

            // 5. 逐个放置
            for (int i = 0; i < n; ++i) {
                UI* c = list[static_cast<size_t>(i)];
                const int mainSize = std::max(size[static_cast<size_t>(i)], 0);

                const int crossPref = flow == Flow::Row ? c->PreferredHeight() : c->PreferredWidth();
                // 交叉轴：子元素显式 Stretch，或父容器 align=Stretch 且子元素为默认 Start 时拉满
                bool stretch = c->layout.align == Align::Stretch || crossPref <= 0 ||
                               (parent.layout.align == Align::Stretch && c->layout.align == Align::Start);
                int crossSize = stretch ? crossTotal : std::min(crossPref, crossTotal);
                int crossOffset = 0;
                if (!stretch && crossTotal > crossSize) {
                    switch (c->layout.align) {
                    case Align::Center: crossOffset = (crossTotal - crossSize) / 2; break;
                    case Align::End:    crossOffset = crossTotal - crossSize; break;
                    default:            crossOffset = 0; break;
                    }
                }

                Bounds cell;
                if (flow == Flow::Row) {
                    cell = Bounds{ content.Left() + cursor, content.Top() + crossOffset, mainSize, crossSize };
                } else {
                    cell = Bounds{ content.Left() + crossOffset, content.Top() + cursor, crossSize, mainSize };
                }

                c->Arrange(cell);
                cursor += mainSize + gap;
            }
        }

        // 子元素主轴/交叉轴合计尺寸（容器自动尺寸用）
        inline void MeasureChildren(const UI& ui, int& w, int& h) {
            w = 0;
            h = 0;
            const Flow flow = ui.layout.flow;
            const int gap = std::max(ui.layout.gap, 0);
            int count = 0;
            int main = 0, cross = 0;
            for (const auto& c : ui.children) {
                if (!c || !c->isVisible) continue;
                const int cw = c->PreferredWidth() + c->layout.HorizontalMargin();
                const int ch = c->PreferredHeight() + c->layout.VerticalMargin();
                if (flow == Flow::Row) {
                    main += cw;
                    cross = std::max(cross, ch);
                } else {
                    main += ch;
                    cross = std::max(cross, cw);
                }
                ++count;
            }
            if (count > 1) main += gap * (count - 1);
            if (flow == Flow::Row) { w = main; h = cross; }
            else { w = cross; h = main; }
        }

    } // namespace Layout
}
