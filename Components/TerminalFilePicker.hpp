//
// Terminal / Components / TerminalFilePicker.hpp
// 文件选择器：
//   1) PickNative(options)       直接弹出 Windows 原生 IFileOpenDialog（文件/文件夹/多选）
//   2) FilePickerWindow(options) 基于 Terminal::Window 的终端文件对话框
//
// 终端版不阻塞主循环；调用方把它加入 OverlayHost，打开后查询 IsDone()/Result()，
// 或通过 onAccepted / onCancelled 接结果。
//

#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "../Utils/Utf8.hpp"
#include "TerminalButton.hpp"
#include "TerminalChoiceGroup.hpp"
#include "TerminalPanel.hpp"
#include "TerminalScrollView.hpp"
#include "TerminalText.hpp"
#include "TerminalTextInput.hpp"
#include "TerminalWindow.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shobjidl.h>
#endif

namespace Terminal {

    // 一个筛选器：name 是对话框里显示的名字，patterns 用分号分隔，例如 "*.txt;*.md"。
    struct FileFilter {
        std::string name{ "所有文件" };
        std::string patterns{ "*.*" };
    };

    struct FilePickerOptions {
        std::string title{ "选择文件" };
        std::string initialPath{};          // 文件或目录；空则使用当前目录
        bool selectFolders{ false };        // true=选文件夹，false=选文件
        bool allowMultiple{ false };        // 文件/文件夹多选
        std::vector<FileFilter> filters{};  // 为空时等价于“所有文件”
    };

    //------------------------------------------------------------------
    // 路径 UTF-8 <-> 原生 wide
    //------------------------------------------------------------------
    namespace FilePickerDetail {
        inline std::filesystem::path FromUtf8(const std::string& text) {
            if (text.empty()) return {};
#ifdef _WIN32
            return std::filesystem::path(Utf8::ToWide(text));
#else
            return std::filesystem::path(text);
#endif
        }

