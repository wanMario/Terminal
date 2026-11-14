//
// Created by Administrator on 2026/5/25.
// 修订：UI 基类扩展为「布局 + 样式 + 子元素 + 焦点」的统一节点，
//        保留原有生命周期与 pos/signature 接口。
//

#pragma once

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "../../Math/Geometry/G2D/Point.hpp"
#include "../../Math/Geometry/G2D/Rect.hpp"
#include "TerminalBuffer.hpp"
#include "../Utils/Color.hpp"
#include "../Utils/Theme.hpp"

namespace Terminal {
    using namespace WMath::GG2D;

    class UI;
    class ConsoleBackBuffer;
    class xChar;
    class UIComponentManager;
    struct xMouse;
    struct Message;

    using Position = Point2Di;
    using Bounds = Rect2Di;

    // 文本/内容在自身格子里的对齐（enum class 以避免与 UI::Left() 等访问器重名）
    enum class LayoutPos {
        Left = 0, Right, Top, Bottom,
        LeftTop, RightTop, LeftBottom, RightBottom,
        Center
    };

    // 容器内子元素的排列方向
    enum class Flow { Row, Column };

    // 鼠标指针语义：由控件声明，前端尽量映射成系统光标；
    // 终端不支持自定义鼠标指针时，至少可以作为命中/交互提示使用。
    enum class MouseCursor {
        Arrow = 0,
        IBeam,       // 文本输入
        Hand,        // 按钮 / 开关 / 选项
        SizeWE,      // 横向缩放 / 横向滑条
        SizeNS,      // 纵向缩放 / 纵向滑条
        SizeAll,     // 窗口拖动 / 角点缩放
        Crosshair
    };

    // 子元素在分配格内的对齐
    enum class Align { Start, Center, End, Stretch };

    //======================================================================
    // LayoutSpec：一份盒模型描述（margin / padding / 尺寸 / 弹性 / 对齐）
    //======================================================================
    struct LayoutSpec {
        // 外边距
        int marginLeft{ 0 }, marginTop{ 0 }, marginRight{ 0 }, marginBottom{ 0 };
        // 内边距（容器用）
        int paddingLeft{ 0 }, paddingTop{ 0 }, paddingRight{ 0 }, paddingBottom{ 0 };

        int width{ -1 };   // <0 表示自动（跟随父级分配）
        int height{ -1 };  // <0 表示自动
        int minWidth{ 0 };
        int minHeight{ 0 };
        int maxWidth{ -1 };
        int maxHeight{ -1 };

        int grow{ 0 };                      // 弹性权重
        Align align{ Align::Start };        // 在可用格内的对齐
        Flow flow{ Flow::Column };          // 容器排列方向
        int gap{ 0 };                       // 子元素间距

        //---------- 链式配置 ----------
        LayoutSpec& Margin(const int all) { marginLeft = marginTop = marginRight = marginBottom = all; return *this; }
        LayoutSpec& Margin(const int h, const int v) {
            marginLeft = marginRight = h; marginTop = marginBottom = v; return *this;
        }
        LayoutSpec& Margin(const int l, const int t, const int r, const int b) {
            marginLeft = l; marginTop = t; marginRight = r; marginBottom = b; return *this;
        }
        LayoutSpec& Padding(const int all) { paddingLeft = paddingTop = paddingRight = paddingBottom = all; return *this; }
        LayoutSpec& Padding(const int h, const int v) {
            paddingLeft = paddingRight = h; paddingTop = paddingBottom = v; return *this;
        }
        LayoutSpec& Padding(const int l, const int t, const int r, const int b) {
            paddingLeft = l; paddingTop = t; paddingRight = r; paddingBottom = b; return *this;
        }
        LayoutSpec& Width(const int w) { width = w; return *this; }
        LayoutSpec& Height(const int h) { height = h; return *this; }
        LayoutSpec& Size(const int w, const int h) { width = w; height = h; return *this; }
        LayoutSpec& Grow(const int g) { grow = g; return *this; }
        LayoutSpec& AlignSelf(const Align a) { align = a; return *this; }
        LayoutSpec& FlowDir(const Flow f) { flow = f; return *this; }
        LayoutSpec& Gap(const int g) { gap = g; return *this; }

