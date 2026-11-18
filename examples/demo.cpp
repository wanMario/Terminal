//
// Terminal / examples / demo.cpp
// 样式展示 demo：边框、Row/Column 排布、TStrFmt 富文本、滚动列表，
// 以及两个可拖动 Window（内含文字与单选框）。
//

#include "demo_common.hpp"

#include <algorithm>
#include <memory>
#include <string>

using namespace Terminal;
using namespace Terminal::Demo;

namespace {

    // Text 本身只显示纯文本；TStrFmt 需要一个能解析颜色段的轻量标签。
    class RichLabel : public UI {
    public:
        RichLabel(std::string text, const Style& base) : m_text(std::move(text)), m_base(base) {
            signature = "UIRichLabel";
            style = base;
            layout.height = 1;
        }

        void SetRichText(const std::string& text) { m_text = text; InvalidateLayout(); Invalidate(); }
        [[nodiscard]] const std::string& RichText() const { return m_text; }

        [[nodiscard]] int MeasureWidth() const override {
            return TStrFmt::Parse(m_text, m_base).Width();
        }
        [[nodiscard]] int MeasureHeight() const override { return 1; }

        void OnRender(ConsoleBackBuffer& buf) override {
            if (m_text.empty()) return;
            const int y = Top();
            if (y < 0 || y >= buf.Height()) return;
            TStrFmt::Parse(m_text, m_base).WriteTo(buf, std::max(Left(), 0), y);
        }

    private:
        std::string m_text;
        Style m_base;
    };

    std::shared_ptr<Border> Card(const std::string& title, const Color& titleColor) {
        const Theme& th = Theme::Current();
        auto card = std::make_shared<Border>(title);
        card->SetChars(Box::Round);
        card->SetBorderColor(th.border);
        card->SetTitleColor(titleColor);
        card->SetBackground(th.surface);
        card->layout.flow = Flow::Column;
        card->layout.gap = 0;
        card->layout.Padding(1);
        return card;
    }

    std::shared_ptr<Window> MakeChoiceWindow(const std::string& title, const int x, const int y,
        const std::string& intro, const std::vector<std::string>& options, const int selected) {
        const Theme& th = Theme::Current();
        auto win = std::make_shared<Window>(title, 32, 11);
        win->MoveTo(x, y);
        win->SetResizable(true);
        win->SetMinSize(24, 9);
        win->SetTitleColors(th.accent, th.surfaceAlt);
        win->SetBorderColor(th.border);

        auto text = std::make_shared<Text>(intro);
        text->SetStyle(Style(th.textMuted, th.surface));
        text->layout.height = 1;
        win->AddChild(text);

        auto group = std::make_shared<ChoiceGroup>();
        group->SetOptions(options);
        group->SetSelected(selected, false);
        group->layout.height = static_cast<int>(options.size());
        win->AddChild(group);
        return win;
    }

