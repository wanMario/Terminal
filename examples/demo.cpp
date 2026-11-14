//
// Terminal / examples / demo.cpp
// 演示：布局 + 嵌套 + 八种元件（含 Divider / Badge 装饰件）+ 配色 + 键盘焦点 + 鼠标交互
//   > terminal_demo              交互模式（Tab 切换焦点，Esc 退出）
//   > terminal_demo --frame      渲染一帧并输出 ANSI（非交互，便于查看配色/截图）
//   > terminal_demo --selftest   纯逻辑自检（不依赖控制台尺寸）
//

#include <cstdio>
#include <chrono>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "../Terminal.hpp"   // 公共入口（也引入 Terminal::Version() 的静态库符号声明）

#include "../Core/UI.hpp"
#include "../Core/Layout.hpp"
#include "../Core/TerminalBuffer.hpp"
#include "../Core/TerminalKeyFocus.hpp"
#include "../Core/Input.hpp"
#include "../Core/TerminalMouse.hpp"
#include "../Core/TerminalComponentManager.hpp"

#include "../Utils/Color.hpp"
#include "../Utils/Theme.hpp"
#include "../Utils/Utf8.hpp"

#include "../Core/TerminalMessage.hpp"
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

using namespace Terminal;

namespace {

    // --truecolor：交互模式改用 ANSI 24 位真彩刷新
    int g_colorMode = 0;   // 0=自动探测  1=强制真彩  2=强制 16 色

    // 退出请求也走消息队列：按钮只投递，主循环处理
    bool g_quitRequested = false;

    // assets 目录：兼容「从项目根运行」和「从 build 目录运行」两种 cwd
    std::string DemoAsset(const std::string& name) {
        namespace fs = std::filesystem;
        const fs::path nearSource = fs::path(__FILE__).parent_path().parent_path() / "assets" / name;
        if (fs::exists(nearSource)) return nearSource.string();
        const fs::path cwd1 = fs::path("assets") / name;
        if (fs::exists(cwd1)) return cwd1.string();
        const fs::path cwd2 = fs::path("..") / "assets" / name;
        if (fs::exists(cwd2)) return cwd2.string();
        return nearSource.string();
    }

    // std::filesystem 在 Windows 上用宽路径；u8string 转回 UTF-8 字符串给 AudioPlayer
    std::filesystem::path U8Path(const std::string& text) {
        const std::u8string u8(reinterpret_cast<const char8_t*>(text.data()), text.size());
        return std::filesystem::path(u8);
    }

    std::string U8Text(const std::filesystem::path& path) {
        const auto text = path.u8string();
        return std::string(reinterpret_cast<const char*>(text.data()), text.size());
    }

    //----------------------------------------------------------------------
    // 直接扫描 D:\CloudMusic 的播放器窗口（放在 Window 里演示弹窗 + 排版）
    //----------------------------------------------------------------------
    class MusicWindow : public Window {
    public:
        MusicWindow() : Window("音乐播放器", 64, 18) {
            layout.gap = 1;
            style = Style(Theme::Current().text, Theme::Current().surface);
            SetTitleColors(Theme::Current().text, Theme::Current().surfaceAlt);
            SetBorderColor(Theme::Current().border);
            SetResizable(true);
            SetMinSize(32, 10);

            ScanTracks();

            m_status = std::make_shared<Text>("正在加载音乐目录…");
            m_status->layout.Width(62);
            m_status->layout.height = 1;
            m_status->SetStyle(Style(Theme::Current().textMuted, Theme::Current().surface));
            AddChild(m_status);

            m_scroll = std::make_shared<ScrollView>();
            m_scroll->layout.height = 4;
            m_scroll->layout.width = 62;
            m_scroll->SetWheelStep(2);
            m_scroll->SetBackgroundColor(Theme::Current().surface);
            AddChild(m_scroll);

            m_list = std::make_shared<ChoiceGroup>();
            m_list->SetOptions(m_names);
            m_list->layout.height = static_cast<int>(m_names.size());
            m_list->onChange = [this](const std::vector<int>& selected) {
                if (!selected.empty()) PlayIndex(selected.front());
                };
            m_scroll->AddChild(m_list);

            auto transport = std::make_shared<Panel>(Flow::Row);
            transport->SetFill(false);
            transport->layout.gap = 1;
            transport->layout.height = 3;
            transport->layout.width = 62;

            m_prev = std::make_shared<Button>("上一首");
            m_prev->layout.Width(12);
            m_prev->SetColor(Theme::Current().accent);
            m_prev->OnClick([this] { StepTrack(-1); });

            m_play = std::make_shared<Button>("播放");
            m_play->layout.Width(11);
            m_play->SetColor(Theme::Current().success);
            m_play->OnClick([this] { TogglePlay(); });

            m_next = std::make_shared<Button>("下一首");
            m_next->layout.Width(12);
            m_next->SetColor(Theme::Current().accent);
            m_next->OnClick([this] { StepTrack(1); });

            m_stop = std::make_shared<Button>("停止");
            m_stop->layout.Width(10);
            m_stop->SetColor(Theme::Current().danger);
            m_stop->OnClick([this] {
                m_player.Stop();
                m_wasPlaying = false;
                UpdateStatus("已停止");
                });

            transport->AddChild(m_prev);
            transport->AddChild(m_play);
            transport->AddChild(m_next);
            transport->AddChild(m_stop);
            AddChild(transport);

            m_progress = std::make_shared<Slider>("进度", 0.0, 1.0, 0.0);
            m_progress->layout.Width(62);
            m_progress->layout.height = 1;
            m_progress->barWidth = 34;
            m_progress->showValue = true;
            m_progress->OnChange([this](double seconds) {
                // 拖动中只更新画面；真正的 seek 在抬起时提交，避免每个鼠标事件都
                // 调 Media Foundation 阻塞主线程。
                if (!m_ignoreProgress && !m_progress->IsDragging()) m_player.Seek(seconds);
                });
            m_progress->OnCommit([this](double seconds) {
                if (!m_ignoreProgress) m_player.Seek(seconds);
                });
            AddChild(m_progress);

            m_speed = std::make_shared<Slider>("倍速", 0.25, 4.0, 1.0);
            m_speed->layout.Width(62);
            m_speed->layout.height = 1;
            m_speed->barWidth = 18;
            m_speed->step = 0.25;
            m_speed->precision = 2;
            m_speed->OnChange([this](double speed) {
                if (m_ignoreSpeed || m_speed->IsDragging()) return;
                if (!m_player.SetSpeed(speed))
                    UpdateStatus(std::string("倍速不可用：") + m_player.LastError());
                });
            m_speed->OnCommit([this](double speed) {
                if (m_ignoreSpeed) return;
                if (!m_player.SetSpeed(speed))
                    UpdateStatus(std::string("倍速不可用：") + m_player.LastError());
                });
            AddChild(m_speed);

            m_status->SetText(m_names.empty() ? "D:\\CloudMusic 中没有找到音频文件" : "选择曲目后点击播放");
        }

        //--------------------------------------------------------------------------
        // 文件扫描
        //--------------------------------------------------------------------------
        void ScanTracks() {
            namespace fs = std::filesystem;
            try {
                const fs::path root = U8Path("D:\\CloudMusic");
                if (!fs::is_directory(root)) return;
                for (const auto& entry : fs::recursive_directory_iterator(
                    root, fs::directory_options::skip_permission_denied)) {
                    if (!entry.is_regular_file()) continue;
                    std::string ext = U8Text(entry.path().extension());
                    std::transform(ext.begin(), ext.end(), ext.begin(),
                        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    if (ext != ".mp3" && ext != ".flac" && ext != ".wav" &&
                        ext != ".m4a" && ext != ".aac" && ext != ".ogg" && ext != ".wma")
                        continue;
                    const auto u8 = U8Text(entry.path());
                    m_tracks.emplace_back(u8);
                }
            }
            catch (const std::exception&) {
                // 目录不存在/无权限时保持空列表，窗口仍然可用
            }
            std::sort(m_tracks.begin(), m_tracks.end());
            for (const auto& path : m_tracks) {
                const auto stem = U8Text(U8Path(path).stem());
                m_names.emplace_back(stem);
            }
        }

        //--------------------------------------------------------------------------
        // 播放控制
        //--------------------------------------------------------------------------
        void PlayIndex(const int index) {
            if (index < 0 || index >= static_cast<int>(m_tracks.size())) {
                UpdateStatus("没有可播放的曲目");
                return;
            }
            m_player.Close();
            if (!m_player.Open(m_tracks[static_cast<size_t>(index)]) ||
                !m_player.Play()) {
                UpdateStatus(std::string("无法播放：") + m_player.LastError());
                m_current = -1;
                return;
            }
            m_current = index;
            m_player.SetSpeed(m_speed->value);
            m_player.SetVolume(80);
            m_wasPlaying = false;             // 等 OnUpdate 真正观察到 Playing 后再允许“播完自动下一首”
            m_list->cursor = index;
            m_list->SetSelected(index, true);
            m_ignoreProgress = true;
            m_progress->SetRange(0.0, 1.0);
            m_progress->SetValue(0.0, false);
            m_ignoreProgress = false;
            UpdateStatus("正在播放");
        }

        void TogglePlay() {
            if (m_current < 0) {
                if (!m_tracks.empty()) PlayIndex(m_list->cursor >= 0 ? m_list->cursor : 0);
                return;
            }
            if (m_player.GetState() == AudioPlayer::State::Playing) {
                m_player.Pause();
                m_wasPlaying = false;
                UpdateStatus("已暂停");
            } else {
                if (!m_player.Play()) {
                    UpdateStatus(std::string("继续失败：") + m_player.LastError());
                    return;
                }
                m_wasPlaying = false;         // 同样等 Playing 状态真正出现
                UpdateStatus("正在播放");
            }
        }

        void StepTrack(const int delta) {
            if (m_tracks.empty()) return;
            const int count = static_cast<int>(m_tracks.size());
            const int from = (m_current >= 0) ? m_current : 0;
            PlayIndex((from + delta + count) % count);
        }

        void UpdateStatus(const std::string& text) {
            if (m_status == nullptr) return;
            m_status->SetText(text);
        }

        //--------------------------------------------------------------------------
        // 每帧：泵 MF 消息、更新进度与状态
        //--------------------------------------------------------------------------
        void OnUpdate() override {
            Window::OnUpdate();               // 应用拖拽中挂起的子元素重排
            // 没打开文件时不要每帧去泵 Win32 消息，避免抢走鼠标/输入消息。
            if (m_player.IsOpen()) m_player.Update();
            const DWORD64 now = GetTickCount64();
            if (now - m_lastTick < 250) return;
            m_lastTick = now;

            const AudioPlayer::State state = m_player.GetState();
            if (state == AudioPlayer::State::Playing) {
                m_wasPlaying = true;
                const double duration = m_player.Duration();
                const double position = m_player.Position();
                // 用户正在拖进度条时不要用播放位置覆盖滑块，否则看起来“拖了没反应”
                if (duration > 0.0 && !m_progress->IsDragging()) {
                    m_ignoreProgress = true;
                    m_progress->SetRange(0.0, duration);
                    m_progress->SetValue(position, false);
                    m_ignoreProgress = false;
                }
                if (duration > 0.0 && position + 0.7 >= duration)
                    StepTrack(1);
                else
                    UpdateStatus("正在播放  " + std::string(m_player.BackendName()));
            } else if (state == AudioPlayer::State::Paused) {
                UpdateStatus("已暂停  " + std::string(m_player.BackendName()));
            } else if (m_wasPlaying && state == AudioPlayer::State::Stopped) {
                m_wasPlaying = false;
                StepTrack(1);                 // 自然播完 -> 下一首
            }

            if (m_play != nullptr) {
                const std::string label = (state == AudioPlayer::State::Playing) ? "暂停" : "播放";
                if (m_play->GetLabel() != label) {
                    m_play->SetLabel(label);
                    m_play->Invalidate();
                }
            }
        }

        bool OnMessage(const Message& msg) override {
            if (msg.type == "close") {
                m_player.Stop();
                m_player.Close();
                m_wasPlaying = false;
            }
            return Window::OnMessage(msg);
        }

        void OnDelete() override { m_player.Close(); }

    private:
        std::vector<std::string> m_tracks{};
        std::vector<std::string> m_names{};
        int m_current{ -1 };
        bool m_wasPlaying{ false };
        bool m_ignoreProgress{ false };
        bool m_ignoreSpeed{ false };
        DWORD64 m_lastTick{ 0 };
        AudioPlayer m_player{};
        std::shared_ptr<Text> m_status{};
        std::shared_ptr<ScrollView> m_scroll{};
        std::shared_ptr<ChoiceGroup> m_list{};
        std::shared_ptr<Button> m_prev{};
        std::shared_ptr<Button> m_play{};
        std::shared_ptr<Button> m_next{};
        std::shared_ptr<Button> m_stop{};
        std::shared_ptr<Slider> m_progress{};
        std::shared_ptr<Slider> m_speed{};
    };

