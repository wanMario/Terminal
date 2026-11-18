//
// Terminal / src / TerminalArchiveDll.cpp
// 方案 D：进程内 libarchive 后端，导出稳定 C ABI。
//
// 这个 DLL 不再释放 bz.exe、不再启动子进程，也不再依赖系统安装 Bandizip：
//   - ZIP / 7z 创建；ZIP / 7z / RAR 解压与列表；
//   - 读取加密 ZIP / 7z / RAR 时可用密码；
//   - ZIP 创建可用 ZipCrypto 密码（7z 写入加密 libarchive 不支持，会明确报错）。
//
// libarchive 以静态库形式链入，最终分发仍然只需要一个 TerminalArchive.dll。
//

#include "../Core/TerminalArchive.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#ifndef _WIN32
#error TerminalArchiveDll is Windows-only
#endif

#include <windows.h>

#ifndef LIBARCHIVE_STATIC
#define LIBARCHIVE_STATIC
#endif
#include <archive.h>
#include <archive_entry.h>

namespace {

    namespace fs = std::filesystem;

    std::wstring Utf8ToWide(const std::string& text) {
        if (text.empty()) return {};
        const int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
            static_cast<int>(text.size()), nullptr, 0);
        if (length <= 0) return {};
        std::wstring out(static_cast<std::size_t>(length), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
            out.data(), length);
        return out;
    }

    std::string WideToUtf8(const std::wstring& text) {
        if (text.empty()) return {};
        const int length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
            static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        if (length <= 0) return {};
        std::string out(static_cast<std::size_t>(length), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
            out.data(), length, nullptr, nullptr);
        return out;
    }

    void CopyText(char* buffer, const int bufferSize, const std::string& text) {
        if (buffer == nullptr || bufferSize <= 0) return;
        const std::size_t limit = static_cast<std::size_t>(bufferSize - 1);
        const std::size_t count = std::min(text.size(), limit);
        if (count > 0) std::memcpy(buffer, text.data(), count);
        buffer[count] = '\0';
    }

    Terminal::Archive::Format ToFormat(const int value) {
        switch (value) {
        case 1: return Terminal::Archive::Format::Zip;
        case 2: return Terminal::Archive::Format::SevenZip;
        case 3: return Terminal::Archive::Format::Rar;
        default: return Terminal::Archive::Format::Auto;
        }
    }

    Terminal::Archive::Format DetectFormat(const fs::path& archivePath) {
        std::wstring ext = archivePath.extension().wstring();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](const wchar_t c) {
            return static_cast<wchar_t>(std::towlower(c));
            });
        if (ext == L".zip") return Terminal::Archive::Format::Zip;
        if (ext == L".7z") return Terminal::Archive::Format::SevenZip;
        if (ext == L".rar") return Terminal::Archive::Format::Rar;
        return Terminal::Archive::Format::Auto;
    }

    // 读取压缩包内的条目名，拒绝绝对路径与 ..，避免解压穿越。
    bool SafeEntryPath(const std::string& utf8Name, fs::path& relative) {
        if (utf8Name.empty()) return false;
        fs::path raw(Utf8ToWide(utf8Name));
        if (raw.is_absolute()) return false;
        relative.clear();
        for (const auto& part : raw) {
            const std::wstring piece = part.wstring();
            if (piece.empty() || piece == L".") continue;
            if (piece == L"..") return false;
            // Windows 盘符 / UNC 残片
            if (piece.find(L':') != std::wstring::npos) return false;
            relative /= part;
        }
        return !relative.empty();
    }

    std::string JoinLines(const std::vector<std::string>& lines) {
        std::string out;
        for (const auto& line : lines) {
            if (!out.empty()) out += "\n";
            out += line;
        }
        return out;
    }

    bool CreateDirectories(const fs::path& path, std::string& error) {
        std::error_code ec;
        fs::create_directories(path, ec);
        if (ec) {
            error = "无法创建目录：" + WideToUtf8(path.wstring()) + "：" + ec.message();
            return false;
        }
        return true;
    }

    archive* OpenRead(const fs::path& path, const std::string& password, std::string& error) {
        archive* a = archive_read_new();
        if (a == nullptr) {
            error = "archive_read_new 失败";
            return nullptr;
        }
        archive_read_support_filter_all(a);
        archive_read_support_format_all(a);
        if (!password.empty()) {
            if (archive_read_add_passphrase(a, password.c_str()) != ARCHIVE_OK) {
                error = "无法设置解压密码：" + std::string(archive_error_string(a) ? archive_error_string(a) : "");
                archive_read_free(a);
                return nullptr;
            }
        }
        if (archive_read_open_filename_w(a, path.c_str(), 64 * 1024) != ARCHIVE_OK) {
            error = "无法打开压缩包：" + std::string(archive_error_string(a) ? archive_error_string(a) : "");
            archive_read_free(a);
            return nullptr;
        }
        return a;
    }

    bool WriteDataToFile(archive* a, const fs::path& target,
        const bool overwrite, std::string& error) {
        std::error_code ec;
        if (!overwrite && fs::exists(target, ec)) {
            error = "目标已存在（未覆盖）：" + WideToUtf8(target.wstring());
            return false;
        }

        if (!CreateDirectories(target.parent_path(), error)) return false;

        const HANDLE file = CreateFileW(target.c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            error = "无法创建文件：" + WideToUtf8(target.wstring());
            return false;
        }

        bool ok = true;
        char buffer[64 * 1024];
        for (;;) {
            const ssize_t got = archive_read_data(a, buffer, sizeof(buffer));
            if (got == 0) break;
            if (got < 0) {
                error = "读取条目数据失败：" + std::string(archive_error_string(a) ? archive_error_string(a) : "");
                ok = false;
                break;
            }
            const DWORD chunk = static_cast<DWORD>(got);
            DWORD written = 0;
            if (!WriteFile(file, buffer, chunk, &written, nullptr) || written != chunk) {
                error = "写入文件失败：" + WideToUtf8(target.wstring());
                ok = false;
                break;
            }
        }
        CloseHandle(file);
        if (!ok) DeleteFileW(target.c_str());
        return ok;
    }

    bool AddFileToArchive(archive* a, const fs::path& actual, const std::string& storedName,
        std::string& error) {
        std::error_code ec;
        const bool isDir = fs::is_directory(actual, ec);
        const auto size = isDir ? 0ull : fs::file_size(actual, ec);
        if (ec) {
            error = "无法读取文件信息：" + WideToUtf8(actual.wstring()) + "：" + ec.message();
            return false;
        }

        archive_entry* entry = archive_entry_new();
        if (entry == nullptr) {
            error = "archive_entry_new 失败";
            return false;
        }
        archive_entry_set_pathname_utf8(entry, storedName.c_str());
        archive_entry_set_filetype(entry, isDir ? AE_IFDIR : AE_IFREG);
        archive_entry_set_perm(entry, isDir ? 0755 : 0644);
        archive_entry_set_size(entry, static_cast<std::int64_t>(size));
        if (const auto ftime = fs::last_write_time(actual, ec); !ec) {
            // C++ file_clock -> time_t 的粗略换算；归档只需可读时间。
            const auto sys = std::chrono::system_clock::now()
                + (ftime - fs::file_time_type::clock::now());
            archive_entry_set_mtime(entry, std::chrono::system_clock::to_time_t(sys), 0);
        }

        if (archive_write_header(a, entry) != ARCHIVE_OK) {
            error = "写入条目失败：" + storedName + "：" + std::string(archive_error_string(a) ? archive_error_string(a) : "");
            archive_entry_free(entry);
            return false;
        }
        archive_entry_free(entry);
        if (isDir) return true;

        std::ifstream in(actual, std::ios::binary);
        if (!in) {
            error = "无法打开文件：" + WideToUtf8(actual.wstring());
            return false;
        }
        char buffer[64 * 1024];
        while (in) {
            in.read(buffer, sizeof(buffer));
            const auto got = in.gcount();
            if (got > 0) {
                if (archive_write_data(a, buffer, static_cast<size_t>(got)) < 0) {
                    error = "写入数据失败：" + storedName + "："
                        + std::string(archive_error_string(a) ? archive_error_string(a) : "");
                    return false;
                }
            }
        }
        return true;
    }

    bool AddTree(archive* a, const fs::path& actual, const std::string& storedName,
        const bool recurse, std::string& error) {
        if (!AddFileToArchive(a, actual, storedName, error)) return false;
        std::error_code ec;
        if (!recurse || !fs::is_directory(actual, ec)) return true;

        fs::recursive_directory_iterator it(actual, fs::directory_options::skip_permission_denied, ec);
        if (ec) {
            error = "无法遍历目录：" + WideToUtf8(actual.wstring()) + "：" + ec.message();
            return false;
        }
        for (const auto& entry : it) {
            const fs::path& child = entry.path();
            std::error_code sizeEc;
            if (entry.is_directory(sizeEc)) {
                const std::string childStored = storedName + "/"
                    + WideToUtf8(fs::relative(child, actual, sizeEc).generic_wstring());
                if (!AddFileToArchive(a, child, childStored, error)) return false;
                continue;
            }
            if (!entry.is_regular_file(sizeEc)) continue;
            const std::string childStored = storedName + "/"
                + WideToUtf8(fs::relative(child, actual, sizeEc).generic_wstring());
            if (!AddFileToArchive(a, child, childStored, error)) return false;
        }
        return true;
    }

} // namespace