        inline std::string ToUtf8(const std::filesystem::path& path) {
            if (path.empty()) return {};
#ifdef _WIN32
            const std::wstring w = path.wstring();
            if (w.empty()) return {};
            const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                nullptr, 0, nullptr, nullptr);
            if (n <= 0) return {};
            std::string out(static_cast<std::size_t>(n), '\0');
            WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                out.data(), n, nullptr, nullptr);
            return out;
#else
            return path.string();
#endif
        }

        inline std::string LowerAscii(std::string s) {
            for (auto& ch : s) {
                if (static_cast<unsigned char>(ch) < 0x80) {
                    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                }
            }
            return s;
        }

        // 仅用于扩展名/文件名筛选；* 和 ? 按 UTF-8 字节处理，ASCII 文件名足够。
        inline bool WildcardMatch(const std::string& text, const std::string& pattern) {
            const std::string t = LowerAscii(text);
            const std::string p = LowerAscii(pattern);
            std::size_t ti = 0, pi = 0, star = std::string::npos, mark = 0;
            while (ti < t.size()) {
                if (pi < p.size() && (p[pi] == '?' || p[pi] == t[ti])) {
                    ++ti; ++pi;
                }
                else if (pi < p.size() && p[pi] == '*') {
                    star = pi++;
                    mark = ti;
                }
                else if (star != std::string::npos) {
                    pi = star + 1;
                    ti = ++mark;
                }
                else return false;
            }
            while (pi < p.size() && p[pi] == '*') ++pi;
            return pi == p.size();
        }

        inline bool MatchesAnyPattern(const std::string& filename, const std::string& patterns) {
            std::size_t start = 0;
            while (start <= patterns.size()) {
                const auto end = patterns.find(';', start);
                std::string one = patterns.substr(start, end == std::string::npos ? std::string::npos : end - start);
                one.erase(0, one.find_first_not_of(" \t"));
                one.erase(one.find_last_not_of(" \t") + 1);
                if (!one.empty() && WildcardMatch(filename, one)) return true;
                if (end == std::string::npos) break;
                start = end + 1;
            }
            return false;
        }

        inline std::vector<FileFilter> NormalizedFilters(const FilePickerOptions& options) {
            if (!options.filters.empty()) return options.filters;
            return { FileFilter{ "所有文件", "*.*" } };
        }
    }

    //------------------------------------------------------------------
    // A. Windows 原生文件选择器
    //------------------------------------------------------------------
    inline std::vector<std::string> PickNative(const FilePickerOptions& options) {
#ifdef _WIN32
        std::vector<std::string> result;

        const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        const bool coInitialized = SUCCEEDED(co);
        if (FAILED(co) && co != RPC_E_CHANGED_MODE) return result;

        IFileOpenDialog* dialog = nullptr;
        const HRESULT created = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
            IID_IFileOpenDialog, reinterpret_cast<void**>(&dialog));
        if (FAILED(created) || dialog == nullptr) {
            if (coInitialized) CoUninitialize();
            return result;
        }

        FILEOPENDIALOGOPTIONS dialogOptions = 0;
        dialog->GetOptions(&dialogOptions);
        dialogOptions |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
        if (options.selectFolders) dialogOptions |= FOS_PICKFOLDERS;
        else dialogOptions |= FOS_FILEMUSTEXIST;
        if (options.allowMultiple) dialogOptions |= FOS_ALLOWMULTISELECT;
        dialog->SetOptions(dialogOptions);
        dialog->SetTitle(Utf8::ToWide(options.title).c_str());

        // 筛选器：COMDLG_FILTERSPEC 只保存指针，names/patterns 必须先构造好且不能再扩容。
        const auto filters = FilePickerDetail::NormalizedFilters(options);
        std::vector<std::wstring> names;
        std::vector<std::wstring> patterns;
        names.reserve(filters.size());
        patterns.reserve(filters.size());
        for (const auto& f : filters) {
            names.push_back(Utf8::ToWide(f.name));
            patterns.push_back(Utf8::ToWide(f.patterns));
        }
        std::vector<COMDLG_FILTERSPEC> specs;
        specs.reserve(filters.size());
        for (std::size_t i = 0; i < filters.size(); ++i) {
            specs.push_back(COMDLG_FILTERSPEC{ names[i].data(), patterns[i].data() });
        }
        dialog->SetFileTypes(static_cast<UINT>(specs.size()), specs.data());
        dialog->SetFileTypeIndex(1);

        // 初始目录 / 初始文件名
        if (!options.initialPath.empty()) {
            std::filesystem::path p = FilePickerDetail::FromUtf8(options.initialPath);
            std::error_code ec;
            if (!p.empty() && !std::filesystem::is_directory(p, ec)) p = p.parent_path();
            if (!p.empty()) {
                IShellItem* folder = nullptr;
                const HRESULT folderResult = SHCreateItemFromParsingName(p.wstring().c_str(), nullptr,
                    IID_IShellItem, reinterpret_cast<void**>(&folder));
                if (SUCCEEDED(folderResult) && folder != nullptr) {
                    dialog->SetFolder(folder);
                    folder->Release();
                }
            }
        }

        const HRESULT shown = dialog->Show(nullptr);
        if (SUCCEEDED(shown)) {
            IShellItemArray* items = nullptr;
            if (SUCCEEDED(dialog->GetResults(&items)) && items != nullptr) {
                DWORD count = 0;
                items->GetCount(&count);
                for (DWORD i = 0; i < count; ++i) {
                    IShellItem* item = nullptr;
                    if (FAILED(items->GetItemAt(i, &item)) || item == nullptr) continue;
                    LPWSTR name = nullptr;
                    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &name)) && name != nullptr) {
                        result.push_back(FilePickerDetail::ToUtf8(std::filesystem::path(name)));
                        CoTaskMemFree(name);
                    }
                    item->Release();
                }
                items->Release();
            }
        }

        dialog->Release();
        if (coInitialized) CoUninitialize();
        return result;
#else
        (void)options;
        return {};