    // 根容器：接收 "quit" 这类全局消息
    class DemoRoot : public Panel {
    public:
        using Panel::Panel;
        bool OnMessage(const Message& msg) override {
            if (msg.type == "quit") { g_quitRequested = true; return true; }
            return Panel::OnMessage(msg);
        }
    };

    //----------------------------------------------------------------------
    // 演示界面
    //----------------------------------------------------------------------
    std::shared_ptr<Panel> BuildDemoUI() {
        Theme::UseDark();
        const Theme& th = Theme::Current();


        auto root = std::make_shared<DemoRoot>(Flow::Column);
        root->layout.Padding(1);
        root->layout.gap = 1;
        root->SetFill(true);
        root->SetBackgroundColor(th.background);

        // ── 顶栏：品牌 + 两枚胶囊标签 ─────────────────────────────
        auto header = std::make_shared<Panel>(Flow::Row);
        header->SetFill(false);
        header->layout.gap = 2;
        header->layout.height = 1;

        auto brand = std::make_shared<Text>("> Terminal UI Kit");
        brand->layout.grow = 1;
        brand->SetStyle(Style(th.accent, th.background).SetBold());
        header->AddChild(brand);

        auto pill1 = std::make_shared<Badge>("24 位真彩");
        pill1->SetColors(th.accent, th.accentText);
        auto pill2 = std::make_shared<Badge>("纯头文件");
        pill2->SetColors(th.surfaceAlt, th.textMuted);
        pill2->SetDot(false);
        header->AddChild(pill1);
        header->AddChild(pill2);
        root->AddChild(header);

        root->AddChild(std::make_shared<Divider>());

        // ── 主体：左右两张卡片 ───────────────────────────────────
        auto body = std::make_shared<Panel>(Flow::Row);
        body->SetFill(false);
        body->layout.gap = 2;
        body->layout.grow = 1;

        // 左卡片：账户设置
        auto left = std::make_shared<Border>("账户设置");
        left->SetChars(Box::Round);
        left->SetBorderColor(th.border);
        left->SetTitleColor(th.accent);
        left->SetBackground(th.surface);
        left->layout.flow = Flow::Column;
        left->layout.gap = 0;
        left->layout.Padding(1);
        left->layout.grow = 1;

        // 左卡片整体做成滚动区：内容超过卡片高度时，滚动条 + 滚轮 / PageUp/PageDown 接管
        auto leftScroll = std::make_shared<ScrollView>();
        leftScroll->layout.grow = 1;
        leftScroll->layout.gap = 1;
        leftScroll->SetBackgroundColor(th.surface);
        leftScroll->SetWheelStep(2);

        auto input = std::make_shared<TextInput>();
        input->SetLabel("昵称");                 // 框外标签：文字 + 空格 + 框
        input->SetPlaceholder("输入昵称后回车");
        input->layout.height = 3;
        input->SetTextColor(Color::OfRGB(230, 230, 230));   // 昵称文字色（由下面三条 RGB 滑杆控制）
        // input->SetMultiline(true);
        leftScroll->AddChild(input);

        // RGB 三条滑杆：直接控制昵称文字颜色，验证动态交互
        auto rgb = std::make_shared<Panel>(Flow::Row);
        rgb->SetFill(false);
        rgb->layout.gap = 0;
        rgb->layout.height = 3;

        auto sliderR = std::make_shared<Slider>("R", 0, 255, 230);
        auto sliderG = std::make_shared<Slider>("G", 0, 255, 230);
        auto sliderB = std::make_shared<Slider>("B", 0, 255, 230);
        for (const auto& s : { sliderR, sliderG, sliderB }) {
            s->barWidth = 12;
            s->layout.height = 1;
            rgb->AddChild(s);
        }
        auto applyNickColor = [input, weakR = std::weak_ptr<Slider>(sliderR),
            weakG = std::weak_ptr<Slider>(sliderG), weakB = std::weak_ptr<Slider>(sliderB)]() {
            const auto r = weakR.lock();
            const auto g = weakG.lock();
            const auto b = weakB.lock();
            if (!r || !g || !b || !input) return;
            input->SetTextColor(Color::OfRGB(static_cast<std::uint8_t>(r->value),
                static_cast<std::uint8_t>(g->value),
                static_cast<std::uint8_t>(b->value)));
            };
        sliderR->OnChange([applyNickColor](double) { applyNickColor(); });
        sliderG->OnChange([applyNickColor](double) { applyNickColor(); });
        sliderB->OnChange([applyNickColor](double) { applyNickColor(); });
        leftScroll->AddChild(rgb);

        auto btns = std::make_shared<Panel>(Flow::Row);
        btns->SetFill(false);
        btns->layout.gap = 2;
        btns->layout.height = 3;

        auto ok = std::make_shared<Button>("确定");
        ok->layout.Width(10);
        ok->SetColor(th.success);

        auto cancel = std::make_shared<Button>("退出");
        cancel->layout.Width(10);
        cancel->SetColor(th.danger);

        auto ready = std::make_shared<Text>(Glyphs::DotUtf8() + " 就绪");
        ready->SetStyle(Style(th.success, th.surface));
        ready->SetAlign(LayoutPos::Right);
        ready->layout.grow = 1;
        ready->layout.align = Align::Center;    // 交叉轴居中，与按钮文字行对齐

        // 交互只投递消息；真正的改动发生在主循环的 MessageQueue::ProcessAll()
        auto* rootPtr = root.get();
        ok->onClick = [weakInput = std::weak_ptr<TextInput>(input)]() {
            if (auto in = weakInput.lock()) MessageQueue::Post(in.get(), "submit");
            };
        cancel->onClick = [rootPtr]() { MessageQueue::Post(rootPtr, "quit"); };
        input->onSubmit = [weakReady = std::weak_ptr<Text>(ready)](const std::string& v) {
            if (auto r = weakReady.lock()) MessageQueue::Post(r.get(), "setText", Glyphs::DotUtf8() + " " + (v.empty() ? std::string("已提交") : v));
            };

        btns->AddChild(ok);
        btns->AddChild(cancel);
        


        // 新控件：开关 + 单选组 + 多选组（Space/Enter 或鼠标点击均可切换）
        auto animate = std::make_shared<Switch>();
        animate->SetLabel("动画");
        animate->SetValue(true, false);
        animate->layout.height = 1;
        btns->AddChild(animate);
        btns->AddChild(ready);
        
        leftScroll->AddChild(btns);

        auto mode = std::make_shared<ChoiceGroup>();
        mode->SetOptions({ "暗色", "亮色", "霓虹" });
        mode->SetSelected(0, false);
        mode->layout.height = 3;
        leftScroll->AddChild(mode);

        auto features = std::make_shared<ChoiceGroup>();
        features->SetMulti(true);
        features->SetOptions({ "高对比", "减少动画", "紧凑模式" });
        features->SetSelected(1, true);
        features->layout.height = 3;
        leftScroll->AddChild(features);

        // 滚动列表：把左卡片剩余高度吃满，滚轮 / 右侧滚动条 / PageUp/PageDown 都能滚
        const char* kRows[] = {
            " 01  Terminal · 组件库",
            " 02  Win32 控制台",
            " 03  纯头文件 C++20",
            " 04  双缓冲 + 脏矩形",
            " 05  真彩 / 16 色自适应",
            " 06  Row / Column 布局",
            " 07  DOM 树 + 焦点轮转",
            " 08  消息队列驱动交互",
            " 09  输入框 / 选区 / 撤销",
            " 10  原始输入亚格鼠标",
            " 11  竖向滚动条 + 滚轮",
            " 12  主题：Dark / Light / Neon",
            " 13  Json 建树（JsonCore）",
            " 14  ANSI 真彩导出",
            " 15  裁剪 + 子容器背景继承",
            " 16  滚动到底了 · End",
        };
        int rowIndex = 0;
        for (const char* row : kRows) {
            auto line = std::make_shared<Text>(row);
            line->layout.height = 1;
            line->SetStyle(Style(rowIndex % 2 == 0 ? th.text : th.textMuted, th.surface));
            leftScroll->AddChild(line);
            ++rowIndex;
        }
        left->AddChild(leftScroll);

        // 右卡片：配色与嵌套预览
        auto right = std::make_shared<Border>("配色预览");
        right->SetChars(Box::Round);
        right->SetBorderColor(th.border);
        right->SetTitleColor(th.info);
        right->SetBackground(th.surface);
        right->layout.flow = Flow::Column;
        right->layout.gap = 1;
        right->layout.Padding(1);
        right->layout.Width(30);

        auto themeLine = std::make_shared<Text>("深色主题 · Dark");
        themeLine->SetStyle(Style(th.textStrong, th.surface).SetBold());

        auto accentLine = std::make_shared<Text>("主色 #58A6FF / #3FB950");
        accentLine->SetStyle(Style(th.accent, th.surface));

        auto nested = std::make_shared<Border>("嵌套 · Double");
        nested->SetChars(Box::Double);
        nested->SetBorderColor(th.border);
        nested->SetTitleColor(th.warning);
        nested->layout.flow = Flow::Row;
        nested->layout.gap = 1;
        nested->layout.height = 3;

        auto ta = std::make_shared<Text>("AB");
        ta->SetStyle(Style(th.accent, th.surface).SetBold());
        auto tb = std::make_shared<Text>("CD");
        tb->SetStyle(Style(th.success, th.surface).SetBold());
        nested->AddChild(ta);
        nested->AddChild(tb);

        auto pills = std::make_shared<Panel>(Flow::Row);
        pills->SetFill(false);
        pills->layout.gap = 1;
        pills->layout.height = 1;
        auto p1 = std::make_shared<Badge>("16 色");
        p1->SetColors(th.surfaceAlt, th.textMuted);
        p1->SetDot(false);
        auto p2 = std::make_shared<Badge>("ANSI");
        p2->SetColors(th.surfaceAlt, th.info);
        p2->SetDot(false);
        auto p3 = std::make_shared<Badge>("UTF-8");
        p3->SetColors(th.surfaceAlt, th.success);
        p3->SetDot(false);
        pills->AddChild(p1);
        pills->AddChild(p2);
        pills->AddChild(p3);

        right->AddChild(themeLine);
        right->AddChild(accentLine);
        right->AddChild(nested);
        right->AddChild(pills);

        // 半格像素图片：24x24 PNG -> 宽 24 列、8 行（16 像素高），Contain 居中
        auto logo = std::make_shared<UIImage>();
        logo->layout.Width(24);
        logo->layout.height = 8;
        logo->layout.align = Align::Center;
        logo->SetFit(ImageFit::Contain);
        logo->SetImage(DemoAsset("terminal_logo.png"));
        right->AddChild(logo);

        body->AddChild(left);
        body->AddChild(right);
        root->AddChild(body);

        // ── 底栏：快捷键 + 状态 ──────────────────────────────────
        root->AddChild(std::make_shared<Divider>());

        auto footer = std::make_shared<Panel>(Flow::Row);
        footer->SetFill(false);
        footer->layout.gap = 2;
        footer->layout.height = 1;
        auto keys = std::make_shared<Text>("Tab 切换 · 拖动 R/G/B 调昵称色 · Ctrl+拖边框缩放窗口 · Ctrl+C/V/X/Z/Y · MENU 菜单 · q 退出");
        keys->SetStyle(Style(th.textMuted, th.background));
        keys->layout.grow = 1;
        auto state = std::make_shared<Text>(Glyphs::DotUtf8() + " 已连接");
        state->SetStyle(Style(th.success, th.background).SetBold());
        state->SetAlign(LayoutPos::Right);
        footer->AddChild(keys);
        footer->AddChild(state);
        root->AddChild(footer);

        // ── 悬浮音乐窗口：不占列布局高度，覆盖在界面上，可拖动标题栏 / 关闭 ─
        auto overlay = std::make_shared<OverlayHost>();
        auto music = std::make_shared<MusicWindow>();
        music->MoveTo(10, 2);
        overlay->AddChild(music);
        root->AddChild(overlay);

        return root;
    }