    std::shared_ptr<Panel> BuildUI() {
        Theme::UseDark();
        const Theme& th = Theme::Current();

        auto root = std::make_shared<DemoRoot>(Flow::Column);
        root->layout.Padding(1);
        root->layout.gap = 1;
        root->SetFill(true);
        root->SetBackgroundColor(th.background);

        auto header = std::make_shared<Panel>(Flow::Row);
        header->SetFill(false);
        header->layout.gap = 2;
        header->layout.height = 1;
        auto brand = std::make_shared<Text>("> Terminal UI · 样式展示");
        brand->layout.grow = 1;
        brand->SetStyle(Style(th.accent, th.background).SetBold());
        header->AddChild(brand);
        auto pill = std::make_shared<Badge>("TStrFmt");
        pill->SetColors(th.accent, th.accentText);
        header->AddChild(pill);
        root->AddChild(header);
        root->AddChild(std::make_shared<Divider>());

        auto body = std::make_shared<Panel>(Flow::Row);
        body->SetFill(false);
        body->layout.gap = 2;
        body->layout.grow = 1;

        // 左卡片：边框与排布
        auto left = Card("边框 / 排布", th.accent);
        left->layout.grow = 1;

        auto scroll = std::make_shared<ScrollView>();
        scroll->layout.grow = 1;
        scroll->layout.gap = 1;
        scroll->SetWheelStep(2);
        scroll->SetBackgroundColor(th.surface);

        auto info = std::make_shared<Text>("Border 支持 Round / Double / Single / Blank，\n可嵌套，标题与边框颜色独立。");
        info->SetStyle(Style(th.textMuted, th.surface));
        info->layout.height = 2;
        scroll->AddChild(info);

        auto row = std::make_shared<Panel>(Flow::Row);
        row->SetFill(false);
        row->layout.gap = 1;
        row->layout.height = 3;
        auto b1 = std::make_shared<Border>("Round");
        b1->SetChars(Box::Round);
        b1->SetBorderColor(th.success);
        b1->layout.Width(14);
        b1->AddChild(std::make_shared<Text>("ABC"));
        auto b2 = std::make_shared<Border>("Double");
        b2->SetChars(Box::Double);
        b2->SetBorderColor(th.warning);
        b2->layout.Width(14);
        b2->AddChild(std::make_shared<Text>("XYZ"));
        row->AddChild(b1);
        row->AddChild(b2);
        scroll->AddChild(row);

        auto nested = std::make_shared<Border>("嵌套 · Column");
        nested->SetChars(Box::Single);
        nested->SetBorderColor(th.info);
        nested->layout.flow = Flow::Column;
        nested->layout.gap = 0;
        nested->layout.height = 7;
        auto n1 = std::make_shared<Text>("外层 Border");
        n1->SetStyle(Style(th.textStrong, th.surface).SetBold());
        auto n2 = std::make_shared<Border>("内层");
        n2->SetChars(Box::Round);
        n2->SetBorderColor(th.accent);
        n2->layout.flow = Flow::Row;
        n2->layout.height = 3;
        n2->AddChild(std::make_shared<Text>("in "));
        n2->AddChild(std::make_shared<Text>("Border"));
        nested->AddChild(n1);
        nested->AddChild(n2);
        scroll->AddChild(nested);

        auto sep = std::make_shared<Text>("──────── 滚动列表 / ScrollView ────────");
        sep->SetStyle(Style(th.border, th.surface));
        scroll->AddChild(sep);
        for (int i = 1; i <= 18; ++i) {
            auto line = std::make_shared<Text>("  " + std::to_string(i) + "  Row 布局 / 滚动条 / 滚轮 / PgUp·PgDn");
            line->SetStyle(Style(i % 2 ? th.text : th.textMuted, th.surface));
            line->layout.height = 1;
            scroll->AddChild(line);
        }
        left->AddChild(scroll);

        // 右卡片：TStrFmt
        auto right = Card("TStrFmt · 颜色语法", th.info);
        right->layout.Width(52);
        right->layout.flow = Flow::Column;
        right->layout.gap = 0;

        const Style base(th.text, th.surface);
        const char* kRich[] = {
            "普通文字 / plain text",
            "{Red}红{/r} {Green}绿{/r} {Blue}蓝{/r} {Yellow}黄{/r}",
            "{#ff88aa}十六进制{#88ffcc} 双色{/r} 混排",
            "{&12}索引 12{/r} {&9}索引 9{/r} {&14}索引 14{/r}",
            "{/c}彩虹：从红开始逐字变色{/r}",
            "{Red | Blue}红蓝平均{/r}  {!Red}红取反{/r}",
            "{/r*0.8 + Red*0.6}混色表达式{/r}",
            "转义：\\{Red\\} 不会被当成指令",
        };
        for (const char* line : kRich) {
            auto rich = std::make_shared<RichLabel>(line, base);
            right->AddChild(rich);
        }

        auto note = std::make_shared<Text>("窗口内：文字 + 单选框，可拖动标题栏、Ctrl+边框缩放。");
        note->SetStyle(Style(th.textMuted, th.surface));
        note->layout.height = 2;
        right->AddChild(note);

        body->AddChild(left);
        body->AddChild(right);
        root->AddChild(body);
        root->AddChild(std::make_shared<Divider>());

        auto footer = std::make_shared<Panel>(Flow::Row);
        footer->SetFill(false);
        footer->layout.gap = 2;
        footer->layout.height = 1;
        auto keys = std::make_shared<Text>("Tab 切换焦点 · 滚轮滚动 · 拖动窗口标题 · Ctrl+拖边框缩放 · q 退出");
        keys->SetStyle(Style(th.textMuted, th.background));
        keys->layout.grow = 1;
        auto state = std::make_shared<Text>(Glyphs::DotUtf8() + " styles");
        state->SetStyle(Style(th.success, th.background).SetBold());
        state->SetAlign(LayoutPos::Right);
        footer->AddChild(keys);
        footer->AddChild(state);
        root->AddChild(footer);

        auto overlay = std::make_shared<OverlayHost>();
        auto winA = MakeChoiceWindow("文字 · 单选框 A", 6, 2,
            "选择一种主题（单选框）", { "深色 Dark", "浅色 Light", "霓虹 Neon" }, 0);
        auto winB = MakeChoiceWindow("文字 · 单选框 B", 46, 6,
            "选择一种密度（单选框）", { "紧凑", "标准", "宽松" }, 1);
        overlay->AddChild(winA);
        overlay->AddChild(winB);
        root->AddChild(overlay);

        return root;
    }

} // namespace

int main(const int argc, char** argv) {
    const auto root = BuildUI();
    return RunMain(argc, argv, root,
        "usage: terminal_demo [--frame|--dump|--bench N] [--truecolor|--mono]\n"
        "  样式展示：Border / Row / Column / TStrFmt / ScrollView，两个 Window 内含文字与单选框\n"
        "  Tab 切换焦点，滚轮滚动列表，拖动标题栏移动窗口，Ctrl+拖边框缩放\n");
}