        [[nodiscard]] int HorizontalMargin() const { return marginLeft + marginRight; }
        [[nodiscard]] int VerticalMargin() const { return marginTop + marginBottom; }
        [[nodiscard]] int HorizontalPadding() const { return paddingLeft + paddingRight; }
        [[nodiscard]] int VerticalPadding() const { return paddingTop + paddingBottom; }
    };

    //======================================================================
    // UI：所有元件的基类
    //======================================================================
    class UI {
    public:
        bool isEnabled{ true };
        bool isVisible{ true };

        std::string signature{ "UI" };

        Position pos{ 0, 0 };     // 兼容旧接口：等于 bounds 左上角
        Bounds bounds{ 0, 0, 0, 0 };

        Style style{};
        LayoutSpec layout{};

        bool focusable{ false };
        bool focused{ false };

        // 指针样式（控件级默认值；容器命中后由最深层控件决定）
        MouseCursor mouseCursor{ MouseCursor::Arrow };
        // 脏标记：子树需要重绘 / 需要重新布局
        bool dirty{ true };
        bool layoutDirty{ true };

        UI* parent{ nullptr };
        std::vector<std::shared_ptr<UI>> children{};

        //---------- 脏标记 ----------
        // 标记「自身及子树需要重绘」，并沿 parent 链向上冒泡
        void Invalidate() {
            for (UI* n = this; n != nullptr && !n->dirty; n = n->parent) n->dirty = true;
        }

        // 标记「需要重排」，同样向上冒泡（父级需要知道子元素尺寸变了）
        void InvalidateLayout() {
            for (UI* n = this; n != nullptr && !n->layoutDirty; n = n->parent) n->layoutDirty = true;
            Invalidate();
        }

        [[nodiscard]] bool IsDirty() const noexcept { return dirty; }

        void ClearDirtyRecursive() {
            dirty = false;
            for (auto& c : children) if (c) c->ClearDirtyRecursive();
        }

        //---------- 生命周期 ----------
        virtual ~UI() = default;
        virtual void OnLoad() {}                 // 挂载时初始化
        virtual void OnMount() {}                // 被加入父节点时
        virtual void OnBoundsChanged() {}        // 布局尺寸变化
        virtual void OnRender(ConsoleBackBuffer&) {}   // 绘制
        virtual void OnRenderOverlay(ConsoleBackBuffer&) {} // 子元素之后绘制（滚动条/浮层）
        virtual void OnUpdate() {                // 每帧逻辑
            if (isEnabled) OnEnabledUpdate();
            else OnDisabledUpdate();
        }
        virtual void OnEnabledUpdate() {}
        virtual void OnDisabledUpdate() {}
        virtual void OnDelete() {}               // 卸载时释放资源

        virtual void OnFocus() {}
        virtual void OnBlur() {}
        virtual void OnMouseEnter() {}
        virtual void OnMouseLeave() {}

        // 消息队列投递：交互回调（OnClick 等）只负责 Post，真正的改动在主循环
        // MessageQueue::ProcessAll() 里由「消息携带的对象」自己 OnMessage 处理。
        virtual bool OnMessage(const Message&) { return false; }

        // 鼠标事件（悬停 / 按下 / 抬起 / 拖拽 / 滚轮）：返回 true 表示消费。
        // 定义在 TerminalMouse.hpp 里（这里只做前置声明，避免头文件互相包含）。
        virtual bool OnMouse(const xMouse& /*ev*/) { return false; }

        // 返回 true 表示消费了该按键
        virtual bool OnKey(const xChar& /*key*/) { return false; }

        //---------- 度量（自动尺寸用） ----------
        [[nodiscard]] virtual int MeasureWidth() const {
            return layout.width >= 0 ? layout.width : (layout.minWidth > 0 ? layout.minWidth : 0);
        }
        [[nodiscard]] virtual int MeasureHeight() const {
            return layout.height >= 0 ? layout.height : (layout.minHeight > 0 ? layout.minHeight : 0);
        }

        [[nodiscard]] int PreferredWidth() const { return std::max(MeasureWidth(), layout.minWidth); }
        [[nodiscard]] int PreferredHeight() const { return std::max(MeasureHeight(), layout.minHeight); }

        //---------- 位置 / 尺寸 ----------
        [[nodiscard]] Position GetPosition() const { return pos; }
        void SetPosition(const Position& p) {
            pos = p;
            bounds.M_begin = p;
        }

