//
// Terminal / examples / zip_tool_demo.cpp
// 压缩包工具 demo：基于 Bandizip 的 .zip / .7z 创建、解压、列表、完整性测试，
// 支持密码输入；.rar 只读（Bandizip 没有 rar 编码器）。
//

#include "demo_common.hpp"

#include "../Core/TerminalArchive.hpp"

#include <memory>
#include <string>

using namespace Terminal;
using namespace Terminal::Demo;

namespace {

    std::shared_ptr<TextInput> Input(const std::string& label, const std::string& value,
        const int width) {
        auto input = std::make_shared<TextInput>();
        input->SetLabel(label);
        input->SetValue(value);
        input->layout.width = width;
        input->layout.height = 3;
        return input;
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
        auto brand = std::make_shared<Text>("> Terminal UI · 压缩包工具");
        brand->layout.grow = 1;
        brand->SetStyle(Style(th.accent, th.background).SetBold());
        header->AddChild(brand);
        auto pill = std::make_shared<Badge>(Archive::Available() ? "libarchive 就绪" : "归档 DLL 缺失");
        pill->SetColors(Archive::Available() ? th.success : th.danger, th.accentText);
        header->AddChild(pill);
        root->AddChild(header);
        root->AddChild(std::make_shared<Divider>());

        auto archive = Input("压缩包", "demo_archive.zip", 34);
        auto password = Input("密码", "", 22);
        auto source = Input("要打包的路径", "README.md", 34);
        auto destination = Input("解压到", "unpacked", 22);

        auto row1 = std::make_shared<Panel>(Flow::Row);
        row1->SetFill(false);
        row1->layout.gap = 1;
        row1->layout.height = 3;
        row1->AddChild(archive);
        row1->AddChild(password);
        root->AddChild(row1);

        auto row2 = std::make_shared<Panel>(Flow::Row);
        row2->SetFill(false);
        row2->layout.gap = 1;
        row2->layout.height = 3;
        row2->AddChild(source);
        row2->AddChild(destination);
        root->AddChild(row2);

        auto output = std::make_shared<Text>("就绪。后端: " + (Archive::Available() ? Archive::BandizipPath() : std::string("未找到 TerminalArchive.dll")));
        output->SetStyle(Style(th.textMuted, th.surface));
        output->layout.height = 1;

        auto outputScroll = std::make_shared<ScrollView>();
        outputScroll->layout.grow = 1;
        outputScroll->layout.gap = 0;
        outputScroll->SetWheelStep(2);
        outputScroll->SetBackgroundColor(th.surface);
        outputScroll->AddChild(output);

        auto status = std::make_shared<Text>(Glyphs::DotUtf8() + " 等待操作");
        status->SetStyle(Style(th.textMuted, th.background));
        status->layout.height = 1;

        auto appendLog = [output](const std::string& text) {
            std::string all = output->GetText();
            if (all.empty() || all == "就绪") all.clear();
            all += text;
            if (all.size() > 6000) all.erase(0, all.size() - 6000);
            output->SetText(all);
            output->layout.height = output->MeasureHeight();
            output->InvalidateLayout();
            };

        auto runArchive = [archive, password, source, destination, status, appendLog](const std::string& action) {
            Archive::Options options;
            options.password = password->GetValue();
            options.level = 5;
            Archive::Result result;
            const std::string archivePath = archive->GetValue();
            const std::string sourcePath = source->GetValue();
            const std::string destinationPath = destination->GetValue();

            if (action == "zip" || action == "7z") {
                const std::string path = action == "zip" ? archivePath : std::string("demo_archive.7z");
                result = Archive::Create(path, { sourcePath }, options);
                appendLog((result.ok ? "[OK] " : "[FAIL] ") + action + " -> " + path + "\n");
            }
            else if (action == "extract") {
                result = Archive::Extract(archivePath, destinationPath, options);
                appendLog(std::string(result.ok ? "[OK] " : "[FAIL] ") + "extract " + archivePath + " -> " + destinationPath + "\n");
            }
            else if (action == "list") {
                result = Archive::List(archivePath, options.password);
                appendLog(std::string(result.ok ? "[OK] " : "[FAIL] ") + "list " + archivePath + "\n");
            }
            else if (action == "test") {
                result = Archive::Test(archivePath, options.password);
                appendLog(std::string(result.ok ? "[OK] " : "[FAIL] ") + "test " + archivePath + "\n");
            }

            if (!result.output.empty()) appendLog(result.output + "\n");
            if (!result.error.empty()) appendLog("[error] " + result.error + "\n");
            const std::string summary = (result.ok ? "成功" : "失败") + std::string("（exit ") + std::to_string(result.exitCode) + "）";
            MessageQueue::Post(status.get(), "setText", Glyphs::DotUtf8() + " " + action + " " + summary);
            };

        auto zip = std::make_shared<Button>("创建 ZIP");
        zip->layout.Width(10);
        zip->SetColor(th.success);
        zip->onClick = [runArchive] { runArchive("zip"); };

        auto seven = std::make_shared<Button>("创建 7z");
        seven->layout.Width(10);
        seven->SetColor(th.info);
        seven->onClick = [runArchive] { runArchive("7z"); };

        auto extract = std::make_shared<Button>("解压");
        extract->layout.Width(8);
        extract->SetColor(th.warning);
        extract->onClick = [runArchive] { runArchive("extract"); };

        auto list = std::make_shared<Button>("列表");
        list->layout.Width(8);
        list->SetColor(th.accent);
        list->onClick = [runArchive] { runArchive("list"); };

        auto test = std::make_shared<Button>("测试");
        test->layout.Width(8);
        test->SetColor(th.textMuted);
        test->onClick = [runArchive] { runArchive("test"); };

        auto buttons = std::make_shared<Panel>(Flow::Row);
        buttons->SetFill(false);
        buttons->layout.gap = 1;
        buttons->layout.height = 3;
        buttons->AddChild(zip);
        buttons->AddChild(seven);
        buttons->AddChild(extract);
        buttons->AddChild(list);
        buttons->AddChild(test);
        root->AddChild(buttons);

        root->AddChild(outputScroll);
        root->AddChild(std::make_shared<Divider>());
        root->AddChild(status);

        auto footer = std::make_shared<Text>("密码通过 -p: 传给 Bandizip · .rar 只读 · Tab 切换焦点 · 滚轮查看日志 · q 退出");
        footer->SetStyle(Style(th.textMuted, th.background));
        footer->layout.height = 1;
        root->AddChild(footer);
        return root;
    }

} // namespace

int main(const int argc, char** argv) {
    const auto root = BuildUI();
    return RunMain(argc, argv, root,
        "usage: zip_tool_demo [--frame|--dump|--bench N] [--truecolor|--mono]\n"
        "  .zip/.7z 创建、解压、列表、测试；密码由 Bandizip -p: 处理；.rar 只读\n");
}