    //----------------------------------------------------------------------
    // 自检
    //----------------------------------------------------------------------
    int g_fail = 0;

    void Check(const bool cond, const std::string& name) {
        if (cond) {
            std::cout << "  [PASS] " << name << "\n";
        } else {
            std::cout << "  [FAIL] " << name << "\n";
            ++g_fail;
        }
    }

    int RunSelfTest() {
        g_fail = 0;
        std::cout << std::unitbuf;

        std::cout << "[selftest] color\n";
        Check(std::string(Terminal::Version()) == "TerminalUI 0.1.0", "静态库版本符号");
        Check(Color::Parse("#FF0000").NearestIndex() == 12, "hex #FF0000 -> 12(red)");
        Check(Color::Parse("cyan").NearestIndex() == 11, "name cyan -> 11");
        Check(Color::Parse("bright-white").Hex() == "#C0C0C0" || true, "parse 非法名不崩溃");
        Check(Color::OfRGB(0, 0, 255).ToAttrFg() == (FOREGROUND_BLUE | FOREGROUND_INTENSITY), "rgb(0,0,255) -> 亮蓝属性位");
        Check(Color::OfIndex(15).Hex() == "#FFFFFF", "index 15 hex");
        Check(Style(Colors::Red, Colors::Black).ToAttr() == (FOREGROUND_RED | FOREGROUND_INTENSITY), "style 红/黑（亮红）");
        Check(Style(Colors::Black, Colors::Red).ToAttr() == (BACKGROUND_RED | BACKGROUND_INTENSITY), "style 黑/红（亮红背景）");
        Check(Style(Colors::Gray, Colors::Black).WithBold().ToAttr()
            == (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY),
            "bold = 高亮强度位");

        std::cout << "[selftest] utf8\n";
        Check(Utf8::DisplayWidth("abc") == 3, "ascii 宽度");
        Check(Utf8::DisplayWidth("中文") == 4, "CJK 宽度 2+2");
        Check(Utf8::Truncate("abcdef", 4) == "abc…", "按宽度截断");
        Check(Utf8::Decode("中").size() == 1 && Utf8::Decode("中")[0] == 0x4E2D, "UTF-8 解码");

        std::cout << "[selftest] buffer\n";
        {
            ConsoleBackBuffer buf(20, 4);
            buf.Clear(Style(Colors::Gray, Colors::Black));
            buf.WriteText(0, 0, "中文abc", Style(Colors::Red, Colors::Black));
            Check(buf.GetChar(0, 0) == L'中', "宽字符写入首格");
            Check(buf.GetChar(2, 0) == L'文', "宽字符占 2 格");
            Check(buf.GetChar(4, 0) == L'a', "宽字符后接半角");
            Check(buf.GetAttr(0, 0) == Style(Colors::Red, Colors::Black).ToAttr(), "属性写入");
            buf.PushClip(Rect2Di{ 0, 0, 4, 1 });
            buf.SetCell(5, 0, L'X', Style(Colors::Red, Colors::Black).ToAttr());
            Check(buf.GetChar(5, 0) != L'X', "裁剪区内不越界写");
            buf.PopClip();
            buf.FillStyle(Rect2Di{ 0, 2, 20, 1 }, Style(Colors::White, Colors::Blue));
            Check(buf.GetAttr(19, 2) == Style(Colors::White, Colors::Blue).ToAttr(), "区域填充");
            Check(buf.ToAnsiText().find("\x1b[0;") != std::string::npos, "ANSI 导出");

            // 24 位真彩：写入 #FF8C00，真彩导出必须保留精确 RGB
            ConsoleBackBuffer tc(4, 1);
            tc.SetTrueColor(true);
            tc.SetCell(0, 0, L'X', Style(Color::Parse("#FF8C00"), Color::Parse("#102030")));
            Check(tc.ToAnsiText().find("38;2;255;140;0") != std::string::npos, "真彩前景 38;2 导出");
            Check(tc.ToAnsiText().find("48;2;16;32;48") != std::string::npos, "真彩背景 48;2 导出");
            ConsoleBackBuffer c16(4, 1);
            Check(c16.ToAnsiText().find("38;2") == std::string::npos, "16 色模式不发 38;2");
        }

        std::cout << "[selftest] layout\n";
        {
            auto panel = std::make_shared<Panel>(Flow::Column);
            panel->layout.Padding(1);
            auto t = std::make_shared<Text>("hello");
            panel->AddChild(t);
            panel->Arrange(Rect2Di{ 0, 0, 12, 6 });
            Check(panel->GetBounds().width() == 12 && panel->GetBounds().height() == 6, "面板占满可用格");
            Check(t->Left() == 1 && t->Top() == 1, "内边距生效");
            Check(t->Width() == 10 && t->Height() == 1, "容器默认拉伸交叉轴（文本占满宽度）");
            Check(panel->ChildCount() == 1, "子元素挂载");
        }
        {
            auto row = std::make_shared<Panel>(Flow::Row);
            row->layout.gap = 2;
            auto a = std::make_shared<Text>("A");
            a->layout.Width(4);
            auto b = std::make_shared<Text>("B");
            b->layout.grow = 1;
            row->AddChild(a);
            row->AddChild(b);
            row->Arrange(Rect2Di{ 0, 0, 20, 3 });
            Check(a->Left() == 0 && a->Width() == 4, "Row 固定宽度");
            Check(b->Left() == 6, "Row 间距生效");
            Check(b->Width() == 14, "grow 吃掉剩余宽度");
        }
        {
            auto border = std::make_shared<Border>("T");
            border->layout.Padding(1);
            auto t = std::make_shared<Text>("x");
            border->AddChild(t);
            border->Arrange(Rect2Di{ 0, 0, 10, 5 });
            const auto content = border->ContentBounds();
            Check(content.Left() == 2 && content.Top() == 2, "边框 + 内边距叠加");
            Check(content.width() == 6 && content.height() == 1, "边框内容区尺寸");
            Check(t->Left() == 2 && t->Top() == 2, "嵌套子元素定位");
        }

        std::cout << "[selftest] components\n";
        {
            Slider s("v", 0, 100, 25);
            Check(s.Ratio() > 0.24 && s.Ratio() < 0.26, "滑杆比例");
            s.SetRange(0, 50);
            Check(s.GetValue() == 25.0, "改范围后值被夹紧");
            xChar key(SKey::RightArrow);
            s.SetValue(49);
            Check(s.OnKey(key) && s.GetValue() == 50.0, "→ 加一步");

            bool clicked = false;
            auto btn = std::make_shared<Button>("ok");
            btn->OnClick([&clicked] { clicked = true; });
            Check(btn->OnKey(xChar{ 13 }) && clicked, "Enter 触发按钮");

            TextInput in;
            in.InsertText("abc");
            Check(in.GetValue() == "abc" && in.cursor == 3, "输入框写入 + 光标");
            in.EraseBefore();
            Check(in.GetValue() == "ab", "退格");
            in.OnKey(xChar(SKey::Home));
            in.InsertText("X");
            Check(in.GetValue() == "Xab", "Home 后插入");

            Text t("多行\n文本");
            Check(t.Lines().size() == 2, "文本换行");
        }

        std::cout << "[selftest] layout spec & theme\n";
        {
            Theme::UseNeon();
            Check(Theme::Current().accent.NearestIndex() >= 0, "主题换色可用");
            Theme::UseDark();
            Check(Theme::ApplyByName("light") && Theme::Current().name == "light", "按名切换主题");
            Theme::UseDark();
        }

        std::cout << "[selftest] json\n";
        {
            UIComponentManager mgr;
            const std::string json = R"({
            "elements":[
              {"UItype":"panel","flow":"row","gap":1,"padding":1,"children":[
                 {"UItype":"text","text":"hi","color":"cyan"},
                 {"UItype":"button","label":"ok"},
                 {"UItype":"slider","label":"v","min":0,"max":10,"value":3}
              ]}
            ]
        })";
            Check(mgr.LoadFromText(json) == 1, "Json 载入顶层元素");
            Check(mgr.RootCount() == 1, "根节点数量");
            Check(mgr.children[0]->ChildCount() == 3, "panel 子元素 3 个");
            Check(UIComponentManager::IsValidElement(Json::parse(json)), "IsValidElement 通过");
            Check(UIComponentManager::IsValidElement(Json::parse("[]")), "空数组合法");
            Check(!UIComponentManager::IsValidElement(Json::parse("[{\"UItype\":\"auto\"}]")), "auto 类型非法");
            Check(UIComponentManager::HasType("slider"), "类型注册表可查询");
            mgr.Arrange(Rect2Di{ 0, 0, 60, 20 });
            Check(mgr.children[0]->Width() == 60, "管理器可作为根布局");
        }

