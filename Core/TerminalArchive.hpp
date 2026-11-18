//
// Terminal / Core / TerminalArchive.hpp
// 压缩包 API：.zip / .7z 创建，.zip / .7z / .rar 解压与列表。
//
// 方案 D：真正的无外部进程后端。所有编解码由随库分发的 libarchive 静态库完成，
// 由 TerminalArchive.dll 导出稳定 C ABI；本头文件只负责动态加载这个 DLL。
// 不依赖已安装 Bandizip，不释放 bz.exe，不启动 CreateProcessW 子进程。
//
// 分发时把 TerminalArchive.dll 放到 exe 同目录（构建目录默认就是如此）即可。
// 若 DLL 不存在，Available() 返回 false，各操作返回带 error 的失败结果。
//

#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "../Utils/Utf8.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Terminal {
    namespace Archive {

        enum class Format {
            Auto = 0,
            Zip,
            SevenZip,
            Rar
        };

        struct Options {
            std::string password{};       // 空 = 不加密
            bool overwrite{ true };       // 解压时是否覆盖已有文件
            int level{ 5 };               // 0(仅存储) - 9(最大压缩)
            bool recurse{ true };         // 创建时是否递归目录
            Format format{ Format::Auto };
        };

        struct Result {
            bool ok{ false };
            int exitCode{ -1 };
            std::string output{};         // 正常输出（列表/日志）
            std::string error{};          // 失败原因
        };

        namespace Detail {
            inline std::string WideToUtf8(const std::wstring& text) {
#ifdef _WIN32
                if (text.empty()) return {};
                const int n = WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                    static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
                if (n <= 0) return {};
                std::string out(static_cast<std::size_t>(n), '\0');
                WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                    out.data(), n, nullptr, nullptr);
                return out;
#else
                (void)text;
                return {};
#endif
            }

            inline std::string LowerExtension(const std::filesystem::path& path) {
                std::string ext = path.extension().string();
                for (auto& ch : ext) {
                    if (static_cast<unsigned char>(ch) < 0x80) {
                        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                    }
                }
                return ext;
            }

            inline Format InferFormat(const std::filesystem::path& path) {
                const std::string ext = LowerExtension(path);
                if (ext == ".zip" || ext == ".zipx") return Format::Zip;
                if (ext == ".7z") return Format::SevenZip;
                if (ext == ".rar") return Format::Rar;
                return Format::Auto;
            }

#ifdef _WIN32
            using AvailableFn = int (*)(void);
            using PathFn = void (*)(char*, int);
            using CreateFn = int (*)(const char*, const char* const*, int, const char*, int, int, int,
                char*, int, char*, int);
            using ExtractFn = int (*)(const char*, const char*, const char*, int,
                char*, int, char*, int);
            using ListFn = int (*)(const char*, const char*, char*, int, char*, int);

            // GetProcAddress 返回 FARPROC；直接 reinterpret_cast 会触发
            // -Wcast-function-type。用同尺寸 memcpy 转换，避免零 warning 被破坏。
            template <typename T>
            T ResolveProc(const HMODULE module, const char* name) {
                FARPROC raw = GetProcAddress(module, name);
                T fn = nullptr;
                static_assert(sizeof(fn) == sizeof(raw), "function pointer size mismatch");
                std::memcpy(&fn, &raw, sizeof(fn));
                return fn;
            }

            struct DllApi {
                HMODULE module{ nullptr };
                AvailableFn available{};
                PathFn path{};
                CreateFn create{};
                ExtractFn extract{};
                ListFn list{};
                ListFn test{};            // 与 List 相同签名

                [[nodiscard]] bool ready() const {
                    return module != nullptr && available != nullptr && create != nullptr
                        && extract != nullptr && list != nullptr && test != nullptr;
                }
            };

            inline HMODULE LoadBackend() {
                if (const HMODULE loaded = GetModuleHandleW(L"TerminalArchive.dll")) return loaded;

                wchar_t exePath[32767]{};
                const DWORD length = GetModuleFileNameW(nullptr, exePath, 32767);
                if (length > 0 && length < 32767) {
                    std::filesystem::path candidate(exePath);
                    candidate.replace_filename(L"TerminalArchive.dll");
                    if (const HMODULE module = LoadLibraryExW(candidate.c_str(), nullptr,
                            LOAD_WITH_ALTERED_SEARCH_PATH)) {
                        return module;
                    }
                }
                return LoadLibraryW(L"TerminalArchive.dll");
            }

            inline const DllApi& Api() {
                static DllApi api = [] {
                    DllApi found;
                    found.module = LoadBackend();
                    if (found.module != nullptr) {
                        found.available = ResolveProc<AvailableFn>(
                            found.module, "TerminalArchive_Available");
                        found.path = ResolveProc<PathFn>(
                            found.module, "TerminalArchive_BandizipPath");
                        found.create = ResolveProc<CreateFn>(
                            found.module, "TerminalArchive_Create");
                        found.extract = ResolveProc<ExtractFn>(
                            found.module, "TerminalArchive_Extract");
                        found.list = ResolveProc<ListFn>(
                            found.module, "TerminalArchive_List");
                        found.test = ResolveProc<ListFn>(
                            found.module, "TerminalArchive_Test");
                    }
                    return found;
                    }();
                return api;
            }
#endif

            inline void CopyBuffer(const char* buffer, std::string& target) {
                target = buffer != nullptr ? buffer : "";
            }
        } // namespace Detail

        // 兼容旧调用名：返回内建后端标识；DLL 不可用时返回空。
        inline std::string BandizipPath() {
#ifdef _WIN32
            const auto& api = Detail::Api();
            if (!api.ready() || api.path == nullptr) return {};
            char buffer[256]{};
            api.path(buffer, static_cast<int>(sizeof(buffer)));
            return buffer;
#else
            return {};
#endif
        }

        inline bool Available() {
#ifdef _WIN32
            const auto& api = Detail::Api();
            if (!api.ready()) return false;
            return api.available() != 0;
#else
            return false;
#endif
        }

        inline bool IsArchivePath(const std::filesystem::path& path) {
            const std::string ext = Detail::LowerExtension(path);
            return ext == ".zip" || ext == ".zipx" || ext == ".7z" || ext == ".rar";
        }

        inline Format FormatOf(const std::filesystem::path& path) {
            return Detail::InferFormat(path);
        }

        // 解压到 destination。空 destination = 压缩包所在目录。
        inline Result Extract(const std::string& archivePath, const std::string& destination,
            const Options& options = {}) {
            Result result;
#ifdef _WIN32
            const auto& api = Detail::Api();
            if (!api.ready()) {
                result.error = "TerminalArchive.dll 不可用；请把它放到 exe 同目录";
                return result;
            }

            const std::filesystem::path archive = std::filesystem::path(Utf8::ToWide(archivePath));
            std::error_code ec;
            if (!std::filesystem::is_regular_file(archive, ec)) {
                result.error = "压缩包不存在：" + archivePath;
                return result;
            }

            std::filesystem::path target = destination.empty()
                ? archive.parent_path()
                : std::filesystem::path(Utf8::ToWide(destination));
            if (target.empty()) target = std::filesystem::current_path();
            std::filesystem::create_directories(target, ec);
            if (ec) {
                result.error = "无法创建解压目录：" + ec.message();
                return result;
            }

            char output[64 * 1024]{};
            char error[4096]{};
            const int ret = api.extract(archivePath.c_str(),
                Detail::WideToUtf8(target.wstring()).c_str(),
                options.password.empty() ? nullptr : options.password.c_str(),
                options.overwrite ? 1 : 0,
                output, static_cast<int>(sizeof(output)), error, static_cast<int>(sizeof(error)));
            result.ok = ret != 0;
            result.exitCode = ret != 0 ? 0 : 1;
            Detail::CopyBuffer(output, result.output);
            Detail::CopyBuffer(error, result.error);
            return result;
#else
            (void)archivePath;
            (void)destination;
            (void)options;
            result.error = "Archive 仅实现 Windows";
            return result;
#endif
        }

        // 创建压缩包。entries 可以是文件或目录；目录按 options.recurse 递归。
        // .rar 不能创建；libarchive 的 7z 写入不支持密码，会明确返回失败。
        inline Result Create(const std::string& archivePath, const std::vector<std::string>& entries,
            const Options& options = {}) {
            Result result;
#ifdef _WIN32
            const auto& api = Detail::Api();
            if (!api.ready()) {
                result.error = "TerminalArchive.dll 不可用；请把它放到 exe 同目录";
                return result;
            }
            if (entries.empty()) {
                result.error = "没有要加入压缩包的文件";
                return result;
            }

            const std::filesystem::path archive = std::filesystem::path(Utf8::ToWide(archivePath));
            Format format = options.format;
            if (format == Format::Auto) format = Detail::InferFormat(archive);
            if (format == Format::Auto) {
                result.error = "无法从扩展名判断格式，请显式指定 zip 或 7z";
                return result;
            }
            if (format == Format::Rar) {
                result.error = "后端不支持创建 RAR，只支持解压 RAR";
                return result;
            }

            std::error_code ec;
            if (!archive.parent_path().empty()) {
                std::filesystem::create_directories(archive.parent_path(), ec);
            }
            if (ec) {
                result.error = "无法创建输出目录：" + ec.message();
                return result;
            }

            std::vector<const char*> pointers;
            pointers.reserve(entries.size());
            for (const auto& entry : entries) pointers.push_back(entry.c_str());

            char output[64 * 1024]{};
            char error[4096]{};
            const int ret = api.create(archivePath.c_str(), pointers.data(),
                static_cast<int>(pointers.size()),
                options.password.empty() ? nullptr : options.password.c_str(),
                std::clamp(options.level, 0, 9),
                static_cast<int>(format), options.recurse ? 1 : 0,
                output, static_cast<int>(sizeof(output)), error, static_cast<int>(sizeof(error)));
            result.ok = ret != 0;
            result.exitCode = ret != 0 ? 0 : 1;
            Detail::CopyBuffer(output, result.output);
            Detail::CopyBuffer(error, result.error);
            return result;
#else
            (void)archivePath;
            (void)entries;
            (void)options;
            result.error = "Archive 仅实现 Windows";
            return result;
#endif
        }

        // 列出压缩包内容；output 每行是“路径<TAB>大小”。
        inline Result List(const std::string& archivePath, const std::string& password = {}) {
            Result result;
#ifdef _WIN32
            const auto& api = Detail::Api();
            if (!api.ready()) {
                result.error = "TerminalArchive.dll 不可用；请把它放到 exe 同目录";
                return result;
            }
            const std::filesystem::path archive = std::filesystem::path(Utf8::ToWide(archivePath));
            std::error_code ec;
            if (!std::filesystem::is_regular_file(archive, ec)) {
                result.error = "压缩包不存在：" + archivePath;
                return result;
            }

            char output[64 * 1024]{};
            char error[4096]{};
            const int ret = api.list(archivePath.c_str(),
                password.empty() ? nullptr : password.c_str(),
                output, static_cast<int>(sizeof(output)), error, static_cast<int>(sizeof(error)));
            result.ok = ret != 0;
            result.exitCode = ret != 0 ? 0 : 1;
            Detail::CopyBuffer(output, result.output);
            Detail::CopyBuffer(error, result.error);
            return result;
#else
            (void)archivePath;
            (void)password;
            result.error = "Archive 仅实现 Windows";
            return result;
#endif
        }

        // 完整性测试：读出所有条目数据，交给 libarchive 校验 CRC。
        inline Result Test(const std::string& archivePath, const std::string& password = {}) {
            Result result;
#ifdef _WIN32
            const auto& api = Detail::Api();
            if (!api.ready()) {
                result.error = "TerminalArchive.dll 不可用；请把它放到 exe 同目录";
                return result;
            }
            const std::filesystem::path archive = std::filesystem::path(Utf8::ToWide(archivePath));
            std::error_code ec;
            if (!std::filesystem::is_regular_file(archive, ec)) {
                result.error = "压缩包不存在：" + archivePath;
                return result;
            }

            char output[64 * 1024]{};
            char error[4096]{};
            const int ret = api.test(archivePath.c_str(),
                password.empty() ? nullptr : password.c_str(),
                output, static_cast<int>(sizeof(output)), error, static_cast<int>(sizeof(error)));
            result.ok = ret != 0;
            result.exitCode = ret != 0 ? 0 : 1;
            Detail::CopyBuffer(output, result.output);
            Detail::CopyBuffer(error, result.error);
            return result;
#else
            (void)archivePath;
            (void)password;
            result.error = "Archive 仅实现 Windows";
            return result;
#endif
        }

    } // namespace Archive
} // namespace Terminal