#endif
    }

    //------------------------------------------------------------------
    // B. 基于 Terminal::Window 的文件选择器
    //------------------------------------------------------------------
    class FilePickerWindow : public Window {
    public:
        std::function<void(const std::vector<std::string>&)> onAccepted{};
        std::function<void()> onCancelled{};

        explicit FilePickerWindow(FilePickerOptions options)
            : Window(options.title.empty() ? "选择文件" : options.title, 76, 24), m_options(std::move(options)) {
            signature = "UIFilePicker";
            m_options.filters = FilePickerDetail::NormalizedFilters(m_options);
            m_filterIndex = 0;

            // 不要在这里扫描文件系统：构造一个隐藏窗口不应该有 I/O 副作用。
            m_current = m_options.initialPath;
            if (m_current.empty()) {
                std::error_code ec;
                const auto cwd = std::filesystem::current_path(ec);
                if (!ec) m_current = FilePickerDetail::ToUtf8(cwd);
            }

            layout.width = 76;
            layout.height = 24;
            layout.gap = 0;
            draggable = true;
            closable = true;
            resizable = false;

            BuildUi();
        }

        void Open() {
            m_done = false;
            m_cancelled = false;
            m_result.clear();
            Show(true);
            if (m_entries.empty()) NavigateTo(m_current);
            if (!m_done) {
                m_pathInput->SetValue(m_current);
                InvalidateLayout();
            }
        }

        void Cancel() {
            if (m_done) return;
            m_cancelled = true;
            m_done = true;
            m_result.clear();
            Show(false);
            if (onCancelled) onCancelled();
        }

        [[nodiscard]] bool IsDone() const noexcept { return m_done; }
        [[nodiscard]] bool WasCancelled() const noexcept { return m_cancelled; }
        [[nodiscard]] const std::vector<std::string>& Result() const noexcept { return m_result; }
        [[nodiscard]] const std::string& CurrentPath() const noexcept { return m_current; }

        void NavigateTo(const std::string& rawPath) {
            std::filesystem::path p = FilePickerDetail::FromUtf8(rawPath);
            std::error_code ec;
            if (p.empty()) p = std::filesystem::current_path(ec);
            if (!p.empty() && !std::filesystem::is_directory(p, ec)) {
                if (std::filesystem::is_regular_file(p, ec)) p = p.parent_path();
            }
            if (p.empty() || !std::filesystem::is_directory(p, ec)) {
                SetStatus("目录不存在：" + rawPath);
                m_pathInput->SetValue(m_current);
                return;
            }
            m_current = FilePickerDetail::ToUtf8(p);
            Reload();
        }

        void Refresh() {
            if (m_current.empty()) NavigateTo(m_current);
            else Reload();
        }

        bool OnKey(const xChar& key) override {
            if (key == SKey::Esc) {
                Cancel();
                return true;
            }
            return false;
        }

        bool OnMouse(const xMouse& ev) override {
            if (ev.action == MouseAction::DoubleClick && ev.IsLeft() &&
                m_listScroll && m_list && m_listScroll->bounds.Contains(Position{ ev.x, ev.y })) {
                const int index = ev.y - m_list->Top();
                if (index >= 0 && index < static_cast<int>(m_entries.size())) {
                    const auto& e = m_entries[static_cast<std::size_t>(index)];
                    if (e.directory) NavigateTo(FilePickerDetail::ToUtf8(e.path));
                    else Accept({ FilePickerDetail::ToUtf8(e.path) });
                    return true;
                }
            }
            return Window::OnMouse(ev);
        }

        bool OnMessage(const Message& msg) override {
            if (msg.type == "close") {
                Cancel();
                return true;
            }
            return Window::OnMessage(msg);
        }

    private:
        struct Entry {
            std::filesystem::path path{};
            std::string name{};
            bool directory{ false };
        };

        FilePickerOptions m_options{};
        std::size_t m_filterIndex{ 0 };
        std::string m_current{};
        std::vector<Entry> m_entries{};
        std::vector<std::string> m_result{};
        bool m_done{ false };
        bool m_cancelled{ false };

        std::shared_ptr<Panel> m_pathRow{};
        std::shared_ptr<TextInput> m_pathInput{};
        std::shared_ptr<Button> m_up{};
        std::shared_ptr<ScrollView> m_listScroll{};
        std::shared_ptr<ChoiceGroup> m_list{};
        std::shared_ptr<ChoiceGroup> m_filters{};
        std::shared_ptr<Text> m_status{};
        std::shared_ptr<Button> m_open{};
        std::shared_ptr<Button> m_cancel{};

        void BuildUi() {
            m_pathRow = std::make_shared<Panel>(Flow::Row);
            m_pathRow->SetFill(false);
            m_pathRow->layout.gap = 1;
            m_pathRow->layout.height = 3;
            m_pathRow->layout.width = 70;

            m_pathInput = std::make_shared<TextInput>();
            m_pathInput->placeholder = "输入目录后回车";
            m_pathInput->layout.height = 3;
            m_pathInput->layout.grow = 1;
            m_pathInput->onSubmit = [this](const std::string& value) { NavigateTo(value); };
            m_pathRow->AddChild(m_pathInput);

            m_up = std::make_shared<Button>("上级");
            m_up->layout.width = 8;
            m_up->layout.height = 3;
            m_up->onClick = [this] {
                const auto p = FilePickerDetail::FromUtf8(m_current).parent_path();
                if (!p.empty()) NavigateTo(FilePickerDetail::ToUtf8(p));
            };
            m_pathRow->AddChild(m_up);

            m_listScroll = std::make_shared<ScrollView>();
            m_listScroll->layout.height = 9;
            m_listScroll->layout.width = 70;
            m_listScroll->SetWheelStep(2);
            m_listScroll->SetShowScrollbar(true);
            m_listScroll->SetShowHScrollbar(true);
            m_list = std::make_shared<ChoiceGroup>();
            m_list->layout.width = 68;
            m_list->layout.height = 1;
            m_list->showHint = false;
            m_list->SetMulti(m_options.allowMultiple);
            m_listScroll->AddChild(m_list);

            m_filters = std::make_shared<ChoiceGroup>();
            m_filters->layout.width = 70;
            m_filters->layout.height = static_cast<int>(std::min<std::size_t>(3, m_options.filters.size()));
            if (m_options.selectFolders) {
                m_filters->isVisible = false;
                m_filters->layout.height = 0;
            }
            else {
                std::vector<std::string> names;
                for (const auto& f : m_options.filters) names.push_back(f.name);
                m_filters->SetOptions(names);
                m_filters->SetSelected(0);
                m_filters->onChange = [this](const std::vector<int>& selected) {
                    if (selected.empty()) return;
                    m_filterIndex = static_cast<std::size_t>(selected.front());
                    Reload();
                };
            }

            m_status = std::make_shared<Text>("");
            m_status->layout.height = 1;
            m_status->SetStyle(Style(Theme::Current().textMuted, Theme::Current().surface));

            auto buttons = std::make_shared<Panel>(Flow::Row);
            buttons->SetFill(false);
            buttons->layout.gap = 1;
            buttons->layout.height = 3;
            buttons->layout.width = 70;

            m_open = std::make_shared<Button>(m_options.selectFolders ? "选择文件夹" : "打开");
            m_open->layout.width = 14;
            m_open->layout.height = 3;
            m_open->onClick = [this] { OpenSelected(); };
            buttons->AddChild(m_open);

            m_cancel = std::make_shared<Button>("取消");
            m_cancel->layout.width = 10;
            m_cancel->layout.height = 3;
            m_cancel->onClick = [this] { Cancel(); };
            buttons->AddChild(m_cancel);

            AddChild(m_pathRow);
            AddChild(m_listScroll);
            AddChild(m_filters);
            AddChild(m_status);
            AddChild(buttons);
        }

        void SetStatus(const std::string& text) {
            if (m_status) m_status->SetText(text);
        }

        void Reload() {
            m_entries.clear();
            const auto current = FilePickerDetail::FromUtf8(m_current);
            std::error_code ec;
            if (!std::filesystem::is_directory(current, ec)) {
                SetStatus("目录不存在：" + m_current);
                return;
            }

            const auto parent = current.parent_path();
            if (!parent.empty() && parent != current) {
                m_entries.push_back(Entry{ parent, "..", true });
            }

            const auto filters = FilePickerDetail::NormalizedFilters(m_options);
            std::filesystem::directory_iterator it(current, ec);
            if (!ec) {
                for (; it != std::filesystem::directory_iterator(); it.increment(ec)) {
                    if (ec) { ec.clear(); continue; }
                    const auto path = it->path();
                    std::error_code typeError;
                    const bool isDir = it->is_directory(typeError);
                    if (typeError) { typeError.clear(); continue; }
                    if (!isDir && m_options.selectFolders) continue;

                    const std::string name = FilePickerDetail::ToUtf8(path.filename());
                    if (name.empty()) continue;
                    if (!isDir) {
                        bool matched = false;
                        const auto& currentFilter = filters[std::min(m_filterIndex, filters.size() - 1)];
                        matched = FilePickerDetail::MatchesAnyPattern(name, currentFilter.patterns);
                        if (!matched) continue;
                    }
                    m_entries.push_back(Entry{ path, name, isDir });
                }
            }

            std::sort(m_entries.begin(), m_entries.end(), [](const Entry& a, const Entry& b) {
                if (a.name == "..") return true;
                if (b.name == "..") return false;
                if (a.directory != b.directory) return a.directory > b.directory;
                return FilePickerDetail::LowerAscii(a.name) < FilePickerDetail::LowerAscii(b.name);
            });

            std::vector<std::string> labels;
            for (const auto& e : m_entries) {
                labels.push_back(e.directory ? "[DIR] " + e.name : e.name);
            }
            m_list->SetOptions(labels);
            m_list->SetMulti(m_options.allowMultiple);
            m_list->layout.height = std::max(static_cast<int>(labels.size()), 1);
            m_listScroll->SetScroll(0);
            m_pathInput->SetValue(m_current);
            SetStatus("");
            InvalidateLayout();
            Invalidate();
        }

        void OpenSelected() {
            if (m_done) return;
            const auto selected = m_list->SelectedIndices();

            if (m_options.selectFolders) {
                std::vector<std::string> picked;
                for (const int i : selected) {
                    if (i >= 0 && i < static_cast<int>(m_entries.size()) && m_entries[static_cast<std::size_t>(i)].directory)
                        picked.push_back(FilePickerDetail::ToUtf8(m_entries[static_cast<std::size_t>(i)].path));
                }
                if (picked.empty()) picked.push_back(m_current);
                Accept(picked);
                return;
            }

            if (selected.size() == 1 && selected.front() >= 0 &&
                selected.front() < static_cast<int>(m_entries.size()) &&
                m_entries[static_cast<std::size_t>(selected.front())].directory) {
                NavigateTo(FilePickerDetail::ToUtf8(m_entries[static_cast<std::size_t>(selected.front())].path));
                return;
            }

            std::vector<std::string> picked;
            for (const int i : selected) {
                if (i < 0 || i >= static_cast<int>(m_entries.size())) continue;
                if (!m_entries[static_cast<std::size_t>(i)].directory)
                    picked.push_back(FilePickerDetail::ToUtf8(m_entries[static_cast<std::size_t>(i)].path));
            }
            if (picked.empty()) {
                SetStatus("请选择文件（选中目录后点“打开”可进入）");
                return;
            }
            Accept(picked);
        }

        void Accept(std::vector<std::string> picked) {
            if (m_done) return;
            m_result = std::move(picked);
            m_cancelled = false;
            m_done = true;
            Show(false);
            if (onAccepted) onAccepted(m_result);
        }
    };

} // namespace Terminal