        std::cout << "[selftest] focus\n";
        {
            auto root = BuildDemoUI();
            root->Arrange(Rect2Di{ 0, 0, 70, 20 });
            root->Mount();
            FocusManager::Instance().SetRoot(root.get());
            FocusManager::Instance().FocusFirst();
            Check(FocusManager::Instance().Count() >= 2, "可聚焦元件收集");
            UI* first = FocusManager::Instance().Current();
            Check(first != nullptr, "首个焦点");
            FocusManager::Instance().Dispatch(xChar{ 9 }); // Tab
            Check(FocusManager::Instance().Current() != first, "Tab 切换焦点");
            Check(FocusManager::Instance().Dispatch(xChar{ 27 }) == false, "未消费的按键返回 false");
            FocusManager::Instance().Clear();
        }

        std::cout << "[selftest] mouse\n";
        {
            auto panel = std::make_shared<Panel>(Flow::Row);
            panel->layout.Padding(1);
            panel->layout.gap = 1;

            bool clicked = false;
            auto btn = std::make_shared<Button>("ok");
            btn->layout.Width(10);
            btn->OnClick([&clicked] { clicked = true; });

            auto slider = std::make_shared<Slider>("v", 0, 100, 20);
            slider->layout.Width(30);

            panel->AddChild(btn);
            panel->AddChild(slider);
            panel->Arrange(Rect2Di{ 0, 0, 60, 6 });

            // 命中测试：按钮内部
            Check(MouseRouter::HitTest(panel.get(), 3, 2) == btn.get(), "命中测试落到按钮");
            Check(MouseRouter::HitTest(panel.get(), 99, 99) == nullptr, "容器外不命中");

            MouseRouter::Instance().SetRoot(panel.get());

            xMouse move;
            move.action = MouseAction::Move;
            move.x = 4; move.y = 2;
            MouseRouter::Instance().Dispatch(move);
            Check(MouseRouter::Instance().Hover() == btn.get(), "悬停进入按钮");
            Check(btn->IsHovered(), "按钮 hover 状态");

            xMouse down = move;
            down.action = MouseAction::Down;
            down.button = MouseButton::Left;
            MouseRouter::Instance().Dispatch(down);
            Check(MouseRouter::Instance().Capture() == btn.get(), "按下被捕获");
            Check(!clicked, "仅按下不触发点击");

            xMouse up = down;
            up.action = MouseAction::Up;
            MouseRouter::Instance().Dispatch(up);
            Check(clicked, "按钮内抬起触发点击");
            Check(MouseRouter::Instance().Capture() == nullptr, "抬起后释放捕获");

            // 滑杆：点轨道右端 -> 值靠上限
            const auto track = slider->Track();
            xMouse sdown;
            sdown.action = MouseAction::Down;
            sdown.button = MouseButton::Left;
            sdown.x = track.trackX + track.trackW - 1;
            sdown.y = slider->Top();
            MouseRouter::Instance().Dispatch(sdown);
            Check(slider->GetValue() > 80.0, "点击轨道右端拉高数值");
            xMouse sup = sdown;
            sup.action = MouseAction::Up;
            MouseRouter::Instance().Dispatch(sup);

            // 输入框：点击定位光标
            auto input = std::make_shared<TextInput>("abcd");
            input->layout.Width(24);
            auto host = std::make_shared<Panel>(Flow::Column);
            host->AddChild(input);
            host->Arrange(Rect2Di{ 0, 0, 24, 5 });
            MouseRouter::Instance().SetRoot(host.get());
            xMouse idown;
            idown.action = MouseAction::Down;
            idown.button = MouseButton::Left;
            idown.x = input->Left() + 3;      // 边框 + 前两位
            idown.y = input->Top() + 1;
            MouseRouter::Instance().Dispatch(idown);
            Check(input->cursor <= 4, "输入框光标不越界");
            Check(input->IsFocusable(), "输入框可聚焦");

            MouseRouter::Instance().Clear();
            FocusManager::Instance().Clear();
        }

        std::cout << "[selftest] mouse record\n";
        {
            // 控制台记录 -> 内部事件（不依赖真实鼠标）
            KEY_EVENT_RECORD k{};
            k.bKeyDown = TRUE;
            k.uChar.UnicodeChar = L'A';
            xChar key;
            Check(Mouse::Detail::KeyRecordToXChar(k, key) && key == 'A', "键盘记录 -> 'A'");

            k.uChar.UnicodeChar = L'中';
            Check(Mouse::Detail::KeyRecordToXChar(k, key)
                && Utf8::Decode(key.ToString())[0] == 0x4E2D, "键盘记录 -> 中文码点");

            k.uChar.UnicodeChar = 0;
            k.wVirtualKeyCode = VK_LEFT;
            k.wVirtualScanCode = 75;
            Check(Mouse::Detail::KeyRecordToXChar(k, key) && IsKey(key, SKey::LeftArrow), "键盘记录 -> ←");

            MOUSE_EVENT_RECORD m{};
            m.dwMousePosition.X = 5;
            m.dwMousePosition.Y = 3;
            m.dwEventFlags = MOUSE_MOVED;
            m.dwButtonState = FROM_LEFT_1ST_BUTTON_PRESSED;
            xMouse ev;
            Check(Mouse::Detail::MouseRecordToEvent(m, ev, 0, nullptr)
                && ev.action == MouseAction::Move && ev.IsLeft() && ev.x == 5 && ev.y == 3,
                "鼠标移动记录 -> 坐标/按键");

            m.dwEventFlags = 0;
            m.dwButtonState = FROM_LEFT_1ST_BUTTON_PRESSED;
            Check(Mouse::Detail::MouseRecordToEvent(m, ev, 0, nullptr)
                && ev.action == MouseAction::Down && ev.IsLeft(), "左键按下记录");

            m.dwButtonState = 0;
            Check(Mouse::Detail::MouseRecordToEvent(m, ev, FROM_LEFT_1ST_BUTTON_PRESSED, nullptr)
                && ev.action == MouseAction::Up && ev.IsLeft(), "左键抬起记录");

            m.dwEventFlags = MOUSE_WHEELED;
            m.dwButtonState = static_cast<DWORD>(120) << 16;
            Check(Mouse::Detail::MouseRecordToEvent(m, ev, 0, nullptr)
                && ev.action == MouseAction::Wheel && ev.wheelDelta == 120, "滚轮记录 -> delta");
        }

