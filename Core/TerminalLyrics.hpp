//
// Terminal / Core / TerminalLyrics.hpp
// 轻量 .lrc 歌词解析：时间轴 + 文本 + 元信息，供音乐播放器 demo 使用。
// 不依赖第三方库；支持 [mm:ss]、[mm:ss.xx]、[mm:ss:xx]、多时间标签、[offset:ms]。
//

#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace Terminal::Lyrics {

    struct Line {
        double time{ 0.0 };      // 秒
        std::string text{};
    };

    struct Document {
        std::string title{};
        std::string artist{};
        std::string album{};
        int offsetMs{ 0 };
        std::vector<Line> lines{};

        [[nodiscard]] bool Empty() const noexcept { return lines.empty(); }

        // 当前时间对应的最后一句；时间在首句之前返回空。
        // 不依赖 lines 已排序：表格编辑时间后仍能正确取到最后一句。
        [[nodiscard]] std::string At(const double seconds) const {
            const double t = seconds - (static_cast<double>(offsetMs) / 1000.0);
            const Line* current = nullptr;
            for (const auto& line : lines) {
                if (line.time > t) continue;
                if (current == nullptr || line.time >= current->time) current = &line;
            }
            return current != nullptr ? current->text : std::string{};
        }

        [[nodiscard]] const Line* CurrentLine(const double seconds) const {
            const double t = seconds - (static_cast<double>(offsetMs) / 1000.0);
            const Line* current = nullptr;
            for (const auto& line : lines) {
                if (line.time > t) continue;
                if (current == nullptr || line.time >= current->time) current = &line;
            }
            return current;
        }

        static std::string Trim(std::string_view text) {
            std::size_t begin = 0;
            std::size_t end = text.size();
            while (begin < end && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r')) ++begin;
            while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r')) --end;
            return std::string(text.substr(begin, end - begin));
        }

        static bool ParseTime(const std::string_view tag, double& out) {
            // 支持三种常见写法：
            //   [mm:ss]       [mm:ss.xx]       [mm:ss:xx]（部分播放器导出的 LRC）
            const auto colon = tag.find(':');
            if (colon == std::string_view::npos || colon == 0) return false;
            for (std::size_t i = 0; i < colon; ++i) {
                if (!std::isdigit(static_cast<unsigned char>(tag[i]))) return false;
            }

            const int minute = std::atoi(std::string(tag.substr(0, colon)).c_str());
            const std::string_view rest = tag.substr(colon + 1);
            if (rest.empty()) return false;

            // [mm:ss:xx]：第二个冒号后面是百分秒/千分秒，而不是秒的小数点。
            if (const auto secondColon = rest.find(':'); secondColon != std::string_view::npos) {
                const std::string_view secondsPart = rest.substr(0, secondColon);
                const std::string_view fractionPart = rest.substr(secondColon + 1);
                if (secondsPart.empty() || fractionPart.empty()) return false;
                for (const char ch : secondsPart) {
                    if (!std::isdigit(static_cast<unsigned char>(ch))) return false;
                }
                for (const char ch : fractionPart) {
                    if (!std::isdigit(static_cast<unsigned char>(ch))) return false;
                }

                const int seconds = std::atoi(std::string(secondsPart).c_str());
                const int fraction = std::atoi(std::string(fractionPart).c_str());
                double scale = 1.0;
                for (std::size_t i = 0; i < fractionPart.size(); ++i) scale *= 10.0;
                out = static_cast<double>(minute) * 60.0 + static_cast<double>(seconds)
                    + static_cast<double>(fraction) / scale;
                return out >= 0.0;
            }

            const std::string secondsText(rest);
            char* end = nullptr;
            const double seconds = std::strtod(secondsText.c_str(), &end);
            if (end == secondsText.c_str()) return false;
            while (*end == ' ' || *end == '\t') ++end;
            if (*end != '\0') return false;
            if (minute < 0 || seconds < 0.0) return false;
            out = static_cast<double>(minute) * 60.0 + seconds;
            return true;
        }

        static Document Parse(std::string_view text) {
            Document doc;
            if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
                static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) {
                text.remove_prefix(3);
            }

            std::size_t pos = 0;
            while (pos <= text.size()) {
                const auto nl = text.find('\n', pos);
                const auto end = (nl == std::string_view::npos) ? text.size() : nl;
                const std::string raw = Trim(text.substr(pos, end - pos));
                pos = (nl == std::string_view::npos) ? text.size() + 1 : nl + 1;
                if (raw.empty()) continue;

                // 收集行首连续的 [...] 标签
                std::vector<double> times;
                std::size_t cursor = 0;
                bool metadata = false;
                std::string metaKey, metaValue;
                while (cursor < raw.size() && raw[cursor] == '[') {
                    const auto close = raw.find(']', cursor);
                    if (close == std::string::npos) break;
                    const std::string tag = raw.substr(cursor + 1, close - cursor - 1);
                    const auto tagColon = tag.find(':');
                    if (tagColon != std::string::npos) {
                        const std::string key = Trim(tag.substr(0, tagColon));
                        const std::string value = Trim(tag.substr(tagColon + 1));
                        if (key == "ti") { doc.title = value; metadata = true; }
                        else if (key == "ar") { doc.artist = value; metadata = true; }
                        else if (key == "al") { doc.album = value; metadata = true; }
                        else if (key == "offset") {
                            doc.offsetMs = std::atoi(value.c_str());
                            metadata = true;
                        }
                    }

                    double seconds = 0.0;
                    if (ParseTime(tag, seconds)) times.push_back(seconds);
                    cursor = close + 1;
                }

                if (metadata) continue;
                if (times.empty()) continue;
                const std::string content = Trim(std::string_view(raw).substr(cursor));
                for (const double t : times) doc.lines.push_back(Line{ t, content });
            }

            std::stable_sort(doc.lines.begin(), doc.lines.end(),
                [](const Line& a, const Line& b) { return a.time < b.time; });
            return doc;
        }

        static Document Load(const std::string& path) {
            const std::u8string u8(reinterpret_cast<const char8_t*>(path.data()), path.size());
            return LoadPath(std::filesystem::path(u8));
        }

        static Document LoadPath(const std::filesystem::path& path) {
            std::ifstream in(path, std::ios::binary);
            if (!in) return {};
            std::string text;
            text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            return Parse(text);
        }

        // mm:ss.xx；用于表格编辑和写回 .lrc。
        static std::string FormatTime(double seconds) {
            if (seconds < 0.0) seconds = 0.0;
            const long long centiseconds = static_cast<long long>(std::llround(seconds * 100.0));
            const long long minutes = centiseconds / 6000;
            const long long wholeSeconds = (centiseconds % 6000) / 100;
            const long long fraction = centiseconds % 100;
            std::ostringstream out;
            out << std::setfill('0') << std::setw(2) << minutes << ':'
                << std::setw(2) << wholeSeconds << '.' << std::setw(2) << fraction;
            return out.str();
        }

        // 写回时按时间排序，保留元信息；空歌词行也保留，方便编辑。
        [[nodiscard]] std::string Serialize() const {
            std::ostringstream out;
            if (!title.empty()) out << "[ti:" << title << "]\n";
            if (!artist.empty()) out << "[ar:" << artist << "]\n";
            if (!album.empty()) out << "[al:" << album << "]\n";
            if (offsetMs != 0) out << "[offset:" << offsetMs << "]\n";
            std::vector<Line> sorted = lines;
            std::stable_sort(sorted.begin(), sorted.end(),
                [](const Line& a, const Line& b) { return a.time < b.time; });
            for (const auto& line : sorted) {
                out << '[' << FormatTime(line.time) << ']' << line.text << '\n';
            }
            return out.str();
        }

        bool SavePath(const std::filesystem::path& path) const {
            std::error_code ec;
            if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), ec);
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            if (!out) return false;
            const std::string text = Serialize();
            out.write(text.data(), static_cast<std::streamsize>(text.size()));
            return out.good();
        }

        // 音频同目录、同名的 .lrc 路径（不要求文件已经存在）。
        static std::filesystem::path LrcPathForAudio(const std::string& audioPath) {
            const std::u8string u8(reinterpret_cast<const char8_t*>(audioPath.data()), audioPath.size());
            std::filesystem::path path(u8);
            path.replace_extension(".lrc");
            return path;
        }

        // 保存到音频对应的 .lrc。
        bool SaveForAudio(const std::string& audioPath) const {
            return SavePath(LrcPathForAudio(audioPath));
        }

        // 音频同目录、同名的 .lrc；不存在时返回空文档。
        static Document LoadForAudio(const std::string& audioPath) {
            namespace fs = std::filesystem;
            fs::path path = LrcPathForAudio(audioPath);
            if (!fs::exists(path)) {
                fs::path upper = path;
                upper.replace_extension(".LRC");
                if (!fs::exists(upper)) return {};
                path = upper;
            }
            return LoadPath(path);
        }
    };

} // namespace Terminal::Lyrics