extern "C" {

__declspec(dllexport) int TerminalArchive_Available(void) {
    return 1;
}

// 兼容旧接口：现在没有外部 bz.exe，返回内建后端标识。
__declspec(dllexport) void TerminalArchive_BandizipPath(char* buffer, const int bufferSize) {
    CopyText(buffer, bufferSize, "libarchive (built-in)");
}

__declspec(dllexport) int TerminalArchive_Create(
    const char* archivePath,
    const char* const* entries,
    const int entryCount,
    const char* password,
    const int level,
    const int format,
    const int recurse,
    char* output, const int outputSize,
    char* error, const int errorSize) {
    if (archivePath == nullptr || entryCount <= 0 || entries == nullptr) {
        CopyText(error, errorSize, "invalid arguments");
        return 0;
    }

    const fs::path archiveFile(Utf8ToWide(archivePath));
    Terminal::Archive::Format fmt = ToFormat(format);
    if (fmt == Terminal::Archive::Format::Auto) fmt = DetectFormat(archiveFile);
    if (fmt == Terminal::Archive::Format::Auto) {
        CopyText(error, errorSize, "无法从扩展名判断格式，请显式指定 zip 或 7z");
        return 0;
    }
    if (fmt == Terminal::Archive::Format::Rar) {
        CopyText(error, errorSize, "libarchive 后端不支持创建 RAR，只支持解压 RAR");
        return 0;
    }

    std::vector<fs::path> inputs;
    for (int i = 0; i < entryCount; ++i) {
        if (entries[i] == nullptr || *entries[i] == '\0') continue;
        inputs.emplace_back(Utf8ToWide(entries[i]));
    }
    if (inputs.empty()) {
        CopyText(error, errorSize, "没有可压缩的条目");
        return 0;
    }

    archive* a = archive_write_new();
    if (a == nullptr) {
        CopyText(error, errorSize, "archive_write_new 失败");
        return 0;
    }
    std::string failure;

    if (fmt == Terminal::Archive::Format::Zip) {
        if (archive_write_set_format_zip(a) != ARCHIVE_OK) {
            failure = "无法设置 ZIP 写入器：" + std::string(archive_error_string(a) ? archive_error_string(a) : "");
        }
    } else {
        if (archive_write_set_format_7zip(a) != ARCHIVE_OK) {
            failure = "无法设置 7z 写入器：" + std::string(archive_error_string(a) ? archive_error_string(a) : "");
        }
    }

    if (failure.empty()) {
        if (archive_write_add_filter_none(a) != ARCHIVE_OK) {
            failure = "无法设置无过滤写入：" + std::string(archive_error_string(a) ? archive_error_string(a) : "");
        }
    }

    const std::string pass = password != nullptr ? password : "";
    if (failure.empty() && !pass.empty()) {
        if (fmt == Terminal::Archive::Format::Zip) {
            // libarchive 的 ZIP 写加密依赖系统 crypto；失败时绝不能静默写出明文。
            if (archive_write_set_options(a, "zip:encryption=zipcrypt") != ARCHIVE_OK) {
                failure = "当前后端不支持 ZIP 密码（系统 crypto 不可用）";
            } else if (archive_write_set_passphrase(a, pass.c_str()) != ARCHIVE_OK) {
                failure = "无法设置 ZIP 密码：" + std::string(archive_error_string(a) ? archive_error_string(a) : "");
            }
        } else {
            failure = "libarchive 的 7z 写入不支持密码；请改用 ZIP 或先用无密码 7z";
        }
    }

    if (failure.empty()) {
        const int lvl = std::clamp(level, 0, 9);
        if (fmt == Terminal::Archive::Format::Zip) {
            const std::string option = "zip:compression-level=" + std::to_string(lvl);
            // 某些 libarchive 版本对 level 只给 WARN；别把它当成致命错误。
            archive_write_set_options(a, option.c_str());
        } else {
            const std::string option = "7zip:compression-level=" + std::to_string(lvl);
            archive_write_set_options(a, option.c_str());
        }
    }

    if (failure.empty()) {
        if (archive_write_open_filename_w(a, archiveFile.c_str()) != ARCHIVE_OK) {
            failure = "无法创建压缩包：" + WideToUtf8(archiveFile.wstring()) + "："
                + std::string(archive_error_string(a) ? archive_error_string(a) : "");
        }
    }

    if (!failure.empty()) {
        CopyText(error, errorSize, failure);
        archive_write_free(a);
        return 0;
    }

    bool ok = true;
    for (const auto& input : inputs) {
        std::error_code ec;
        const fs::path actual = fs::absolute(input, ec);
        if (ec || !fs::exists(actual, ec)) {
            failure = "条目不存在：" + WideToUtf8(input.wstring());
            ok = false;
            break;
        }
        // 绝对路径只存 basename；相对路径保留用户给的层级。
        const fs::path storedBase = input.is_absolute() ? input.filename() : input.lexically_normal();
        const std::string stored = WideToUtf8(storedBase.generic_wstring());
        if (!AddTree(a, actual, stored, recurse != 0, failure)) {
            ok = false;
            break;
        }
    }

    if (archive_write_close(a) != ARCHIVE_OK && ok) {
        failure = "关闭压缩包失败：" + std::string(archive_error_string(a) ? archive_error_string(a) : "");
        ok = false;
    }
    archive_write_free(a);

    if (!ok) {
        std::error_code ec;
        fs::remove(archiveFile, ec);
        CopyText(error, errorSize, failure);
        return 0;
    }

    std::vector<std::string> lines;
    lines.push_back("created " + WideToUtf8(archiveFile.filename().wstring()));
    lines.push_back("entries " + std::to_string(inputs.size()));
    CopyText(output, outputSize, JoinLines(lines));
    CopyText(error, errorSize, "");
    return 1;
}

__declspec(dllexport) int TerminalArchive_Extract(
    const char* archivePath,
    const char* outputDir,
    const char* password,
    const int overwrite,
    char* output, const int outputSize,
    char* error, const int errorSize) {
    if (archivePath == nullptr || outputDir == nullptr) {
        CopyText(error, errorSize, "invalid arguments");
        return 0;
    }

    const fs::path archiveFile(Utf8ToWide(archivePath));
    const fs::path root(Utf8ToWide(outputDir));
    std::string failure;
    if (!CreateDirectories(root, failure)) {
        CopyText(error, errorSize, failure);
        return 0;
    }

    const std::string pass = password != nullptr ? password : "";
    archive* a = OpenRead(archiveFile, pass, failure);
    if (a == nullptr) {
        CopyText(error, errorSize, failure);
        return 0;
    }

    std::vector<std::string> notes;
    archive_entry* entry = nullptr;
    int count = 0;
    bool ok = true;
    int readStatus = ARCHIVE_OK;
    while ((readStatus = archive_read_next_header(a, &entry)) == ARCHIVE_OK) {
        std::string name = archive_entry_pathname_utf8(entry)
            ? archive_entry_pathname_utf8(entry)
            : (archive_entry_pathname(entry) ? archive_entry_pathname(entry) : "");
        fs::path relative;
        if (!SafeEntryPath(name, relative)) {
            notes.push_back("skip unsafe path " + name);
            archive_read_data_skip(a);
            continue;
        }

        const fs::path target = root / relative;
        if (archive_entry_filetype(entry) == AE_IFDIR) {
            if (!CreateDirectories(target, failure)) {
                ok = false;
                break;
            }
            ++count;
            notes.push_back("dir " + WideToUtf8(relative.generic_wstring()));
            continue;
        }
        if (archive_entry_filetype(entry) != AE_IFREG) {
            notes.push_back("skip special " + WideToUtf8(relative.generic_wstring()));
            archive_read_data_skip(a);
            continue;
        }

        if (!WriteDataToFile(a, target, overwrite != 0, failure)) {
            ok = false;
            break;
        }
        ++count;
        notes.push_back("file " + WideToUtf8(relative.generic_wstring()));
    }

    if (ok && readStatus != ARCHIVE_EOF) {
        failure = "读取压缩包失败："
            + std::string(archive_error_string(a) ? archive_error_string(a) : "unknown error");
        ok = false;
    }

    archive_read_close(a);
    archive_read_free(a);

    if (!ok) {
        CopyText(error, errorSize, failure.empty() ? "解压失败" : failure);
        return 0;
    }

    CopyText(error, errorSize, "");
    CopyText(output, outputSize,
        "extracted " + std::to_string(count) + " entries to " + WideToUtf8(root.wstring())
        + (notes.empty() ? "" : "\n" + JoinLines(notes)));
    return 1;
}

__declspec(dllexport) int TerminalArchive_Test(
    const char* archivePath,
    const char* password,
    char* output, const int outputSize,
    char* error, const int errorSize) {
    if (archivePath == nullptr) {
        CopyText(error, errorSize, "invalid arguments");
        return 0;
    }
    const fs::path archiveFile(Utf8ToWide(archivePath));
    const std::string pass = password != nullptr ? password : "";
    std::string failure;
    archive* a = OpenRead(archiveFile, pass, failure);
    if (a == nullptr) {
        CopyText(error, errorSize, failure);
        return 0;
    }

    archive_entry* entry = nullptr;
    int count = 0;
    int readStatus = ARCHIVE_OK;
    char buffer[64 * 1024];
    while ((readStatus = archive_read_next_header(a, &entry)) == ARCHIVE_OK) {
        ++count;
        if (archive_entry_filetype(entry) != AE_IFREG) {
            archive_read_data_skip(a);
            continue;
        }
        for (;;) {
            const ssize_t got = archive_read_data(a, buffer, sizeof(buffer));
            if (got == 0) break;
            if (got < 0) {
                failure = "完整性测试失败：" + std::string(
                    archive_error_string(a) ? archive_error_string(a) : "unknown error");
                readStatus = ARCHIVE_FATAL;
                break;
            }
        }
        if (readStatus == ARCHIVE_FATAL) break;
    }
    if (readStatus != ARCHIVE_EOF && failure.empty()) {
        failure = "完整性测试失败：" + std::string(
            archive_error_string(a) ? archive_error_string(a) : "unknown error");
    }
    archive_read_close(a);
    archive_read_free(a);

    if (!failure.empty()) {
        CopyText(error, errorSize, failure);
        return 0;
    }
    CopyText(error, errorSize, "");
    CopyText(output, outputSize, "tested " + std::to_string(count) + " entries");
    return 1;
}

__declspec(dllexport) int TerminalArchive_List(
    const char* archivePath,
    const char* password,
    char* output, const int outputSize,
    char* error, const int errorSize) {
    if (archivePath == nullptr) {
        CopyText(error, errorSize, "invalid arguments");
        return 0;
    }
    const fs::path archiveFile(Utf8ToWide(archivePath));
    const std::string pass = password != nullptr ? password : "";
    std::string failure;
    archive* a = OpenRead(archiveFile, pass, failure);
    if (a == nullptr) {
        CopyText(error, errorSize, failure);
        return 0;
    }

    std::vector<std::string> lines;
    archive_entry* entry = nullptr;
    int readStatus = ARCHIVE_OK;
    while ((readStatus = archive_read_next_header(a, &entry)) == ARCHIVE_OK) {
        std::string name = archive_entry_pathname_utf8(entry)
            ? archive_entry_pathname_utf8(entry)
            : (archive_entry_pathname(entry) ? archive_entry_pathname(entry) : "");
        const bool isDir = archive_entry_filetype(entry) == AE_IFDIR;
        const auto size = archive_entry_size(entry);
        lines.push_back(name + "\t" + (isDir ? "[dir]" : std::to_string(size)));
        archive_read_data_skip(a);
    }
    if (readStatus != ARCHIVE_EOF) {
        CopyText(error, errorSize, "读取压缩包失败："
            + std::string(archive_error_string(a) ? archive_error_string(a) : "unknown error"));
        archive_read_close(a);
        archive_read_free(a);
        return 0;
    }
    archive_read_close(a);
    archive_read_free(a);

    CopyText(error, errorSize, "");
    CopyText(output, outputSize, JoinLines(lines));
    return 1;
}

} // extern "C"