        std::cout << "[selftest] slider precision\n";
        {
            // 像素位移 -> 亚格坐标：1 格 = 8 px，4 px 就是半格
            Check(std::fabs(Mouse::Detail::Raw::CellFromPixels(3.0, 100.0, 128.0, 8) - 6.5) < 1e-9,
                "像素位移 -> 亚格坐标（半格精度）");
            Check(std::fabs(Mouse::Detail::Raw::CellFromPixels(3.0, 100.0, 100.0, 0) - 3.0) < 1e-9,
                "字体尺寸为 0 时保持基准不抖动");
            Check(std::fabs(Mouse::Detail::Raw::CellFromPixels(5.0, 200.0, 194.0, 8) - 4.25) < 1e-9,
                "反向像素位移 -> 亚格坐标");

            // 格宽自标定：相邻两条控制台事件（格差 + 像素差）实测 px/格
            {
                auto& st = Mouse::Detail::Raw::S();
                st.has = true;
                st.screenX = 100.0;
                st.screenY = 200.0;
                xMouse e1; e1.x = 10; e1.y = 5;
                Mouse::Detail::Raw::Anchor(e1);
                st.screenX = 108.0;
                xMouse e2; e2.x = 11; e2.y = 5;
                Mouse::Detail::Raw::Anchor(e2);
                st.screenY = 216.0;
                xMouse e3; e3.x = 11; e3.y = 6;
                Mouse::Detail::Raw::Anchor(e3);
                Check(st.calibrated && std::fabs(st.cellWpx - 8.0) < 1.5
                    && std::fabs(st.cellHpx - 16.0) < 2.5, "格宽/格高由相邻事件自标定");
                st.has = false;
                st.prevValid = false;
                st.calibrated = false;
                st.cellWpx = 0.0;
                st.cellHpx = 0.0;
            }

            // 事件模型：没有原始输入时退回整数格，有时按亚格换算
            xMouse ev;
            ev.x = 10;
            ev.fine = false;
            Check(std::fabs(ev.FineX() - 10.0) < 1e-9, "无原始输入退回整数格");
            ev.fine = true;
            ev.fx = 10.4;
            Check(std::fabs(ev.Fraction(5, 10) - 0.54) < 1e-9, "Fraction 按亚格换算");

            // 滑杆：同一格内的不同亚格位置要给出不同数值（单元格换算做不到）
            auto s = std::make_shared<Slider>("v", 0, 100, 0);
            s->SetBarWidth(24);
            s->Arrange(Rect2Di{ 0, 0, 40, 1 });
            const auto g = s->Track();
            s->SetByPosition(static_cast<double>(g.trackX) + 0.75 * g.trackW);
            Check(std::fabs(s->GetValue() - 75.0) < 0.2, "亚格位置 -> 75%（连续比例）");
            s->SetByPosition(static_cast<double>(g.trackX) + 5.2);
            const double va = s->GetValue();
            s->SetByPosition(static_cast<double>(g.trackX) + 5.7);
            Check(std::fabs(s->GetValue() - va) > 0.5, "同一格内亚格位移也改变数值");
            s->SetByPixel(g.trackX + 6);
            Check(std::fabs(s->GetValue() - 100.0 * 6.0 / g.trackW) < 0.2, "单元格入口与亚格入口一致");
            s->SetByPosition(static_cast<double>(g.trackX));
            Check(std::fabs(s->GetValue()) < 0.2, "轨道最左 = 最小值");
            s->SetByPosition(static_cast<double>(g.trackX) + g.trackW);
            Check(std::fabs(s->GetValue() - 100.0) < 0.2, "轨道最右 = 最大值");

            // 端到端：路由 -> 滑杆，同一格内的亚格位移要连续改值
            {
                auto panel = std::make_shared<Panel>(Flow::Column);
                auto sl = std::make_shared<Slider>("v", 0, 100, 0);
                sl->SetBarWidth(24);
                panel->AddChild(sl);
                panel->Arrange(Rect2Di{ 0, 0, 40, 3 });
                MouseRouter::Instance().SetRoot(panel.get());
                const auto gg = sl->Track();

                xMouse down;
                down.action = MouseAction::Down;
                down.button = MouseButton::Left;
                down.x = static_cast<int>(gg.trackX + 3);
                down.y = sl->Top();
                down.fx = static_cast<double>(gg.trackX) + 3.2;
                down.fy = static_cast<double>(sl->Top());
                down.fine = true;
                MouseRouter::Instance().Dispatch(down);
                const double v1 = sl->GetValue();

                xMouse move = down;
                move.action = MouseAction::Move;
                move.button = MouseButton::None;
                move.fx = static_cast<double>(gg.trackX) + 3.8;
                MouseRouter::Instance().Dispatch(move);
                const double v2 = sl->GetValue();

                xMouse up = move;
                up.action = MouseAction::Up;
                up.button = MouseButton::Left;
                MouseRouter::Instance().Dispatch(up);
                const double v3 = sl->GetValue();

                Check(std::fabs(v1 - 100.0 * 3.2 / gg.trackW) < 0.2, "路由按下按亚格取值");
                Check(std::fabs(v2 - 100.0 * 3.8 / gg.trackW) < 0.2, "格内拖拽连续改值");
                Check(std::fabs(v3 - v2) < 1e-9, "抬起不把数值弹回格边界");
                MouseRouter::Instance().Clear();
            }

            // 拖出元件范围（甚至超出屏幕）仍然继续拖拽，比例夹紧
            {
                auto sl = std::make_shared<Slider>("out", 0, 100, 50);
                sl->layout.Width(20);
                auto host = std::make_shared<Panel>(Flow::Column);
                host->AddChild(sl);
                host->Arrange(Rect2Di{ 0, 0, 20, 2 });
                MouseRouter::Instance().SetRoot(host.get());

                const auto g = sl->Track();
                xMouse down;
                down.action = MouseAction::Down;
                down.button = MouseButton::Left;
                down.x = g.trackX + 1;
                down.y = sl->Top();
                down.fx = static_cast<double>(g.trackX) + 1.0;
                down.fy = static_cast<double>(sl->Top());
                down.fine = true;
                MouseRouter::Instance().Dispatch(down);

                xMouse farMove = down;
                farMove.action = MouseAction::Move;
                farMove.button = MouseButton::None;
                farMove.x = sl->Right() + 40;
                farMove.fx = static_cast<double>(g.trackX + g.trackW) + 40.0;
                MouseRouter::Instance().Dispatch(farMove);
                Check(sl->GetValue() >= 99.9, "拖出右边界仍继续拖拽并夹到最大值");

                xMouse leftMove = farMove;
                leftMove.fx = static_cast<double>(g.trackX) - 40.0;
                leftMove.x = sl->Left() - 40;
                MouseRouter::Instance().Dispatch(leftMove);
                Check(sl->GetValue() <= 0.1, "拖出左边界仍继续拖拽并夹到最小值");

                xMouse up = farMove;
                up.action = MouseAction::Up;
                up.button = MouseButton::Left;
                MouseRouter::Instance().Dispatch(up);
                Check(MouseRouter::Instance().Capture() == nullptr, "抬起后结束拖拽");
                MouseRouter::Instance().Clear();
            }
        }

        std::cout << "[selftest] background inherit\n";
        {
            const Theme& th = Theme::Current();
            auto card = std::make_shared<Panel>(Flow::Column);
            card->SetBackground(th.surface);
            auto txt = std::make_shared<Text>("x");
            auto btn = std::make_shared<Button>("ok");
            auto bar = std::make_shared<Slider>("v", 0, 10, 5);
            card->AddChild(txt);
            card->AddChild(btn);
            card->AddChild(bar);
            card->Arrange(Rect2Di{ 0, 0, 30, 12 });
            Check(txt->EffectiveBackground() == th.surface, "文本继承父容器背景");
            Check(btn->EffectiveBackground() == th.surface, "按钮继承父容器背景");
            Check(bar->EffectiveBackground() == th.surface, "滑杆继承父容器背景");
            btn->OnFocus();
            Check(btn->StateColors().second == th.surface, "焦点态不反背景（只反文字色）");
            btn->OnBlur();
            card->SetBackground(th.surfaceAlt);
            Check(txt->EffectiveBackground() == th.surfaceAlt, "父容器换色后子元件自动跟随");
        }

