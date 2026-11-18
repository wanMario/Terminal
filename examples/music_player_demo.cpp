//
// Terminal / examples / music_player_demo.cpp
// 音乐播放器 demo：扫描 D:\CloudMusic，播放 / 暂停 / 上一首 / 下一首 / 进度 / 倍速，
// 识别并编辑与音频同名的 .lrc 歌词。界面全部使用页面内嵌布局，不使用 Window。
//

#include "demo_common.hpp"

#include "../Core/TerminalAudio.hpp"
#include "../Core/TerminalLyrics.hpp"
#include "../Components/TerminalTextInput.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace Terminal;
using namespace Terminal::Demo;

namespace {

    std::string Lower(std::string text) {
        std::transform(text.begin(), text.end(), text.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    }

    std::vector<std::string> ScanAudioFiles(const std::string& root) {
        namespace fs = std::filesystem;
        std::vector<std::string> files;
        const fs::path dir = U8Path(root);
        if (!fs::is_directory(dir)) return files;

        static const std::vector<std::string> kExt = {
            ".mp3", ".wav", ".flac", ".m4a", ".aac", ".ogg", ".wma", ".opus"
        };
        std::error_code ec;
        for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec),
            end; it != end; it.increment(ec)) {
            if (ec) break;
            if (!it->is_regular_file(ec)) continue;
            const std::string ext = Lower(it->path().extension().string());
            if (std::find(kExt.begin(), kExt.end(), ext) == kExt.end()) continue;
            files.push_back(U8Text(it->path()));
            if (files.size() >= 200) break;
        }
        std::sort(files.begin(), files.end());
        return files;
    }

    // 页面内嵌的播放器 + 歌词编辑器。没有 Window，也就没有拖动/缩放/关闭弹窗。
    class MusicPage : public Panel {
    public:
        MusicPage(const std::shared_ptr<AudioPlayer>& audio, std::vector<std::string> tracks)
            : Panel(Flow::Column), m_audio(audio), m_tracks(std::move(tracks)) {
            layout.grow = 1;
            layout.gap = 0;
            SetFill(true);
            SetBackgroundColor(Theme::Current().surface);

            auto body = std::make_shared<Panel>(Flow::Row);
            body->layout.grow = 1;
            body->layout.gap = 1;
            body->SetFill(false);
            AddChild(body);

            // ---- 左：播放列表 ----
            auto left = std::make_shared<Panel>(Flow::Column);
            left->layout.Width(24);
            left->layout.gap = 0;
            left->SetFill(false);

            auto listTitle = std::make_shared<Text>("播放列表");
            listTitle->SetStyle(Style(Theme::Current().accent, Theme::Current().surface).SetBold());
            listTitle->layout.height = 1;
            left->AddChild(listTitle);

            m_trackScroll = std::make_shared<ScrollView>();
            m_trackScroll->layout.grow = 1;
            m_trackScroll->SetWheelStep(2);
            m_trackScroll->SetBackgroundColor(Theme::Current().surface);
            m_trackList = std::make_shared<ChoiceGroup>();
            m_trackList->layout.height = std::max(1, static_cast<int>(m_tracks.size()));
            m_trackScroll->AddChild(m_trackList);
            left->AddChild(m_trackScroll);
            body->AddChild(left);

            // ---- 右：播放状态 / 进度 / 控制 / LRC 编辑器 ----
            auto right = std::make_shared<Panel>(Flow::Column);
            right->layout.grow = 1;
            right->layout.gap = 0;
            right->SetFill(false);
            body->AddChild(right);

            m_status = std::make_shared<Text>("扫描 D:\\CloudMusic …");
            m_status->SetStyle(Style(Theme::Current().textMuted, Theme::Current().surface));
            m_status->layout.height = 1;
            m_status->layout.Width(46);
            right->AddChild(m_status);

            m_lyric = std::make_shared<Text>("♪ 未开始");
            m_lyric->SetStyle(Style(Theme::Current().info, Theme::Current().surface).SetBold());
            m_lyric->layout.height = 1;
            m_lyric->layout.Width(46);
            right->AddChild(m_lyric);

            m_progress = std::make_shared<Slider>("进度", 0.0, 1.0, 0.0);
            m_progress->barWidth = 30;
            m_progress->showValue = false;
            m_progress->layout.height = 1;
            right->AddChild(m_progress);

            auto controls = std::make_shared<Panel>(Flow::Row);
            controls->SetFill(false);
            controls->layout.gap = 1;
            controls->layout.height = 3;

            m_prev = std::make_shared<Button>("上一首");
            m_prev->layout.Width(8);
            m_prev->SetColor(Theme::Current().info);
            m_play = std::make_shared<Button>("暂停");
            m_play->layout.Width(8);
            m_play->SetColor(Theme::Current().success);
            m_next = std::make_shared<Button>("下一首");
            m_next->layout.Width(8);
            m_next->SetColor(Theme::Current().info);
            controls->AddChild(m_prev);
            controls->AddChild(m_play);
            controls->AddChild(m_next);
            right->AddChild(controls);

            m_speed = std::make_shared<Slider>("倍速", 0.5, 2.0, 1.0);
            m_speed->barWidth = 12;
            m_speed->step = 0.25;
            m_speed->precision = 2;
            m_speed->layout.height = 1;
            right->AddChild(m_speed);

            m_editorTitle = std::make_shared<Text>("LRC 时间轴 · 单击选中");
            m_editorTitle->SetStyle(Style(Theme::Current().textMuted, Theme::Current().surface));
            m_editorTitle->layout.height = 1;
            m_editorTitle->layout.Width(46);
            right->AddChild(m_editorTitle);

            m_editorScroll = std::make_shared<ScrollView>();
            m_editorScroll->layout.grow = 1;
            m_editorScroll->SetWheelStep(2);
            m_editorScroll->SetShowHScrollbar(false);
            m_editorScroll->SetBackgroundColor(Theme::Current().surface);
            m_editorRows = std::make_shared<Panel>(Flow::Column);
            m_editorRows->layout.gap = 0;
            m_editorRows->SetFill(false);
            m_editorScroll->AddChild(m_editorRows);
            right->AddChild(m_editorScroll);

            auto lineEditors = std::make_shared<Panel>(Flow::Row);
            lineEditors->SetFill(false);
            lineEditors->layout.gap = 1;
            lineEditors->layout.height = 3;

            m_timeEdit = std::make_shared<TextInput>("");
            m_timeEdit->SetLabel("时间");
            m_timeEdit->SetPlaceholder("00:12.04");
            m_timeEdit->SetMaxLength(16);
            m_timeEdit->layout.Width(16);
            m_timeEdit->layout.height = 3;

            m_lineEdit = std::make_shared<TextInput>("");
            m_lineEdit->SetLabel("歌词");
            m_lineEdit->SetPlaceholder("选中一句后在这里改词");
            m_lineEdit->SetMaxLength(512);
            m_lineEdit->layout.Width(29);
            m_lineEdit->layout.height = 3;

            lineEditors->AddChild(m_timeEdit);
            lineEditors->AddChild(m_lineEdit);
            right->AddChild(lineEditors);

            auto editActions = std::make_shared<Panel>(Flow::Row);
            editActions->SetFill(false);
            editActions->layout.gap = 1;
            editActions->layout.height = 3;
            m_playFrom = std::make_shared<Button>("从此播放");
            m_playFrom->layout.Width(10);
            m_playFrom->SetColor(Theme::Current().success);
            m_add = std::make_shared<Button>("此刻插入");
            m_add->layout.Width(10);
            m_add->SetColor(Theme::Current().info);
            m_delete = std::make_shared<Button>("删除所选");
            m_delete->layout.Width(10);
            m_delete->SetColor(Theme::Current().danger);
            editActions->AddChild(m_playFrom);
            editActions->AddChild(m_add);
            editActions->AddChild(m_delete);
            right->AddChild(editActions);

            auto editActions2 = std::make_shared<Panel>(Flow::Row);
            editActions2->SetFill(false);
            editActions2->layout.gap = 1;
            editActions2->layout.height = 3;
            m_apply = std::make_shared<Button>("应用修改");
            m_apply->layout.Width(10);
            m_apply->SetColor(Theme::Current().accent);
            m_save = std::make_shared<Button>("保存");
            m_save->layout.Width(8);
            m_save->SetColor(Theme::Current().success);
            editActions2->AddChild(m_apply);
            editActions2->AddChild(m_save);
            right->AddChild(editActions2);

            if (m_tracks.empty()) {
                m_status->SetText("D:\\CloudMusic 中未找到音频文件");
                m_play->SetEnabled(false);
                m_playFrom->SetEnabled(false);
                m_add->SetEnabled(false);
                m_delete->SetEnabled(false);
                m_apply->SetEnabled(false);
                m_save->SetEnabled(false);
                return;
            }

            std::vector<std::string> labels;
            labels.reserve(m_tracks.size());
            for (const auto& path : m_tracks) labels.push_back(ShortName(path));
            m_trackList->SetOptions(labels);
            m_trackList->SetSelected(0, false);

            m_trackList->onChange = [this](const std::vector<int>& indices) {
                if (m_syncing || indices.empty()) return;
                LoadIndex(indices.front());
                };
            m_prev->onClick = [this] { LoadIndex(m_index - 1); };
            m_next->onClick = [this] { LoadIndex(m_index + 1); };
            m_play->onClick = [this] {
                if (!m_audio->IsOpen()) return;
                if (m_audio->IsPlaying()) m_audio->Pause();
                else m_audio->Resume();
                UpdateButtonLabel();
                };
            m_progress->onCommitted = [this](double seconds) { m_audio->Seek(seconds); };
            m_speed->onChanged = [this](double speed) { m_audio->SetSpeed(speed); };

            m_playFrom->onClick = [this] { PlayFromSelected(); };
            m_add->onClick = [this] { InsertAtPlayhead(); };
            m_delete->onClick = [this] { DeleteSelectedLine(); };
            m_apply->onClick = [this] { ApplySelectedLine(); };
            m_lineEdit->onSubmit = [this](const std::string&) { ApplySelectedLine(); };
            m_timeEdit->onSubmit = [this](const std::string&) { ApplySelectedLine(); };
            m_save->onClick = [this] { SaveLyrics(true); };

            LoadIndex(FirstTrackWithLyrics());
        }

        int FirstTrackWithLyrics() const {
            namespace fs = std::filesystem;
            for (std::size_t i = 0; i < m_tracks.size(); ++i) {
                const fs::path lrc = Lyrics::Document::LrcPathForAudio(m_tracks[i]);
                std::error_code ec;
                // 0 字节 .lrc 是网易云常见的占位文件，不能作为“已有歌词”的默认选中。
                if (fs::exists(lrc, ec) && fs::file_size(lrc, ec) > 0) {
                    if (!Lyrics::Document::LoadForAudio(m_tracks[i]).Empty()) return static_cast<int>(i);
                }
                fs::path upper = lrc;
                upper.replace_extension(".LRC");
                if (fs::exists(upper, ec) && fs::file_size(upper, ec) > 0) {
                    if (!Lyrics::Document::LoadForAudio(m_tracks[i]).Empty()) return static_cast<int>(i);
                }
            }
            return 0;
        }

        void OnUpdate() override {
            Panel::OnUpdate();
            m_audio->Update();
            if (!m_audio->IsOpen()) return;

            const double duration = m_audio->Duration();
            const double position = m_audio->Position();
            // 拖动进度条时不要用播放器位置覆盖用户拖动值；暂停时 old position 不变，
            // 否则每帧都会把滑块拉回去，看起来就是“暂停后进度条拖不动”。
            if (duration > 0.0 && !m_progress->IsDragging()) {
                m_progress->SetRange(0.0, duration);
                m_progress->SetValue(std::min(position, duration), false);
            }
            UpdateLyric(position);
            if (m_audio->IsPlaying() != m_wasPlaying) {
                m_wasPlaying = m_audio->IsPlaying();
                UpdateButtonLabel();
            }
        }

    private:
        static std::string ShortName(const std::string& path) {
            const auto name = U8Path(path).filename().u8string();
            return std::string(reinterpret_cast<const char*>(name.data()), name.size());
        }

        void UpdateButtonLabel() {
            if (m_play) m_play->SetLabel(m_audio->IsPlaying() ? "暂停" : "播放");
        }

        void UpdateLyric(const double position) {
            if (!m_lyric) return;
            if (m_editLines.empty()) {
                if (m_lyric->GetText() != "♪ 暂无歌词，可在下方时间轴插入") {
                    m_lyric->SetText("♪ 暂无歌词，可在下方时间轴插入");
                }
                if (m_playRow != -1) {
                    const int old = m_playRow;
                    m_playRow = -1;
                    StyleRow(old);
                }
                return;
            }
            const auto current = m_lyrics.At(position);
            const std::string line = current.empty() ? "♪ …" : ("♪ " + current);
            if (line != m_lastLyric) {
                m_lastLyric = line;
                m_lyric->SetText(line);
            }

            int row = -1;
            if (const auto* cur = m_lyrics.CurrentLine(position)) {
                row = static_cast<int>(cur - m_lyrics.lines.data());
            }
            if (row != m_playRow) {
                const int old = m_playRow;
                m_playRow = row;
                StyleRow(old);
                StyleRow(row);
                if (row >= 0) FollowRow(row);
            }
        }

        void SyncEditor() {
            RebuildRows();
            UpdateEditorTitle();
        }

        void UpdateEditorTitle() {
            if (!m_editorTitle) return;
            const std::string count = std::to_string(m_editLines.size()) + " 句";
            const std::string dirty = m_dirty ? " · 未保存" : "";
            const std::string path = m_hasTrack
                ? " · " + U8Text(Lyrics::Document::LrcPathForAudio(m_tracks[static_cast<size_t>(m_index)]))
                : "";
            m_editorTitle->SetText("LRC 时间轴  " + count + dirty + path);
        }

        // 时间轴行：一句一行，按时间先后排列；▶ 是当前播放句，选中行用高亮色。
        std::string RowLabel(const int index) const {
            const auto& line = m_editLines[static_cast<std::size_t>(index)];
            const std::string prefix = index == m_playRow ? "▶ " : "  ";
            return prefix + Lyrics::Document::FormatTime(line.time) + "  " + line.text;
        }

        // 只刷新一行的文字/颜色。歌词推进时只动新旧两行，不重建整个时间轴，
        // 避免 20+ 个 Button 每换一句就 ClearChildren 造成闪屏和卡顿。
        void StyleRow(const int index) {
            if (index < 0 || static_cast<std::size_t>(index) >= m_rows.size()) return;
            const bool playing = index == m_playRow;
            const bool selected = index == m_selected;
            auto& row = m_rows[static_cast<std::size_t>(index)];
            if (!row) return;
            row->SetLabel(RowLabel(index));
            const Color color = playing ? Theme::Current().accent
                : (selected ? Theme::Current().info : Theme::Current().textMuted);
            if (!row->useCustomColors || !row->normalFg.Equals(color) || !row->borderColor.Equals(color)) {
                row->SetColor(color);
            }
        }

        void RebuildRows() {
            if (!m_editorRows) return;
            m_editorRows->ClearChildren();
            m_rows.clear();
            m_rows.reserve(m_editLines.size());
            for (std::size_t i = 0; i < m_editLines.size(); ++i) {
                auto row = std::make_shared<Button>("");
                row->showFrame = false;
                row->layout.height = 1;
                row->layout.minHeight = 1;
                row->SetLabelAlign(LayoutPos::Left);
                row->SetShowFrame(false);
                row->onClick = [this, i] { SelectLine(static_cast<int>(i)); };
                m_editorRows->AddChild(row);
                m_rows.push_back(row);
                StyleRow(static_cast<int>(i));
            }
            m_editorScroll->InvalidateLayout();
            // 默认跟随当前播放句；手动选中时会由 SelectLine 覆盖到选中行。
            ScrollToRow(m_playRow >= 0 ? m_playRow : m_selected);
            Invalidate();
        }

        void ScrollToRow(const int index) {
            if (index < 0 || !m_editorScroll) return;
            const int viewH = std::max(m_editorScroll->ViewportHeightActual(), 1);
            m_editorScroll->SetScroll(std::max(0, index - viewH / 2));
        }

        // 播放跟随：只在当前句走出可视区时才滚一行，避免每换一句都把列表
        // 重新居中导致整块时间轴抖动/重绘。
        void FollowRow(const int index) {
            if (index < 0 || !m_editorScroll) return;
            const int viewH = std::max(m_editorScroll->ViewportHeightActual(), 1);
            const int top = m_editorScroll->scroll;
            if (index < top) m_editorScroll->SetScroll(index);
            else if (index >= top + viewH) m_editorScroll->SetScroll(index - viewH + 1);
        }

        void SelectLine(const int index) {
            if (index < 0 || static_cast<std::size_t>(index) >= m_editLines.size()) return;
            const int old = m_selected;
            m_selected = index;
            m_lineEdit->SetValue(m_editLines[static_cast<size_t>(index)].text);
            m_timeEdit->SetValue(Lyrics::Document::FormatTime(m_editLines[static_cast<size_t>(index)].time));
            StyleRow(old);
            StyleRow(index);
            ScrollToRow(m_selected);
            m_status->SetText("已选中 " + Lyrics::Document::FormatTime(m_editLines[static_cast<size_t>(index)].time)
                + " 这一句；可改时间/歌词，或从此播放");
        }

        void PlayFromSelected() {
            if (!m_hasTrack || m_selected < 0
                || static_cast<std::size_t>(m_selected) >= m_editLines.size()) {
                m_status->SetText("先在时间轴上选中一句");
                return;
            }
            if (!m_audio->IsOpen()) {
                m_status->SetText("音频未打开，无法从歌词播放");
                return;
            }

            const double time = m_editLines[static_cast<size_t>(m_selected)].time;
            if (!m_audio->Seek(time)) {
                m_status->SetText("跳转到歌词时间失败：" + m_audio->LastError());
                return;
            }
            if (!m_audio->Play()) {
                m_status->SetText("播放失败：" + m_audio->LastError());
                return;
            }
            m_wasPlaying = true;
            if (!m_progress->IsDragging()) {
                m_progress->SetRange(0.0, std::max(m_audio->Duration(), 1.0));
                m_progress->SetValue(time, false);
            }
            UpdateButtonLabel();
            UpdateLyric(time);
            m_status->SetText("从 " + Lyrics::Document::FormatTime(time) + " 开始播放");
        }

        void InsertAtPlayhead() {
            if (!m_hasTrack) return;
            const double time = m_audio->IsOpen() ? m_audio->Position() : 0.0;
            // 在当前播放时刻插入；同一时刻已有句子时插在它前面，保持时间线性。
            const auto where = std::lower_bound(m_editLines.begin(), m_editLines.end(), time,
                [](const Lyrics::Line& line, const double t) { return line.time < t; });
            const int index = static_cast<int>(std::distance(m_editLines.begin(), where));
            m_editLines.insert(where, Lyrics::Line{ time, "" });
            m_selected = index;
            m_lyrics.lines = m_editLines;
            m_dirty = true;
            m_lastLyric.clear();
            m_lineEdit->SetValue("");
            m_timeEdit->SetValue(Lyrics::Document::FormatTime(time));
            SyncEditor();
            m_status->SetText("已在 " + Lyrics::Document::FormatTime(time) + " 插入新句，输入歌词后按 Enter");
        }

        void ApplySelectedLine() {
            if (m_selected < 0 || static_cast<std::size_t>(m_selected) >= m_editLines.size()) {
                m_status->SetText("先在时间轴上选中一句");
                return;
            }

            double time = 0.0;
            if (!Lyrics::Document::ParseTime(m_timeEdit->GetValue(), time)) {
                m_status->SetText("时间格式应为 mm:ss.xx，例如 00:12.04");
                return;
            }
            const std::string text = m_lineEdit->GetValue();
            Lyrics::Line edited = m_editLines[static_cast<std::size_t>(m_selected)];
            if (std::abs(edited.time - time) < 1e-9 && edited.text == text) {
                m_status->SetText("这一句没有变化");
                return;
            }
            edited.time = time;
            edited.text = text;

            // 改时间后重新按时间排序，保持“时间线性”视图。
            m_editLines.erase(m_editLines.begin() + m_selected);
            const auto where = std::lower_bound(m_editLines.begin(), m_editLines.end(), time,
                [](const Lyrics::Line& line, const double t) { return line.time < t; });
            m_selected = static_cast<int>(std::distance(m_editLines.begin(), where));
            m_editLines.insert(where, edited);

            m_lyrics.lines = m_editLines;
            m_dirty = true;
            m_lastLyric.clear();
            m_playRow = -1;
            m_timeEdit->SetValue(Lyrics::Document::FormatTime(time));
            SyncEditor();
            m_status->SetText("已更新选中句的时间/歌词，记得保存");
        }

        void DeleteSelectedLine() {
            if (m_selected < 0 || static_cast<std::size_t>(m_selected) >= m_editLines.size()) {
                m_status->SetText("先在时间轴上选中一句再删除");
                return;
            }
            const int index = m_selected;
            m_editLines.erase(m_editLines.begin() + index);
            m_lyrics.lines = m_editLines;
            m_dirty = true;
            m_lastLyric.clear();
            if (m_editLines.empty()) m_selected = -1;
            else m_selected = std::min(index, static_cast<int>(m_editLines.size()) - 1);
            m_lineEdit->SetValue(m_selected >= 0 ? m_editLines[static_cast<size_t>(m_selected)].text : "");
            m_timeEdit->SetValue(m_selected >= 0
                ? Lyrics::Document::FormatTime(m_editLines[static_cast<size_t>(m_selected)].time) : "");
            SyncEditor();
            m_status->SetText("已删除选中句");
        }

        void SaveLyrics(const bool showMessage) {
            if (!m_hasTrack) return;
            m_lyrics.lines = m_editLines;
            const bool ok = m_lyrics.SaveForAudio(m_tracks[static_cast<size_t>(m_index)]);
            if (showMessage) {
                m_status->SetText(ok
                    ? "LRC 已保存：" + U8Text(Lyrics::Document::LrcPathForAudio(m_tracks[static_cast<size_t>(m_index)]))
                    : "LRC 保存失败，请检查目录权限");
            }
            if (ok) {
                m_dirty = false;
                UpdateEditorTitle();
            }
        }

        void LoadIndex(const int requested) {
            if (m_tracks.empty()) return;
            // 切歌前把未保存的歌词写回当前曲目的 .lrc，避免编辑内容丢失。
            if (m_hasTrack && m_dirty) SaveLyrics(false);
            int index = requested;
            if (index < 0) index = static_cast<int>(m_tracks.size()) - 1;
            if (index >= static_cast<int>(m_tracks.size())) index = 0;
            m_index = index;

            m_syncing = true;
            m_trackList->SetSelected(index, true);
            m_syncing = false;

            const std::string& path = m_tracks[static_cast<size_t>(index)];
            // LRC 与音频解码相互独立：即使媒体后端打不开音频，也先把已有歌词导入表格。
            m_lyrics = Lyrics::Document::LoadForAudio(path);
            m_editLines = m_lyrics.lines;
            m_selected = m_editLines.empty() ? -1 : 0;
            m_playRow = -1;
            m_hasTrack = true;
            m_dirty = false;
            m_lastLyric.clear();
            if (m_lineEdit) {
                m_lineEdit->SetValue(m_selected >= 0 ? m_editLines[static_cast<size_t>(m_selected)].text : "");
            }
            if (m_timeEdit) {
                m_timeEdit->SetValue(m_selected >= 0
                    ? Lyrics::Document::FormatTime(m_editLines[static_cast<size_t>(m_selected)].time)
                    : "");
            }
            SyncEditor();

            if (!m_audio->Open(path)) {
                m_play->SetEnabled(false);
                m_playFrom->SetEnabled(false);
                m_audio->SetSpeed(m_speed->value);
                m_progress->SetRange(0.0, 1.0);
                m_progress->SetValue(0.0, false);
                UpdateButtonLabel();
                const std::string lyricInfo = m_lyrics.Empty() ? "无 .lrc" : ("LRC " + std::to_string(m_lyrics.lines.size()) + " 行");
                m_status->SetText("打开失败：" + ShortName(path) + "  [" + lyricInfo + "] · " + m_audio->LastError());
                UpdateLyric(0.0);
                return;
            }
            m_play->SetEnabled(true);
            m_playFrom->SetEnabled(!m_editLines.empty());
            m_audio->SetSpeed(m_speed->value);
            m_progress->SetRange(0.0, std::max(m_audio->Duration(), 1.0));
            m_progress->SetValue(0.0, false);
            m_audio->Play();
            m_wasPlaying = true;
            UpdateButtonLabel();

            const std::string lyricInfo = m_lyrics.Empty() ? "无 .lrc" : ("LRC " + std::to_string(m_lyrics.lines.size()) + " 行");
            m_status->SetText("正在播放：" + ShortName(path) + "  [" + m_audio->BackendName() + " / " + lyricInfo + "]");
            UpdateLyric(0.0);
        }

        std::shared_ptr<AudioPlayer> m_audio;
        std::vector<std::string> m_tracks;
        int m_index{ 0 };
        bool m_syncing{ false };
        bool m_wasPlaying{ false };
        bool m_hasTrack{ false };
        bool m_dirty{ false };
        std::string m_lastLyric{};
        Lyrics::Document m_lyrics{};
        std::vector<Lyrics::Line> m_editLines{};

        std::shared_ptr<Text> m_status;
        std::shared_ptr<ScrollView> m_trackScroll;
        std::shared_ptr<ChoiceGroup> m_trackList;
        std::shared_ptr<Text> m_lyric;
        std::shared_ptr<Slider> m_progress;
        std::shared_ptr<Button> m_prev;
        std::shared_ptr<Button> m_play;
        std::shared_ptr<Button> m_next;
        std::shared_ptr<Slider> m_speed;
        std::shared_ptr<Text> m_editorTitle;
        std::shared_ptr<ScrollView> m_editorScroll;
        std::shared_ptr<Panel> m_editorRows;
        std::shared_ptr<TextInput> m_timeEdit;
        std::shared_ptr<TextInput> m_lineEdit;
        std::vector<std::shared_ptr<Button>> m_rows;
        int m_selected{ -1 };
        int m_playRow{ -1 };
        std::shared_ptr<Button> m_playFrom;
        std::shared_ptr<Button> m_add;
        std::shared_ptr<Button> m_delete;
        std::shared_ptr<Button> m_apply;
        std::shared_ptr<Button> m_save;
    };

    std::shared_ptr<Panel> BuildUI() {
        Theme::UseDark();
        const Theme& th = Theme::Current();

        auto root = std::make_shared<DemoRoot>(Flow::Column);
        root->layout.Padding(1);
        root->layout.gap = 0;
        root->SetFill(true);
        root->SetBackgroundColor(th.background);

        auto header = std::make_shared<Panel>(Flow::Row);
        header->SetFill(false);
        header->layout.gap = 2;
        header->layout.height = 1;
        auto brand = std::make_shared<Text>("> Terminal UI · 音乐播放器 / LRC 编辑器");
        brand->layout.grow = 1;
        brand->SetStyle(Style(th.accent, th.background).SetBold());
        header->AddChild(brand);
        auto pill = std::make_shared<Badge>("LRC");
        pill->SetColors(th.info, th.accentText);
        header->AddChild(pill);
        root->AddChild(header);
        root->AddChild(std::make_shared<Divider>());

        auto page = std::make_shared<MusicPage>(std::make_shared<AudioPlayer>(), ScanAudioFiles("D:\\CloudMusic"));
        page->layout.grow = 1;
        root->AddChild(page);

        auto footer = std::make_shared<Text>("单击歌词选中 · 从此播放 · 时间/歌词输入框 Enter 应用 · 进度条暂停也可拖 · q 退出");
        footer->SetStyle(Style(th.textMuted, th.background));
        footer->layout.height = 1;
        root->AddChild(footer);
        return root;
    }

} // namespace

int main(const int argc, char** argv) {
    const auto root = BuildUI();
    return RunMain(argc, argv, root,
        "usage: music_player_demo [--frame|--dump|--bench N] [--truecolor|--mono]\n"
        "  音乐播放器：D:\\CloudMusic，播放/暂停/倍速/进度，LRC 歌词编辑与保存（无 Window）\n");
}
