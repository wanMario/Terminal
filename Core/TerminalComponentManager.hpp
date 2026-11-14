//
// Created by Administrator on 2026/6/27.
// 修订：修正 Json 初始化与缺失头文件，补上「类型注册表 + Json 构建 UI 树」。
//        Json 只是次要入口，代码里也可以直接用组件类搭界面。
//

#pragma once

#include <functional>
#include <memory>
#include <stack>
#include <string>
#include <unordered_map>
#include <vector>

#include "UI.hpp"
#include "Layout.hpp"
#include "TerminalBuffer.hpp"
#include "TerminalJson.hpp"
#include "../Utils/Outputs.hpp"

#include "../Components/TerminalText.hpp"
#include "../Components/TerminalButton.hpp"
#include "../Components/TerminalTextInput.hpp"
#include "../Components/TerminalSlider.hpp"
#include "../Components/TerminalScrollView.hpp"
#include "../Components/TerminalImage.hpp"
#include "../Components/TerminalBorder.hpp"
#include "../Components/TerminalPanel.hpp"
#include "../Components/TerminalDivider.hpp"
#include "../Components/TerminalBadge.hpp"
#include "../Components/TerminalSwitch.hpp"
#include "../Components/TerminalChoiceGroup.hpp"
#include "../Components/TerminalWindow.hpp"
#include "../Components/TerminalPageHolder.hpp"

namespace Terminal {

    //----------------------------------------------------------------------
    // Json 取值小工具（JsonCore 的类型不匹配是抛错，所以先判类型）
    //----------------------------------------------------------------------
    namespace JsUtil {
        inline bool Has(const Json& js, const std::string& key) {
            return js.IsObject() && js.contains_key(key);
        }

        inline std::string Str(const Json& js, const std::string& key, const std::string& def = {}) {
            if (!Has(js, key)) return def;
            const Json v = js.get(key);
            if (v.IsString()) return v.toStr();
            if (v.IsNumber()) return v.toString();
            return def;
        }

        // 多个候选 key 里取第一个存在的
        inline std::string StrAny(const Json& js, const std::initializer_list<std::string>& keys,
                                  const std::string& def = {}) {
            for (const auto& k : keys) {
                if (Has(js, k)) return Str(js, k, def);
            }
            return def;
        }

        inline double Num(const Json& js, const std::string& key, const double def = 0.0) {
            if (!Has(js, key)) return def;
            const Json v = js.get(key);
            if (v.IsNumber()) return v.toNumber();
            if (v.IsString()) {
                try { return std::stod(v.toStr()); } catch (...) { return def; }
            }
            return def;
        }

        inline int Int(const Json& js, const std::string& key, const int def = 0) {
            return static_cast<int>(Num(js, key, static_cast<double>(def)));
        }

        inline bool Bool(const Json& js, const std::string& key, const bool def = false) {
            if (!Has(js, key)) return def;
            const Json v = js.get(key);
            if (v.IsBool()) return v.toBoolean();
            if (v.IsNumber()) return v.toNumber() != 0.0;
            if (v.IsString()) {
                const std::string s = v.toStr();
                return s == "true" || s == "1" || s == "yes" || s == "on";
            }
            return def;
        }

        inline Color ColorOf(const Json& js, const std::string& key, const Color& def) {
            if (!Has(js, key)) return def;
            const Json v = js.get(key);
            if (v.IsString()) return Terminal::Color::Parse(v.toStr(), def);
            if (v.IsNumber()) return Terminal::Color::OfIndex(static_cast<int>(v.toNumber()));
            return def;
        }

        inline Align AlignOf(const std::string& s) {
            if (s == "center") return Align::Center;
            if (s == "end" || s == "right" || s == "bottom") return Align::End;
            if (s == "stretch") return Align::Stretch;
            return Align::Start;
        }

        inline Flow FlowOf(const std::string& s) {
            if (s == "row" || s == "horizontal") return Flow::Row;
            return Flow::Column;
        }

        inline LayoutPos LayoutPosOf(const std::string& s) {
            if (s == "right") return LayoutPos::Right;
            if (s == "center") return LayoutPos::Center;
            if (s == "top") return LayoutPos::Top;
            if (s == "bottom") return LayoutPos::Bottom;
            if (s == "lefttop") return LayoutPos::LeftTop;
            if (s == "righttop") return LayoutPos::RightTop;
            if (s == "leftbottom") return LayoutPos::LeftBottom;
            if (s == "rightbottom") return LayoutPos::RightBottom;
            return LayoutPos::Left;
        }
    }