        [[nodiscard]] const Bounds& GetBounds() const { return bounds; }
        void SetBounds(const Bounds& b) {
            if (bounds.begin().x == b.begin().x && bounds.begin().y == b.begin().y
                && bounds.width() == b.width() && bounds.height() == b.height())
                return;
            bounds = b;
            pos = Position{ b.Left(), b.Top() };
            layoutDirty = true;      // 尺寸变了，子元素需要重排
            dirty = true;
            OnBoundsChanged();
        }

        [[nodiscard]] int Width() const { return bounds.width(); }
        [[nodiscard]] int Height() const { return bounds.height(); }
        [[nodiscard]] int Left() const { return bounds.Left(); }
        [[nodiscard]] int Top() const { return bounds.Top(); }
        [[nodiscard]] int Right() const { return bounds.Right(); }
        [[nodiscard]] int Bottom() const { return bounds.Bottom(); }

        //---------- 样式 ----------
        [[nodiscard]] const Style& GetStyle() const { return style; }
        void SetStyle(const Style& s) { if (style != s) style = s; Invalidate(); }
        virtual void SetForeground(const Color& c) { style.fg = c; Invalidate(); }
        virtual void SetBackground(const Color& c) { style.bg = c; style.opaque = true; Invalidate(); }
        [[nodiscard]] bool HasColor() const { return style.fg.IsIndexed() || !style.fg.Equals(Colors::Gray); }

        //---------- 背景继承 ----------
        // 元件绘制文字/装饰时，底色不该自己拍脑袋取主题色，而应「坐在谁身上就用谁的底色」：
        // 自身设了不透明背景 -> 用自身；否则沿 parent 链向上找；都没有才回落主题背景。
        // 这样卡片里放按钮/滑杆/文字不会出现「挖洞」的异色块。
        [[nodiscard]] Color EffectiveBackground() const {
            if (style.opaque) return style.bg;
            if (parent != nullptr) return parent->EffectiveBackground();
            return Theme::Current().background;
        }

        //---------- 使能 / 可见 ----------
        [[nodiscard]] bool GetEnabled() const { return isEnabled; }
        virtual void SetEnabled(const bool b) { if (isEnabled != b) { isEnabled = b; Invalidate(); } }
        [[nodiscard]] bool GetVisible() const { return isVisible; }
        virtual void SetVisible(const bool b) { if (isVisible != b) { isVisible = b; Invalidate(); } }

        //---------- 焦点 ----------
        [[nodiscard]] virtual bool IsFocusable() const { return focusable && isEnabled && isVisible; }
        [[nodiscard]] bool IsFocused() const { return focused; }
        void SetFocusable(const bool b) { focusable = b; }

        //---------- 子元素 ----------
        UI& AddChild(std::shared_ptr<UI> child) {
            if (!child) return *this;
            child->parent = this;
            children.push_back(std::move(child));
            children.back()->OnMount();
            InvalidateLayout();
            return *this;
        }

        UI& AddChild(UI* child) { return AddChild(std::shared_ptr<UI>(child)); }

        template <typename T, typename... Args>
        T* EmplaceChild(Args&&... args) {
            auto ptr = std::make_shared<T>(std::forward<Args>(args)...);
            T* raw = ptr.get();
            AddChild(std::move(ptr));
            return raw;
        }

        void RemoveChild(const UI* child) {
            children.erase(std::remove_if(children.begin(), children.end(),
                                          [child](const std::shared_ptr<UI>& p) { return p.get() == child; }),
                           children.end());
        }

        void ClearChildren() { children.clear(); InvalidateLayout(); }

        [[nodiscard]] size_t ChildCount() const { return children.size(); }

        //---------- 布局 ----------
        // 在父级给的可用格内计算自身矩形，然后排布子元素
        virtual void Arrange(const Bounds& available) {
            int x = available.Left() + layout.marginLeft;
            int y = available.Top() + layout.marginTop;
            int maxW = std::max(available.width() - layout.HorizontalMargin(), 0);
            int maxH = std::max(available.height() - layout.VerticalMargin(), 0);

            int w = layout.width >= 0 ? layout.width : maxW;
            int h = layout.height >= 0 ? layout.height : maxH;

            if (layout.minWidth > 0) w = std::max(w, layout.minWidth);
            if (layout.minHeight > 0) h = std::max(h, layout.minHeight);
            if (layout.maxWidth >= 0) w = std::min(w, layout.maxWidth);
            if (layout.maxHeight >= 0) h = std::min(h, layout.maxHeight);
            w = std::min(w, maxW);
            h = std::min(h, maxH);

            switch (layout.align) {
            case Align::Center: x += (maxW - w) / 2; break;
            case Align::End:    x += (maxW - w); break;
            case Align::Stretch: w = maxW; break;
            default: break;
            }
            h = layout.height >= 0 ? h : maxH;

            Bounds target{ x, y, std::max(w, 0), std::max(h, 0) };
            // 尺寸/位置没变且不要求重排 → 直接复用上一轮结果，跳过整棵子树的布局计算
            const bool same = bounds.begin().x == target.begin().x && bounds.begin().y == target.begin().y &&
                              bounds.width() == target.width() && bounds.height() == target.height();
            if (same && !layoutDirty) return;

            SetBounds(target);
            layoutDirty = false;
            OnLayout();
        }