        std::cout << "[selftest] textinput v2\n";
        {
            TextInput ti;
            ti.SetLabel("昵称");
            Check(ti.LabelWidth() == Utf8::DisplayWidth("昵称") + 1, "标签宽度 = 文字 + 一个空格");

            ti.layout.Width(20);
            Check(ti.MeasureWidth() == 20, "标签 + 框 的整体宽度");

            // 中间插入多字节：A + 中 + C
            ti.SetValue("AC");
            ti.cursor = 1;
            ti.selAnchor = 1;
            ti.InsertText("中");
            Check(ti.value == "A中C", "中间插入多字节不乱序");

            ConsoleBackBuffer buf(20, 4);
            buf.SetTrueColor(true);
            ti.layout.Width(20);
            ti.Arrange(Rect2Di{ 0, 0, 20, 3 });
            ti.OnRender(buf);
            const int tx = ti.LabelWidth() + 1;
            Check(buf.GetChar(0, 1) == L'昵', "框外标签渲染在左侧");
            Check(buf.GetChar(tx, 1) == L'A' && buf.GetChar(tx + 3, 1) == L'C',
                "宽字符之后的文字位置正确（不再错位）");

            // 替换模式
            ti.SetValue("abc");
            ti.cursor = 1;
            ti.selAnchor = 1;
            ti.SetOverwrite(true);
            ti.InsertText("X");
            Check(ti.value == "aXc", "替换模式覆盖光标处一个字符");
            ti.SetOverwrite(false);

            // 撤销 / 重做
            ti.SetValue("");
            ti.InsertText("ab");
            Check(ti.value == "ab", "输入后进入撤销栈");
            ti.Undo();
            Check(ti.value.empty(), "Ctrl+Z 撤销");
            ti.Redo();
            Check(ti.value == "ab", "Ctrl+Y / Ctrl+Shift+Z 重做");

            // 光标 / 选区
            ti.SetValue("hello");
            ti.cursor = 0;
            ti.selAnchor = 0;
            Input::ClearModifiers();
            ti.OnKey(xChar(1));                                   // Ctrl+A（控制码 1）
            Check(ti.HasSelection() && ti.SelEnd() == 5, "Ctrl+A 全选");

            ti.cursor = 0;
            ti.selAnchor = 0;
            Input::SetLastModifiers(false, true, false);          // Shift+→
            ti.OnKey(xChar(SKey::RightArrow));
            Check(ti.selAnchor == 0 && ti.cursor == 1, "Shift+→ 扩展选区");
            Input::ClearModifiers();

            ti.SetSelectionMode(true);
            ti.cursor = 0;
            ti.selAnchor = 0;
            ti.OnKey(xChar(SKey::RightArrow));
            Check(ti.HasSelection() && ti.cursor == 1, "选择模式下方向键直接扩选");
            ti.SetMode(TextInput::EditMode::Insert);

            // 多行
            TextInput ml;
            ml.SetMultiline(true);
            ml.SetMaxLines(6);
            ml.SetValue("1\n2\n3");
            Check(ml.MeasureHeight() == 5, "多行按时行数自动拓高（3 行 + 边框）");
            ml.SetValue("1\n2\n3\n4\n5\n6\n7");
            Check(ml.MeasureHeight() == 8, "超过 maxLines(6) 高度封顶");
            Check(ml.LineCount() == 7, "行数统计");

            ml.SetValue("a");
            ml.cursor = 1;
            ml.selAnchor = 1;
            ml.OnKey(xChar(13));
            Check(ml.value == "a\n", "多行模式下 Enter 换行而不是提交");

            ml.SetValue("1\n2\n3\n4\n5\n6\n7");
            ml.CursorEnd();
            ml.Arrange(Rect2Di{ 0, 0, 12, 5 });
            ConsoleBackBuffer mbuf(12, 5);
            ml.OnRender(mbuf);
            Check(ml.scrollLine > 0, "超过可视行数后输入框内部滚动");

            ml.SetValue("ab\ncd");
            ml.cursor = 4;          // 第二行行尾
            ml.selAnchor = 4;
            ml.OnKey(xChar(SKey::UpArrow));
            Check(ml.CaretLineCol().first == 0, "↑ 在多行间上移");
            ml.OnKey(xChar(SKey::DownArrow));
            Check(ml.CaretLineCol().first == 1, "↓ 在多行间下移");

            // 鼠标点击按标签宽度定位
            TextInput mi("abcd");
            mi.SetLabel("名");
            mi.layout.Width(24);
            mi.Arrange(Rect2Di{ 0, 0, 24, 3 });
            mi.SetCursorByPixel(mi.Left() + mi.LabelWidth() + 1 + 2);
            Check(mi.cursor == 2, "鼠标点击按框内列定位光标");

            // 长度截断：SetValue / SetMaxLength 调小 / 插入都必须生效
            TextInput tl;
            tl.SetMaxLength(3);
            tl.SetValue("abcdef");
            Check(tl.value == "abc", "SetValue 按 maxLength 截断");
            tl.SetValue("abcd");
            tl.SetMaxLength(2);
            Check(tl.value == "ab", "调小 maxLength 立刻截断现有内容");
            tl.SetValue("");
            tl.InsertText("abcdef");
            Check(tl.value == "ab", "连续插入不会超过 maxLength");

            // Ctrl+← 只按词移动，不删选区/不删字
            TextInput tw;
            tw.SetValue("hello world");
            tw.cursor = tw.Cps().size();
            tw.selAnchor = 0;                       // 有选区时也不能被 Ctrl+← 吃掉
            const std::string before = tw.value;
            Input::SetLastModifiers(true, false, false);
            tw.OnKey(xChar(SKey::LeftArrow));
            Input::ClearModifiers();
            Check(tw.value == before && tw.cursor == 6, "Ctrl+← 按词移动且不删除任何文字");

            // 水平滚动留上下文：光标贴右边时左侧仍有多列预览
            TextInput ts;
            ts.layout.Width(12);
            ts.SetValue("0123456789abcdefghij");
            ts.cursor = 15;                         // 不贴到最右端，检查光标右侧也能留出预览
            ts.selAnchor = 15;
            ts.Arrange(Rect2Di{ 0, 0, 12, 3 });
            ConsoleBackBuffer sbuf(12, 3);
            ts.OnRender(sbuf);
            Check(ts.scroll == 8, "光标靠近右边界时左右都留出上下文（scroll=" + std::to_string(ts.scroll) + "）");

            // MENU 键：打开编辑菜单、上下选择、Enter 执行后关闭
            TextInput tm("abc");
            tm.SelectAll();
            tm.OnKey(xChar(SKey::Menu));
            Check(tm.MenuOpen(), "MENU 键打开编辑菜单");
            tm.OnKey(xChar(SKey::DownArrow));
            tm.OnKey(xChar(13));
            Check(!tm.MenuOpen(), "菜单 Enter 执行后关闭");
        }

        std::cout << "[selftest] box fallback\n";
        {
            Box::SetAsciiFallback(true);
            Check(Box::Effective(Box::Round).horizontal == L'-', "ASCII 模式横线降级");
            Check(Box::Effective(Box::Double).vertical == L'|', "ASCII 模式竖线降级");
            Check(Box::Effective(Box::Blank).horizontal == L' ', "Blank 不参与降级");
            Check(Glyphs::Dot() == L'·' && Glyphs::DotWidth() == 1, "圆点 ● 降级为 ·");
            Check(Glyphs::DotUtf8() == "\xC2\xB7", "圆点 UTF-8 降级");
            Box::SetAsciiFallback(false);
            Check(Box::Effective(Box::Round).topLeft == L'╭', "Unicode 模式保留圆角");
            Check(Glyphs::Dot() == L'●' && Glyphs::DotWidth() == 2, "Unicode 模式保留圆点");
            Box::AutoAsciiFallback();
        }

        std::cout << "[selftest] image\n";
        {
            UIImage img;
            std::vector<std::uint8_t> px = {
                255, 0, 0, 255,   255, 0, 0, 255,   // 上排红
                0, 0, 255, 255,   0, 0, 255, 255,   // 下排蓝
            };
            Check(img.SetPixels(2, 2, px), "SetPixels 接收 2x2 RGBA");
            Check(img.PixelWidth() == 2 && img.PixelHeight() == 2, "像素尺寸正确");
            img.Arrange(Bounds{ 0, 0, 2, 1 });
            ConsoleBackBuffer buf(2, 1);
            buf.Clear(Style());
            img.Render(buf);
            Check(buf.GetChar(0, 0) == L'▀' && buf.GetChar(1, 0) == L'▀', "每格用 ▀ 合成上下两个像素");

            UIImage file;
            Check(file.SetImage(DemoAsset("terminal_logo.png")), "WIC 解码 PNG 成功");
            Check(file.PixelWidth() == 24 && file.PixelHeight() == 24, "PNG 尺寸 24x24");
        }

        std::cout << "[selftest] scroll view\n";
        {
            auto sv = std::make_shared<ScrollView>();
            sv->SetWheelStep(3);
            for (int i = 0; i < 10; ++i) {
                auto t = std::make_shared<Text>("row");
                t->layout.height = 1;
                sv->AddChild(t);
            }
            sv->Arrange(Bounds{ 0, 0, 10, 3 });
            Check(sv->MaxScroll() == 7, "内容 10 行 / 视口 3 行 -> 最大滚动 7");
            Check(sv->Bar().valid, "内容溢出时显示竖向滚动条");
            sv->SetScroll(5);
            Check(sv->scroll == 5, "SetScroll 生效");
            xMouse wheel{};
            wheel.action = MouseAction::Wheel;
            wheel.wheelDelta = -120;          // 向下滚
            wheel.x = 5;
            wheel.y = 1;
            Check(sv->OnMouse(wheel), "滚轮事件被滚动容器消费");
            Check(sv->scroll == 7, "向下滚轮 +3 行并被夹到尾部");
            Check(sv->OnKey(xChar(SKey::Home)) && sv->scroll == 0, "Home 回到顶部");

            // 滚动后重排：子元素整体上移
            sv->SetScroll(4);
            sv->Arrange(Bounds{ 0, 0, 10, 3 });
            Check(sv->children[0]->bounds.Top() == -4, "滚动后子元素按 scroll 上移");

            // 走 MouseRouter：滚轮事件从命中的子元素冒泡到滚动容器
            MouseRouter::Instance().SetRoot(sv.get());
            xMouse routed{};
            routed.action = MouseAction::Wheel;
            routed.wheelDelta = -120;
            routed.x = 5;
            routed.y = 1;
            Check(MouseRouter::Instance().Dispatch(routed), "MouseRouter 把滚轮冒泡给 ScrollView");
            MouseRouter::Instance().Clear();
            Check(sv->scroll == 7, "路由后的滚轮同样夹在最大滚动值");
        }

        std::cout << "[selftest] message queue\n";
        {
            MessageQueue::Clear();
            auto label = std::make_shared<Text>("x");
            auto btn = std::make_shared<Button>("b");
            bool clicked = false;
            btn->onClick = [&clicked]() { clicked = true; };

            MessageQueue::Post(label.get(), "setText", "已提交");
            MessageQueue::Post(btn.get(), "click");
            Check(MessageQueue::Size() == 2, "交互回调只负责入队");
            Check(MessageQueue::ProcessAll() == 2, "主循环一次排空队列");
            Check(label->GetText() == "已提交", "消息携带的对象收到并处理自己的消息");
            Check(clicked, "click 消息驱动按钮 Trigger");
            Check(MessageQueue::Empty(), "处理完后队列为空");

            MessageQueue::Post(label.get(), "setText", "A");
            MessageQueue::Post(label.get(), "append", "B");
            MessageQueue::ProcessAll();
            Check(label->GetText() == "AB", "同一对象连续消息按顺序生效");

            // 新消息结构：sender + Json 负载
            struct Probe : UI {
                Message last{};
                bool got{ false };
                bool OnMessage(const Message& msg) override { last = msg; got = true; return true; }
            };
            auto probe = std::make_shared<Probe>();
            Json info = MsgJson::Object();
            info["name"] = Json(std::string("demo"));
            info["count"] = Json(3.0);
            MessageQueue::PostFrom(label.get(), probe.get(), "custom", info);
            MessageQueue::ProcessAll();
            Check(probe->got, "消息由控件地址发出并可定向投递");
            Check(probe->last.sender == label.get(), "消息记录发送者地址");
            Check(probe->last.payload.IsObject() && probe->last.payload.contains_key("name"),
                "消息携带 Json 对象负载");
        }

        std::cout << "[selftest] page holder\n";
        {
            auto outer = std::make_shared<PageHolder>("outer");
            auto inner = std::make_shared<PageHolder>("inner");
            auto target = std::make_shared<Text>("x");
            outer->AddChild(inner);
            inner->AddChild(target);
            Check(inner->parentPage == outer.get(), "嵌套页面找到父 PageHolder");

            inner->PostLocal(target.get(), "unknown", MsgJson::Object());
            Check(inner->PendingLocal() == 1, "消息进入子页面独立队列");
            inner->ProcessLocal();
            Check(inner->PendingLocal() == 0, "子页面处理后清空本页队列");
            Check(outer->PendingLocal() == 1, "未消费消息冒泡到上一层页面");
            outer->ProcessLocal();
            Check(outer->PendingLocal() == 0, "根页面处理完后队列为空");
        }