    //----------------------------------------------------------------------
    // 组件管理器：类型注册表 + Json -> UI 树 + 统一 Arrange/Render/Update
    //----------------------------------------------------------------------
    class UIComponentManager : public UI {
    public:
        using Factory = std::function<std::shared_ptr<UI>(const Json&)>;
        using RegistryMap = std::unordered_map<std::string, Factory>;

        static RegistryMap& Registry() {
            static RegistryMap map;
            return map;
        }

        static bool RegisterType(const std::string& type, Factory factory) {
            if (type.empty() || !factory) return false;
            Registry()[type] = std::move(factory);
            return true;
        }

        [[nodiscard]] static bool HasType(const std::string& type) {
            return Registry().count(type) > 0;
        }

        [[nodiscard]] static std::vector<std::string> Types() {
            std::vector<std::string> out;
            out.reserve(Registry().size());
            for (const auto& kv : Registry()) out.push_back(kv.first);
            return out;
        }

        //--------------------------------------------------------------
        // 旧接口保留：一个 slot 池，按 id 复用位置
        //--------------------------------------------------------------
        size_t GetId() {
            const size_t id = rebin.top();
            rebin.pop();
            if (rebin.empty()) rebin.emplace(remem++);
            return id;
        }

        void DelId(const size_t& x) {
            if (remem != x + 1) rebin.emplace(x);
            else remem--;
        }

        void DeleteElements(const std::initializer_list<size_t>& xs) {
            for (const auto& i : xs) {
                if (ptr.size() <= i) continue;
                DelId(i);
                ptr[i] = nullptr;
            }
        }

        Json elements = Json::parse("{\"elements\":[]}");
        std::vector<std::unique_ptr<UI>> ptr{};
        std::stack<size_t> rebin{};
        size_t remem{ 0 };

        UIComponentManager() {
            signature = "UIComponentManager";
            layout.flow = Flow::Column;
            layout.align = Align::Stretch;   // 根容器：子元素默认撑满交叉轴
            style = Style(Theme::Current().text, Theme::Current().background);
            rebin.push(0ull);
            remem++;
            RegisterBuiltinTypes();
        }

        // 管理器本身是容器：布局 / 渲染都转发给 children
        void OnLayout() override { Layout::ArrangeChildren(*this); }

        void OnRender(ConsoleBackBuffer& buf) override {
            if (style.opaque) buf.FillStyle(GetBounds(), style);
        }

        size_t AddElements(std::unique_ptr<UI> ele = std::make_unique<UI>(),
                           std::string sig = "auto") {
            if (!ele) return static_cast<size_t>(-1);
            if (sig == "auto") sig = ele->signature;
            const size_t id = GetId();
            if (id >= ptr.size()) ptr.resize(id + 1);
            ptr[id] = std::move(ele);
            return id;
        }

        //--------------------------------------------------------------
        // 校验：允许 [] / [ {..}, {...} ] / {"elements":[...]}
        //--------------------------------------------------------------
        static bool IsValidElement(const Json& js) {
            auto validObject = [](const Json& obj) {
                if (!obj.IsObject()) return false;
                if (!TopHasType(obj)) return false;
                const std::string type = TopType(obj);
                if (type == "auto") return false;
                const Json p = obj.get("ptr");
                if (p.IsString() && p.toStr() == "none") return false;
                return true;
            };

            if (js.IsArray()) {
                for (const auto& sub : js.GetArray()) {
                    if (sub.IsArray()) {
                        for (const auto& item : sub.GetArray()) {
                            if (item.IsArray()) return false;
                            if (!validObject(item)) return false;
                        }
                    } else if (!validObject(sub)) {
                        return false;
                    }
                }
                return true;
            }
            if (js.IsObject() && JsUtil::Has(js, "elements")) {
                return IsValidElement(js.get("elements"));
            }
            return false;
        }

        // 兼容旧名
        static bool isValidElement(const Json& js) { return IsValidElement(js); }