        virtual void OnLayout() {}

        //---------- 渲染 ----------
        // dirty 为 false 时整棵子树跳过；为 true 时整棵子树强制重绘（避免父级背景擦掉子元素）
        void Render(ConsoleBackBuffer& buf) {
            if (!isVisible || bounds.IsEmpty() || !dirty) return;
            buf.PushClip(bounds);
            if (style.opaque) buf.FillStyle(bounds, style);
            OnRender(buf);
            for (auto& c : children) {
                if (c) c->RenderSubtree(buf);
            }
            OnRenderOverlay(buf);
            buf.PopClip();
            dirty = false;
        }

    private:
        // 父级重绘时，子树必须一并重绘（即使自身 dirty 为 false）
        void RenderSubtree(ConsoleBackBuffer& buf) {
            if (!isVisible || bounds.IsEmpty()) return;
            buf.PushClip(bounds);
            if (style.opaque) buf.FillStyle(bounds, style);
            OnRender(buf);
            for (auto& c : children) {
                if (c) c->RenderSubtree(buf);
            }
            OnRenderOverlay(buf);
            buf.PopClip();
            dirty = false;
        }

    public:

        //---------- 更新 ----------
        void Update() {
            OnUpdate();
            for (auto& c : children) {
                if (c) c->Update();
            }
        }

        void Mount() {
            OnLoad();
            for (auto& c : children) {
                if (c) c->Mount();
            }
        }

        //---------- 命中测试 ----------
        // 容器可覆盖：例如 OverlayHost 自身占满画布，但只让子窗口接收鼠标。
        [[nodiscard]] virtual bool HitTest(const int x, const int y) const {
            return bounds.Contains(Position{ x, y });
        }

        // 子元素之前的最高优先级热区：滚动条等需要压住超出视口的子内容。
        [[nodiscard]] virtual bool HitTestOverlay(const int /*x*/, const int /*y*/) const { return false; }

        // 鼠标悬停时的指针语义；具体坐标用于命中边框/滑条等局部区域。
        // 返回值只在指针形状变化时由前端应用一次，不参与每帧渲染。
        [[nodiscard]] virtual MouseCursor CursorAt(const int /*x*/, const int /*y*/) const {
            return mouseCursor;
        }

        void SetMouseCursor(const MouseCursor c) { mouseCursor = c; }

        // 内容区（去掉 padding）；容器可覆盖（例如边框再让出 1 格）
        [[nodiscard]] virtual Bounds ContentBounds() const {
            return Bounds{ bounds.Left() + layout.paddingLeft,
                           bounds.Top() + layout.paddingTop,
                           std::max(bounds.width() - layout.HorizontalPadding(), 0),
                           std::max(bounds.height() - layout.VerticalPadding(), 0) };
        }
    };

    //======================================================================
    // UIDynamic 保留（交互态与非交互态的双通道回调）
    //======================================================================
    template <void (*OnceInteractive)() = [] {}, void (*OnceNonInteractive)() = [] {}>
    class UIDynamic : public UI {
    public:
        bool isInteractive{ false };

        UIDynamic() { signature = "UIDynamic"; }   // 不在类作用域重复声明，避免遮蔽基类成员

        [[nodiscard]] bool IsInteractive() const { return isInteractive; }

        void SetInteractive(const bool interactive) {
            isInteractive = interactive;
            if (interactive) OnceInteractive();
            else OnceNonInteractive();
        }

        void OnEnabledUpdate() override {
            if (isInteractive) OnInteractiveUpdate();
            else OnNormalUpdate();
        }

        virtual void OnInteractiveUpdate() {}
        virtual void OnNormalUpdate() {}
    };
}