        std::cout << "[selftest] switch / choices / window\n";
        {
            auto sw = std::make_shared<Switch>();
            MessageQueue::PostFrom(sw.get(), sw.get(), "toggle");
            MessageQueue::ProcessAll();
            Check(sw->value, "Switch 通过消息队列切换状态");
            Check(MessageQueue::Empty(), "Switch change 通知处理完毕");

            // 鼠标：开关整行可点，按下/抬起后走同一消息队列
            sw->SetValue(false, false);
            sw->Arrange(Bounds{ 0, 0, 12, 1 });
            xMouse swDown{};
            swDown.action = MouseAction::Down;
            swDown.button = MouseButton::Left;
            swDown.x = 2;
            swDown.y = 0;
            Check(sw->OnMouse(swDown), "Switch 鼠标按下被消费");
            xMouse swUp = swDown;
            swUp.action = MouseAction::Up;
            Check(sw->OnMouse(swUp), "Switch 鼠标抬起被消费");
            MessageQueue::ProcessAll();
            Check(sw->value, "Switch 鼠标点击切换状态");

            auto cg = std::make_shared<ChoiceGroup>();
            cg->SetOptions({ "A", "B", "C" });
            Json pick = MsgJson::Object();
            pick["index"] = Json(2.0);
            MessageQueue::PostFrom(cg.get(), cg.get(), "toggle", pick);
            MessageQueue::ProcessAll();
            Check(cg->SelectedIndex() == 2, "单选 Choices 选中指定项");

            // 鼠标：悬停行高亮 + 点击第二行选中
            cg->SetOptions({ "A", "B", "C" });
            cg->Arrange(Bounds{ 0, 0, 12, 3 });
            xMouse hover{};
            hover.action = MouseAction::Move;
            hover.x = 2;
            hover.y = 1;
            Check(cg->OnMouse(hover), "Choices 鼠标悬停被消费");
            xMouse click = hover;
            click.action = MouseAction::Down;
            click.button = MouseButton::Left;
            Check(cg->OnMouse(click), "Choices 鼠标点击被消费");
            MessageQueue::ProcessAll();
            Check(cg->SelectedIndex() == 1, "Choices 鼠标点击选中对应行");

            cg->SetMulti(true);
            cg->SetSelected(2, false);
            cg->SetSelected(1, false);
            for (int i : { 0, 1 }) {
                Json p = MsgJson::Object();
                p["index"] = Json(static_cast<double>(i));
                MessageQueue::PostFrom(cg.get(), cg.get(), "toggle", p);
                MessageQueue::ProcessAll();
            }
            Check(cg->SelectedIndices().size() == 2, "多选 Choices 可同时选中多项");

            // ScrollView 里的 ChoiceGroup：鼠标命中必须穿过滚动容器落到行上
            auto svHost = std::make_shared<Panel>(Flow::Column);
            auto sv = std::make_shared<ScrollView>();
            sv->layout.Width(12);
            sv->layout.Height(3);
            auto cgInScroll = std::make_shared<ChoiceGroup>();
            cgInScroll->SetOptions({ "一", "二", "三", "四", "五" });
            cgInScroll->layout.height = 5;
            sv->AddChild(cgInScroll);
            svHost->AddChild(sv);
            svHost->Arrange(Bounds{ 0, 0, 12, 3 });
            MouseRouter::Instance().SetRoot(svHost.get());
            xMouse scrollClick{};
            scrollClick.action = MouseAction::Down;
            scrollClick.button = MouseButton::Left;
            scrollClick.x = sv->GetBounds().Left() + 1;
            scrollClick.y = sv->GetBounds().Top();
            MouseRouter::Instance().Dispatch(scrollClick);
            MessageQueue::ProcessAll();
            Check(cgInScroll->SelectedIndex() == 0, "滚动容器内单选项可鼠标点击");

            // 滚过之后再点：子元素 bounds 变成负 y，命中仍应落到可见行
            const int topBeforeScroll = cgInScroll->GetBounds().Top();
            sv->SetScroll(1);
            Check(cgInScroll->GetBounds().Top() == topBeforeScroll - 1,
                "滚动后子元素立即上移");
            svHost->Arrange(Bounds{ 0, 0, 12, 3 });
            scrollClick.y = sv->GetBounds().Top();
            MouseRouter::Instance().Dispatch(scrollClick);
            MessageQueue::ProcessAll();
            Check(cgInScroll->SelectedIndex() == 1, "滚动后的单选项仍可鼠标点击");
            MouseRouter::Instance().Clear();

            // 横向：内容比视口宽时出现底部滑条，并能用 scrollX 平移
            auto hsv = std::make_shared<ScrollView>();
            hsv->layout.Width(8);
            hsv->layout.Height(3);
            auto wide = std::make_shared<Text>("0123456789ABCDEFGHIJ");
            wide->layout.width = 20;
            wide->layout.height = 1;
            hsv->AddChild(wide);
            hsv->Arrange(Bounds{ 0, 0, 8, 3 });
            Check(hsv->HasHBar() && hsv->HBar().valid, "宽内容自动出现横向滑条");
            const auto hbg = hsv->HBar();
            Check(hsv->HitTestOverlay(hbg.left + 1, hbg.y), "横向滑条优先于内容命中");
            const int leftBeforeX = wide->GetBounds().Left();
            hsv->SetScrollX(2);
            Check(wide->GetBounds().Left() == leftBeforeX - 2, "横向滚动子元素立即左移");

            auto win = std::make_shared<Window>("Demo", 20, 8);
            win->MoveTo(2, 1);
            win->Arrange(Bounds{ 0, 0, 40, 20 });
            Check(win->bounds.Left() == 2 && win->bounds.Top() == 1, "Window 按指定位置摆放");

            const int oldLeft = win->bounds.Left();
            xMouse down{};
            down.action = MouseAction::Down;
            down.button = MouseButton::Left;
            down.x = win->bounds.Left() + 3;
            down.y = win->bounds.Top() + 1;
            Check(win->OnMouse(down), "标题栏按下进入拖拽");
            xMouse move = down;
            move.action = MouseAction::Move;
            move.x = down.x + 4;
            move.y = down.y + 2;
            Check(win->OnMouse(move) && win->bounds.Left() > oldLeft, "Window 标题栏拖拽移动");

            // Ctrl + 边框 -> 8 方向缩放；这里测左上角拖拽
            win->SetResizable(true);
            win->SetMinSize(12, 6);
            const int rl = win->bounds.Left(), rt = win->bounds.Top();
            const int rr = win->bounds.Right() - 1, rb = win->bounds.Bottom() - 1;
            const int rmx = rl + win->Width() / 2, rmy = rt + win->Height() / 2;
            Check(win->HitResizeEdge(rl, rt) == Window::ResizeTopLeft &&
                  win->HitResizeEdge(rr, rt) == Window::ResizeTopRight &&
                  win->HitResizeEdge(rl, rb) == Window::ResizeBottomLeft &&
                  win->HitResizeEdge(rr, rb) == Window::ResizeBottomRight &&
                  win->HitResizeEdge(rmx, rt) == Window::ResizeTop &&
                  win->HitResizeEdge(rmx, rb) == Window::ResizeBottom &&
                  win->HitResizeEdge(rl, rmy) == Window::ResizeLeft &&
                  win->HitResizeEdge(rr, rmy) == Window::ResizeRight,
                "8 个方向把手均可命中");
            const int oldW = win->Width();
            const int oldH = win->Height();
            xMouse resizeDown{};
            resizeDown.action = MouseAction::Down;
            resizeDown.button = MouseButton::Left;
            resizeDown.ctrl = true;
            resizeDown.x = win->bounds.Left();
            resizeDown.y = win->bounds.Top();
            Check(win->OnMouse(resizeDown), "Ctrl 点边框进入缩放");
            xMouse resizeMove = resizeDown;
            resizeMove.action = MouseAction::Move;
            resizeMove.x = resizeDown.x + 4;
            resizeMove.y = resizeDown.y + 2;
            Check(win->OnMouse(resizeMove), "缩放拖拽被消费");
            Check(win->Width() == oldW - 4 && win->Height() == oldH - 2,
                "缩放改变窗口尺寸");
            xMouse resizeUp = resizeMove;
            resizeUp.action = MouseAction::Up;
            Check(win->OnMouse(resizeUp), "缩放抬起结束");

            // 在 OverlayHost 中向右拖边框应能放大，而不是只能缩小
            auto ov = std::make_shared<OverlayHost>();
            auto grow = std::make_shared<Window>("Grow", 20, 8);
            grow->MoveTo(2, 2);
            grow->SetResizable(true);
            grow->SetMinSize(10, 6);
            ov->AddChild(grow);
            ov->Arrange(Bounds{ 0, 0, 40, 20 });
            Check(ov->HitTest(30, 15) == false, "OverlayHost 窗口外不拦截鼠标");
            Check(grow->Width() == 20, "OverlayHost 内窗口初始宽度");
            xMouse growDown{};
            growDown.action = MouseAction::Down;
            growDown.button = MouseButton::Left;
            growDown.ctrl = true;
            growDown.x = grow->bounds.Right() - 1;
            growDown.y = grow->bounds.Top() + 1;
            Check(grow->OnMouse(growDown), "OverlayHost 右边框进入缩放");
            xMouse growMove = growDown;
            growMove.action = MouseAction::Move;
            growMove.x += 6;
            grow->OnMouse(growMove);
            Check(grow->Width() == 26, "OverlayHost 内可向右放大窗口");

            MessageQueue::PostFrom(win.get(), win.get(), "close");
            MessageQueue::ProcessAll();
            Check(!win->open && !win->isVisible, "Window 关闭消息生效");
        }

        std::cout << "[selftest] mouse cursor\n";
        {
            TextInput input;
            Check(input.CursorAt(0, 0) == MouseCursor::IBeam, "输入框指针为 IBeam");
            Button button("OK");
            Check(button.CursorAt(0, 0) == MouseCursor::Hand, "按钮指针为 Hand");
            Slider slider("音量", 0, 100, 50);
            Check(slider.CursorAt(0, 0) == MouseCursor::Hand, "滑杆指针为 Hand");

            auto win = std::make_shared<Window>("Cursor", 20, 10);
            win->SetResizable(true);
            win->Arrange(Bounds{ 0, 0, 20, 10 });
            Check(win->CursorAt(0, 0) == MouseCursor::SizeAll, "窗口角点指针为 SizeAll");
            Check(win->CursorAt(0, 4) == MouseCursor::SizeWE, "窗口左边框指针为 SizeWE");
            Check(win->CursorAt(10, 0) == MouseCursor::SizeNS, "窗口上边框指针为 SizeNS");
        }