        //--------------------------------------------------------------
        // 构建
        //--------------------------------------------------------------
        void Clear() {
            children.clear();
            ptr.clear();
            rebin = std::stack<size_t>{};
            rebin.push(0ull);
            remem = 1;
            elements = Json::parse("{\"elements\":[]}");
        }

        size_t LoadFromText(const std::string& text) {
            Json js = Json::parse(text);
            return LoadFromJSON(js);
        }

        bool LoadFromFile(const std::string& path) {
            Json js{};
            if (!js.ReadFromFile(path)) return false;
            return LoadFromJSON(js) > 0;
        }

        /**
         * @brief 从 Json 构建 UI 树，返回最高层元素数量。
         *        支持 [ ... ] 与 {"elements":[ ... ]} 两种顶层形式。
         */
        size_t LoadFromJSON(const Json& js) {
            Clear();
            const Json* list = &js;
            Json tmp;
            if (js.IsObject() && JsUtil::Has(js, "elements")) {
                tmp = js.get("elements");
                list = &tmp;
            }
            if (!list->IsArray()) return 0;

            size_t count = 0;
            for (const auto& item : list->GetArray()) {
                auto ui = Build(item);
                if (!ui) continue;
                AddChild(std::move(ui));
                ++count;
            }
            elements = Json::parse("{\"elements\":[]}");
            arrayWrite(elements, "elements", *list);
            return count;
        }

        std::shared_ptr<UI> CreateElement(const Json& js) { return Build(js); }

        size_t RootCount() const { return children.size(); }

