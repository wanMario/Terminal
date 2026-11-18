//
// Terminal / examples / table_editor_demo.cpp
// 表格编辑器 demo：TStrFmt 单元格、可编辑、筛选、CSV / SpreadsheetML 导出。
//

#include "demo_common.hpp"

#include <memory>
#include <string>

using namespace Terminal;
using namespace Terminal::Demo;

namespace {

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
        auto brand = std::make_shared<Text>("> Terminal UI · 表格编辑器");
        brand->layout.grow = 1;
        brand->SetStyle(Style(th.accent, th.background).SetBold());
        header->AddChild(brand);
        auto pill = std::make_shared<Badge>("Editable");
        pill->SetColors(th.success, th.accentText);
        header->AddChild(pill);
        root->AddChild(header);
        root->AddChild(std::make_shared<Divider>());

        auto table = std::make_shared<Table>();
        table->AddColumn("姓名", 14);
        table->AddColumn("年龄", 8, LayoutPos::Right);
        table->AddColumn("城市", 14);
        table->AddColumn("状态", 12);
        table->SetEditable(true);

        table->AddRow({ "{Red}Alice{/r}", "20", "南昌", "{#00ff88}在线{/r}" });
        table->AddRow({ "Bob", "31", "上海", "离线" });
        table->AddRow({ "{#00ffff}Carol{/r}", "27", "北京", "在线" });
        table->AddRow({ "Dave", "24", "杭州", "{Yellow}忙{/r}" });
        table->AddRow({ "Eve", "38", "广州", "在线" });
        table->AddRow({ "{#ff88cc}Fiona{/r}", "19", "成都", "离线" });
        table->AddRow({ "George", "45", "武汉", "在线" });
        table->AddRow({ "Henry", "29", "西安", "在线" });
        table->AddRow({ "Ivy", "33", "南京", "{Red}离线{/r}" });
        table->AddRow({ "Jack", "52", "苏州", "在线" });
        table->AddRow({ "Kate", "26", "长沙", "在线" });
        table->AddRow({ "Leo", "41", "厦门", "离线" });

        auto status = std::make_shared<Text>(Glyphs::DotUtf8() + " 就绪：单击选中，双击 / Enter 编辑，Esc 取消");
        status->SetStyle(Style(th.textMuted, th.background));
        status->layout.height = 1;

        auto toolbar = std::make_shared<Panel>(Flow::Row);
        toolbar->SetFill(false);
        toolbar->layout.gap = 1;
        toolbar->layout.height = 3;

        auto filter = std::make_shared<TextInput>();
        filter->SetLabel("筛选");
        filter->SetPlaceholder("姓名包含…（清空恢复）");
        filter->layout.width = 30;
        filter->layout.height = 3;
        toolbar->AddChild(filter);

        auto addRow = std::make_shared<Button>("添加行");
        addRow->layout.Width(10);
        addRow->SetColor(th.success);
        toolbar->AddChild(addRow);

        auto exportCsv = std::make_shared<Button>("导出 CSV");
        exportCsv->layout.Width(12);
        exportCsv->SetColor(th.info);
        toolbar->AddChild(exportCsv);

        auto exportXml = std::make_shared<Button>("导出 XML");
        exportXml->layout.Width(12);
        exportXml->SetColor(th.warning);
        toolbar->AddChild(exportXml);

        root->AddChild(toolbar);

        auto scroll = std::make_shared<ScrollView>();
        scroll->layout.grow = 1;
        scroll->layout.gap = 0;
        scroll->SetWheelStep(2);
        scroll->SetBackgroundColor(th.surface);
        table->layout.width = table->MeasureWidth();
        table->layout.height = table->MeasureHeight();
        scroll->AddChild(table);
        root->AddChild(scroll);

        auto refreshTableSize = [table] {
            table->layout.width = table->MeasureWidth();
            table->layout.height = table->MeasureHeight();
            table->InvalidateLayout();
            };
        auto setStatus = [status](const std::string& msg) {
            MessageQueue::Post(status.get(), "setText", Glyphs::DotUtf8() + " " + msg);
            };

        filter->onChange = [table, setStatus](const std::string& text) {
            if (text.empty()) {
                table->ClearFilter();
                setStatus("筛选已清除，共 " + std::to_string(table->RowCount()) + " 行");
            }
            else {
                table->SetFilter(0, text);
                setStatus("筛选 \"" + text + "\"：可见 " + std::to_string(table->FilteredRowCount()) + " 行");
            }
            };

        addRow->onClick = [table, refreshTableSize, setStatus] {
            const auto n = table->RowCount() + 1;
            table->AddRow({ "New" + std::to_string(n), "0", "城市", "{#00ff88}新建{/r}" });
            refreshTableSize();
            setStatus("已添加第 " + std::to_string(n) + " 行");
            };

        exportCsv->onClick = [table, setStatus] {
            const bool ok = table->ExportCsv("table_demo.csv");
            setStatus(ok ? "已导出 table_demo.csv" : "导出 table_demo.csv 失败");
            };

        exportXml->onClick = [table, setStatus] {
            const bool ok = table->ExportExcel("table_demo.xml");
            setStatus(ok ? "已导出 table_demo.xml（SpreadsheetML）" : "导出 table_demo.xml 失败");
            };

        table->onCellChanged = [table, setStatus](const std::size_t row, const std::size_t col, const std::string& value) {
            setStatus("已修改 [" + std::to_string(row) + "," + std::to_string(col) + "] = " + value);
            (void)table;
            };

        root->AddChild(std::make_shared<Divider>());
        root->AddChild(status);

        auto footer = std::make_shared<Text>("单击 / 双击单元格 · Enter 编辑 · Esc 取消 · 滚轮滚动 · Tab 切换焦点 · q 退出");
        footer->SetStyle(Style(th.textMuted, th.background));
        footer->layout.height = 1;
        root->AddChild(footer);

        return root;
    }

} // namespace

int main(const int argc, char** argv) {
    const auto root = BuildUI();
    return RunMain(argc, argv, root,
        "usage: table_editor_demo [--frame|--dump|--bench N] [--truecolor|--mono]\n"
        "  表格编辑器：可编辑单元格、姓名筛选、添加行、导出 CSV / SpreadsheetML\n");
}