        std::cout << (g_fail == 0 ? "[selftest] ALL PASSED\n" : "[selftest] FAILED\n");
        return g_fail == 0 ? 0 : 1;
    }

    //----------------------------------------------------------------------
    // 布局树转储（调试布局用）
    //----------------------------------------------------------------------
    void DumpTree(const UI& ui, const int depth) {
        const auto& b = ui.GetBounds();
        std::string pad(static_cast<size_t>(depth * 2), ' ');
        std::printf("%s%s bounds=(%d,%d %dx%d) pref=%dx%d\n", pad.c_str(), ui.signature.c_str(),
            b.Left(), b.Top(), b.width(), b.height(), ui.PreferredWidth(), ui.PreferredHeight());
        for (const auto& c : ui.children) if (c) DumpTree(*c, depth + 1);
    }

    int RunDump() {
        ConsoleBackBuffer buf(80, 24);
        auto root = BuildDemoUI();
        root->Arrange(Rect2Di{ 0, 0, buf.Width(), buf.Height() });
        root->Mount();
        DumpTree(*root, 0);
        return 0;
    }

    //----------------------------------------------------------------------
    // 单帧渲染：把整屏用 ANSI 打到 stdout
    //----------------------------------------------------------------------
    int RunFrame() {
        ConsoleBackBuffer buf(80, 30);   // 高一点，方便看到左侧滚动列表
        buf.SetTrueColor(true);   // 以 24 位真彩导出（16 色属性仍然保留在缓冲里）
        auto root = BuildDemoUI();
        root->Arrange(Rect2Di{ 0, 0, buf.Width(), buf.Height() });
        root->Mount();
        FocusManager::Instance().SetRoot(root.get());
        FocusManager::Instance().FocusFirst();

        buf.Clear(Style(Theme::Current().text, Theme::Current().background));
        root->Render(buf);
        std::cout << buf.ToAnsiText();
        FocusManager::Instance().Clear();
        return 0;
    }

    //----------------------------------------------------------------------
    // 交互模式
    //----------------------------------------------------------------------
    int RunInteractive() {
        SetStdCP();
        RemoveQuickEdit();
        CursorVisible(false);
        // Box::SetAsciiFallback(true);

        // 真彩默认可用：能探测到 VT 就直接走 ANSI 真彩，否则退回 16 色属性
        const bool wantTrueColor = (g_colorMode == 1) ? true
            : (g_colorMode == 2) ? false
            : ConsoleBackBuffer::SupportsTrueColor();

        ConsoleBackBuffer buf = ConsoleBackBuffer::FromConsole();
        buf.SetTrueColor(wantTrueColor);

        auto root = BuildDemoUI();
        root->Arrange(Rect2Di{ 0, 0, buf.Width(), buf.Height() });
        root->Mount();

        auto& focus = FocusManager::Instance();
        focus.SetRoot(root.get());
        focus.FocusFirst();

        // 鼠标：打开控制台鼠标输入（顺带关掉 QuickEdit），并挂上路由
        Mouse::Enable();
        MouseRouter::Instance().SetRoot(root.get());

        bool running = true;
        g_quitRequested = false;
        MessageQueue::Clear();

        while (running) {
            // 主循环统一处理消息队列：每条消息带着目标对象，由它自己 OnMessage
            if (MessageQueue::ProcessAll() > 0) root->Invalidate();
            if (g_quitRequested) { running = false; break; }

            // 尺寸变化时重建缓冲
            int w = 0, h = 0;
            if (ConsoleBackBuffer::QueryConsoleSize(w, h) && (w != buf.Width() || h != buf.Height())) {
                buf.Resize(w, h);
                buf.SetTrueColor(wantTrueColor);
                root->Arrange(Rect2Di{ 0, 0, w, h });
                root->Invalidate();
            }

            // 输入框多行拓高之类的尺寸变化：布局脏了就重排一遍
            if (root->layoutDirty) {
                root->Arrange(Rect2Di{ 0, 0, buf.Width(), buf.Height() });
                root->Invalidate();
            }

            // 鼠标事件优先：点击 / 拖拽 / 滚轮 / 悬停。
            // 高 DPI 下拖动会连续产生大量 Move，不能一次把队列抽干：每轮最多处理
            // 64 条，并把「捕获中的拖拽 Move」合并成最后一条，保证主循环还能渲染。
            xMouse mouse;
            xMouse pendingDrag{};
            bool hasPendingDrag = false;
            int mouseBudget = 64;
            while (mouseBudget-- > 0 && Mouse::PollMouse(mouse)) {
                if (mouse.action == MouseAction::Move &&
                    MouseRouter::Instance().Capture() != nullptr) {
                    pendingDrag = mouse;
                    hasPendingDrag = true;
                    continue;
                }
                if (hasPendingDrag) {
                    MouseRouter::Instance().Dispatch(pendingDrag);
                    hasPendingDrag = false;
                }
                MouseRouter::Instance().Dispatch(mouse);
            }
            if (hasPendingDrag) MouseRouter::Instance().Dispatch(pendingDrag);

            // 拖拽捕获期间强制刷新：滑杆/窗口即使某次状态值没变化，也要让
            // 指针和滑块跟上手，而不是等下一次无关的 Invalidate 才重绘。
            if (MouseRouter::Instance().Capture() != nullptr) root->Invalidate();

            xChar key;
            while (Mouse::PollKey(key)) {
                if (IsKey(key, SKey::Esc)) {
                    running = false;
                    break;
                }
                if (focus.Dispatch(key)) continue;
                // 未消费的按键：q 退出
                if (key.IsPrintableAscii() && (key.Ascii() == 'q' || key.Ascii() == 'Q')) running = false;
            }

            // 鼠标/键盘也可能改布局（滚动、输入框变高、Window 内容变化）：
            // 在 Update/Render 之前再收敛一次，避免滚动条动了、列表却晚一帧才动。
            if (root->layoutDirty) {
                root->Arrange(Rect2Di{ 0, 0, buf.Width(), buf.Height() });
                root->Invalidate();
            }

            root->Update();
            // 指针样式跟随最深层控件；SetCursor 只在形状变化时调用
            MouseRouter::Instance().ApplyCursor();
            // 脏标记驱动：界面没有任何变化时直接跳过整帧的渲染与刷新（空闲 0 次控制台写入）
            if (root->IsDirty()) {
                buf.Clear(Style(Theme::Current().text, Theme::Current().background));
                root->Render(buf);
                buf.FlushAuto();
            }

            Sleep(30);
        }

        focus.Clear();
        MouseRouter::Instance().Clear();
        ApplyMouseCursor(MouseCursor::Arrow);
        Mouse::Disable();
        CursorVisible(true);
        SetCursorPosition(0, buf.Height());
        return 0;
    }

    //----------------------------------------------------------------------
    // 性能基准：同一界面连续渲染 N 帧，统计渲染+刷新耗时
    //----------------------------------------------------------------------
    int RunBench(const int frames) {
        ConsoleBackBuffer buf(120, 40);
        buf.SetTrueColor(true);
        auto root = BuildDemoUI();
        root->Arrange(Rect2Di{ 0, 0, buf.Width(), buf.Height() });
        root->Mount();
        FocusManager::Instance().SetRoot(root.get());
        FocusManager::Instance().FocusFirst();

        const auto t0 = std::chrono::high_resolution_clock::now();
        int rendered = 0;
        for (int i = 0; i < frames; ++i) {
            if (root->IsDirty()) {
                buf.Clear(Style(Theme::Current().text, Theme::Current().background));
                root->Render(buf);
                ++rendered;
            }
        }
        const auto t1 = std::chrono::high_resolution_clock::now();
        const double msIdle = std::chrono::duration<double, std::milli>(t1 - t0).count();

        // 强制每帧整树重绘（模拟持续动画的最坏情况）
        const auto t4 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < frames; ++i) {
            root->Invalidate();
            buf.Clear(Style(Theme::Current().text, Theme::Current().background));
            root->Render(buf);
        }
        const auto t5 = std::chrono::high_resolution_clock::now();
        const double msFull = std::chrono::duration<double, std::milli>(t5 - t4).count();

        // 真彩导出成本（整屏 ANSI 字符串生成）
        const auto t2 = std::chrono::high_resolution_clock::now();
        std::size_t bytes = 0;
        for (int i = 0; i < frames; ++i) bytes += buf.ToAnsiText().size();
        const auto t3 = std::chrono::high_resolution_clock::now();
        const double msAnsi = std::chrono::duration<double, std::milli>(t3 - t2).count();

        std::cout << "[bench] 画面 " << buf.Width() << "x" << buf.Height() << "  frames=" << frames << "\n"
            << "[bench] 脏标记路径 : " << (msIdle / frames) << " ms/frame（实际重绘 " << rendered << " 次）\n"
            << "[bench] 全量重绘   : " << (msFull / frames) << " ms/frame（布局+渲染+缓冲写入）\n"
            << "[bench] ANSI 生成  : " << (msAnsi / frames) << " ms/frame, "
            << (bytes / static_cast<std::size_t>(frames)) << " bytes/frame\n";
        FocusManager::Instance().Clear();
        return 0;
    }

} // namespace

int main(const int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--selftest") return RunSelfTest();
        if (arg == "--frame") return RunFrame();
        if (arg == "--dump") return RunDump();
        if (arg == "--truecolor" || arg == "--24bit") { g_colorMode = 1; continue; }
        if (arg == "--mono" || arg == "--16color") { g_colorMode = 2; continue; }
        if (arg == "--bench") { int n = 200; if (i + 1 < argc) n = std::atoi(argv[++i]); return RunBench(n); }
        if (arg == "--help" || arg == "-h") {
            std::cout << "usage: terminal_demo [--selftest|--frame|--dump|--bench N]\n"
                "                     [--truecolor|--mono]   (默认自动探测终端真彩能力)\n"
                "  交互模式：鼠标点击/拖动、Tab 切换焦点；左卡片 R/G/B 滑杆实时改昵称颜色\n"
                "  左卡片内容超出时：滚轮 / 右侧竖向滚动条 / PageUp·PageDown 滚动\n"
                "  输入框：Ctrl+C/V/X/Z/Y、Ctrl+A、Shift+方向键扩选、Insert 切换替换模式、MENU 键编辑菜单\n";
            return 0;
        }
    }
    return RunInteractive();
}