        //--------------------------------------------------------------
        // 内置类型
        //--------------------------------------------------------------
        static void RegisterBuiltinTypes() {
            static bool done = false;
            if (done) return;
            done = true;

            // ---- text ----
            RegisterType("text", [](const Json& js) -> std::shared_ptr<UI> {
                auto t = std::make_shared<Text>();
                t->SetText(JsUtil::StrAny(js, { "text", "value", "content" }, ""));
                const Color fg = JsUtil::ColorOf(js, "color", Theme::Current().text);
                const Color bg = JsUtil::ColorOf(js, "bg", Theme::Current().background);
                t->SetStyle(Style(fg, bg).SetBold(JsUtil::Bool(js, "bold")));
                t->SetAlign(JsUtil::LayoutPosOf(JsUtil::Str(js, "align", "left")));
                ApplyCommon(*t, js);
                return t;
            });

            // ---- button ----
            RegisterType("button", [](const Json& js) -> std::shared_ptr<UI> {
                auto b = std::make_shared<Button>();
                b->SetLabel(JsUtil::StrAny(js, { "label", "text", "title" }, "Button"));
                b->SetShowFrame(JsUtil::Bool(js, "frame", true));
                b->SetFrame(Box::FromName(JsUtil::Str(js, "box", "round")));
                // 按钮背景色不开放配置入口（底色继承容器）；只允许指定文字色
                if (JsUtil::Has(js, "color")) b->SetTextColor(JsUtil::ColorOf(js, "color", Theme::Current().text));
                if (JsUtil::Has(js, "borderColor")) b->SetBorderColor(JsUtil::ColorOf(js, "borderColor", Theme::Current().danger));
                ApplyCommon(*b, js);
                return b;
            });

            // ---- input ----
            RegisterType("input", [](const Json& js) -> std::shared_ptr<UI> {
                auto in = std::make_shared<TextInput>();
                in->value = JsUtil::StrAny(js, { "value", "text" }, "");
                in->cursor = in->Cps().size();
                in->SetLabel(JsUtil::Str(js, "label", ""));
                in->SetPlaceholder(JsUtil::Str(js, "placeholder", in->placeholder));
                in->SetPrefix(JsUtil::Str(js, "prefix", in->prefix));
                in->SetPassword(JsUtil::Bool(js, "password"));
                in->SetMaxLength(JsUtil::Int(js, "maxLength", in->maxLength));
                in->SetMultiline(JsUtil::Bool(js, "multiline"));
                in->SetMaxLines(JsUtil::Int(js, "maxLines", in->maxLines));
                in->showFrame = JsUtil::Bool(js, "frame", true);
                in->SetFrame(Box::FromName(JsUtil::Str(js, "box", "round")));
                if (JsUtil::Has(js, "overwrite")) in->SetOverwrite(JsUtil::Bool(js, "overwrite"));
                ApplyCommon(*in, js);
                return in;
            });

            // ---- slider ----
            RegisterType("slider", [](const Json& js) -> std::shared_ptr<UI> {
                auto s = std::make_shared<Slider>();
                s->label = JsUtil::StrAny(js, { "label", "text" }, "");
                s->min = JsUtil::Num(js, "min", 0.0);
                s->max = JsUtil::Num(js, "max", 100.0);
                s->SetStep(JsUtil::Num(js, "step", 1.0));
                s->barWidth = JsUtil::Int(js, "barWidth", 20);
                s->showValue = JsUtil::Bool(js, "showValue", true);
                s->SetPrecision(JsUtil::Int(js, "precision", 0));
                s->SetValue(JsUtil::Num(js, "value", (s->min + s->max) / 2.0), false);
                ApplyCommon(*s, js);
                return s;
            });

            // ---- image（半格像素 PNG/JPG）----
            RegisterType("image", [](const Json& js) -> std::shared_ptr<UI> {
                auto img = std::make_shared<UIImage>();
                const std::string src = JsUtil::StrAny(js, { "src", "path" }, "");
                if (!src.empty()) img->SetImage(src);
                img->SetFit(JsUtil::Str(js, "fit", "contain"));
                if (JsUtil::Has(js, "placeholder")) img->SetPlaceholder(JsUtil::Str(js, "placeholder", ""));
                ApplyCommon(*img, js);
                return img;
            });

            // ---- border ----
            RegisterType("border", [](const Json& js) -> std::shared_ptr<UI> {
                auto b = std::make_shared<Border>();
                b->SetTitle(JsUtil::Str(js, "title", ""));
                b->SetChars(Box::FromName(JsUtil::Str(js, "box", "round")));
                b->SetFrameVisible(JsUtil::Bool(js, "frame", true));
                b->SetTitleAlign(JsUtil::LayoutPosOf(JsUtil::Str(js, "titleAlign", "left")));
                if (JsUtil::Has(js, "borderColor")) b->SetBorderColor(JsUtil::ColorOf(js, "borderColor", b->borderColor));
                if (JsUtil::Has(js, "titleColor")) b->SetTitleColor(JsUtil::ColorOf(js, "titleColor", b->titleColor));
                b->layout.flow = JsUtil::FlowOf(JsUtil::Str(js, "flow", "column"));
                b->layout.gap = JsUtil::Int(js, "gap", 0);
                ApplyCommon(*b, js);
                BuildChildren(*b, js);
                return b;
            });

            // ---- panel ----
            RegisterType("panel", [](const Json& js) -> std::shared_ptr<UI> {
                auto p = std::make_shared<Panel>();
                p->SetFlow(JsUtil::FlowOf(JsUtil::Str(js, "flow", "column")));
                p->SetGap(JsUtil::Int(js, "gap", 0));
                p->SetTitle(JsUtil::Str(js, "title", ""));
                if (!JsUtil::Has(js, "title")) p->showTitle = false;
                p->SetFill(JsUtil::Bool(js, "fill", true));
                p->SetBackgroundColor(JsUtil::ColorOf(js, "bg", Theme::Current().background));
                ApplyCommon(*p, js);
                BuildChildren(*p, js);
                return p;
            });

            // ---- scroll（竖向滚动容器 + 滚动条）----
            RegisterType("scroll", [](const Json& js) -> std::shared_ptr<UI> {
                auto s = std::make_shared<ScrollView>();
                s->layout.gap = JsUtil::Int(js, "gap", 0);
                s->SetWheelStep(JsUtil::Int(js, "wheelStep", 3));
                s->SetShowScrollbar(JsUtil::Bool(js, "scrollbar", true));
                if (JsUtil::Has(js, "bg")) s->SetBackgroundColor(JsUtil::ColorOf(js, "bg", Theme::Current().background));
                ApplyCommon(*s, js);
                BuildChildren(*s, js);
                return s;
            });

            // ---- divider（分隔线，可带标题）----
            RegisterType("divider", [](const Json& js) -> std::shared_ptr<UI> {
                auto d = std::make_shared<Divider>();
                d->SetTitle(JsUtil::Str(js, "title", ""));
                d->SetChars(Box::FromName(JsUtil::Str(js, "box", "single")));
                if (JsUtil::Has(js, "lineColor")) d->SetColors(JsUtil::ColorOf(js, "lineColor", d->lineColor),
                                                              JsUtil::ColorOf(js, "titleColor", d->titleColor));
                ApplyCommon(*d, js);
                return d;
            });

            // ---- badge（胶囊标签）----
            RegisterType("badge", [](const Json& js) -> std::shared_ptr<UI> {
                auto b = std::make_shared<Badge>();
                b->SetText(JsUtil::StrAny(js, { "text", "label", "content" }, ""));
                b->SetDot(JsUtil::Bool(js, "dot", true));
                if (JsUtil::Has(js, "bg") || JsUtil::Has(js, "fg"))
                    b->SetColors(JsUtil::ColorOf(js, "bg", Theme::Current().accent),
                                 JsUtil::ColorOf(js, "fg", Theme::Current().accentText));
                ApplyCommon(*b, js);
                return b;
            });

            // ---- switch ----
            RegisterType("switch", [](const Json& js) -> std::shared_ptr<UI> {
                auto s = std::make_shared<Switch>();
                s->SetLabel(JsUtil::StrAny(js, { "label", "text" }, "开关"));
                s->SetValue(JsUtil::Bool(js, "value", false));
                if (JsUtil::Has(js, "onText")) s->onText = JsUtil::Str(js, "onText", s->onText);
                if (JsUtil::Has(js, "offText")) s->offText = JsUtil::Str(js, "offText", s->offText);
                if (JsUtil::Has(js, "onColor")) s->onColor = JsUtil::ColorOf(js, "onColor", s->onColor);
                if (JsUtil::Has(js, "offColor")) s->offColor = JsUtil::ColorOf(js, "offColor", s->offColor);
                ApplyCommon(*s, js);
                return s;
            });

            // ---- choices（single / multi）----
            RegisterType("choices", [](const Json& js) -> std::shared_ptr<UI> {
                auto c = std::make_shared<ChoiceGroup>();
                c->SetMulti(JsUtil::Bool(js, "multi", false));
                if (JsUtil::Has(js, "options")) {
                    const Json opts = js.get("options");
                    if (opts.IsArray()) {
                        std::vector<std::string> list;
                        for (const auto& item : opts.GetArray())
                            if (item.IsString()) list.push_back(item.GetString());
                        c->SetOptions(list);
                    }
                }
                if (JsUtil::Has(js, "selected")) {
                    const Json sel = js.get("selected");
                    if (sel.IsArray()) {
                        for (const auto& item : sel.GetArray())
                            if (item.IsNumber()) c->SetSelected(static_cast<int>(item.GetNumber()), true);
                    } else if (sel.IsNumber()) {
                        c->SetSelected(static_cast<int>(sel.GetNumber()), true);
                    }
                }
                ApplyCommon(*c, js);
                return c;
            });
            RegisterType("singlechoice", [](const Json& js) -> std::shared_ptr<UI> {
                Json copy = js;
                copy["multi"] = Json(false);
                return Build(copy);
            });
            RegisterType("multichoice", [](const Json& js) -> std::shared_ptr<UI> {
                Json copy = js;
                copy["multi"] = Json(true);
                return Build(copy);
            });

            // ---- window ----
            RegisterType("window", [](const Json& js) -> std::shared_ptr<UI> {
                auto w = std::make_shared<Window>();
                w->SetTitle(JsUtil::Str(js, "title", "Window"));
                w->SetFrame(Box::FromName(JsUtil::Str(js, "box", "round")));
                w->SetClosable(JsUtil::Bool(js, "closable", true));
                w->SetDraggable(JsUtil::Bool(js, "draggable", true));
                if (JsUtil::Has(js, "width")) w->layout.width = JsUtil::Int(js, "width", w->layout.width);
                if (JsUtil::Has(js, "height")) w->layout.height = JsUtil::Int(js, "height", w->layout.height);
                if (JsUtil::Has(js, "x") || JsUtil::Has(js, "y"))
                    w->MoveTo(JsUtil::Int(js, "x", 0), JsUtil::Int(js, "y", 0));
                ApplyCommon(*w, js);
                BuildChildren(*w, js);
                return w;
            });

            // ---- page（可嵌套页面 + 局部队列）----
            RegisterType("page", [](const Json& js) -> std::shared_ptr<UI> {
                auto p = std::make_shared<PageHolder>();
                p->SetName(JsUtil::Str(js, "name", ""));
                p->SetFlow(JsUtil::FlowOf(JsUtil::Str(js, "flow", "column")));
                p->SetGap(JsUtil::Int(js, "gap", 0));
                p->SetFill(JsUtil::Bool(js, "fill", true));
                ApplyCommon(*p, js);
                BuildChildren(*p, js);
                return p;
            });

            // ---- spacer（占位）----
            RegisterType("spacer", [](const Json& js) -> std::shared_ptr<UI> {
                auto sp = std::make_shared<UI>();
                sp->signature = "UISpacer";
                sp->layout.width = JsUtil::Int(js, "width", 0);
                sp->layout.height = JsUtil::Int(js, "height", 0);
                sp->layout.grow = JsUtil::Int(js, "grow", 0);
                return sp;
            });
        }

    private:
        //--------------------------------------------------------------
        static std::string TopType(const Json& obj) {
            if (JsUtil::Has(obj, "UItype")) return JsUtil::Str(obj, "UItype");
            if (JsUtil::Has(obj, "type")) return JsUtil::Str(obj, "type");
            return {};
        }

        static bool TopHasType(const Json& obj) {
            return JsUtil::Has(obj, "UItype") || JsUtil::Has(obj, "type");
        }

        static void arrayWrite(Json& target, const std::string& key, const Json& value) {
            target[key] = value;
        }

        static void ApplyCommon(UI& ui, const Json& js) {
            if (JsUtil::Has(js, "width")) ui.layout.width = JsUtil::Int(js, "width", ui.layout.width);
            if (JsUtil::Has(js, "height")) ui.layout.height = JsUtil::Int(js, "height", ui.layout.height);
            if (JsUtil::Has(js, "grow")) ui.layout.grow = JsUtil::Int(js, "grow", 0);
            if (JsUtil::Has(js, "margin")) {
                const Json m = js.get("margin");
                if (m.IsNumber()) ui.layout.Margin(static_cast<int>(m.toNumber()));
                else if (m.IsArray()) {
                    const auto& arr = m.GetArray();
                    int v[4]{ 0, 0, 0, 0 };
                    for (size_t i = 0; i < arr.size() && i < 4; ++i)
                        if (arr[i].IsNumber()) v[i] = static_cast<int>(arr[i].toNumber());
                    ui.layout.Margin(v[0], v[1], v[2], v[3]);
                }
            }
            if (JsUtil::Has(js, "padding")) {
                const Json p = js.get("padding");
                if (p.IsNumber()) ui.layout.Padding(static_cast<int>(p.toNumber()));
                else if (p.IsArray()) {
                    const auto& arr = p.GetArray();
                    int v[4]{ 0, 0, 0, 0 };
                    for (size_t i = 0; i < arr.size() && i < 4; ++i)
                        if (arr[i].IsNumber()) v[i] = static_cast<int>(arr[i].toNumber());
                    ui.layout.Padding(v[0], v[1], v[2], v[3]);
                }
            }
            if (JsUtil::Has(js, "align")) ui.layout.align = JsUtil::AlignOf(JsUtil::Str(js, "align", "start"));
            if (JsUtil::Has(js, "visible")) ui.SetVisible(JsUtil::Bool(js, "visible", true));
            if (JsUtil::Has(js, "enabled")) ui.SetEnabled(JsUtil::Bool(js, "enabled", true));
        }

        static void BuildChildren(UI& parent, const Json& js) {
            const Json* list = nullptr;
            Json tmp;
            if (JsUtil::Has(js, "children")) tmp = js.get("children");
            else if (JsUtil::Has(js, "ptr")) tmp = js.get("ptr");
            if (!tmp.IsArray()) return;
            list = &tmp;
            for (const auto& item : list->GetArray()) {
                auto child = Build(item);
                if (child) parent.AddChild(std::move(child));
            }
        }

        static std::shared_ptr<UI> Build(const Json& js) {
            if (!js.IsObject()) return nullptr;
            std::string type = TopType(js);
            if (type.empty() || type == "auto") return nullptr;
            const auto it = Registry().find(type);
            if (it == Registry().end() || !it->second) return nullptr;
            return it->second(js);
        }
    };

    using UIComponentsManager = UIComponentManager;
}
