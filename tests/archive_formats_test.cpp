//
// Terminal / tests / archive_formats_test.cpp
// 验证 libarchive 后端：ZIP（含密码）与 7z 的创建 / 列表 / 解压。
//

#include <windows.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {
    using CreateFn = int (*)(const char*, const char* const*, int, const char*, int, int, int,
        char*, int, char*, int);
    using ExtractFn = int (*)(const char*, const char*, const char*, int, char*, int, char*, int);
    using ListFn = int (*)(const char*, const char*, char*, int, char*, int);

    template <typename T>
    T Resolve(const HMODULE module, const char* name) {
        return reinterpret_cast<T>(GetProcAddress(module, name));
    }

    bool WriteText(const std::filesystem::path& path, const std::string& text) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        return out.good();
    }

    std::string ReadText(const std::filesystem::path& path) {
        std::ifstream in(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    struct Api {
        CreateFn create{};
        ExtractFn extract{};
        ListFn list{};
    };

    bool RoundTrip(const Api& api, const std::filesystem::path& root,
        const std::filesystem::path& source, const char* archiveName,
        const int format, const char* password) {
        const auto archive = root / archiveName;
        const auto unpacked = root / (std::string("out-") + archiveName);
        std::filesystem::remove(archive);
        std::filesystem::remove_all(unpacked);
        std::filesystem::create_directories(unpacked);

        char output[4096]{};
        char error[512]{};

        const std::string sourceName = source.filename().string();
        const char* entries[] = { sourceName.c_str() };
        const int created = api.create(archive.string().c_str(), entries, 1,
            password, 5, format, 0,
            output, static_cast<int>(sizeof(output)), error, static_cast<int>(sizeof(error)));
        if (created == 0) {
            std::cerr << archiveName << " create failed: " << error << "\n";
            return false;
        }

        const int listed = api.list(archive.string().c_str(), password,
            output, static_cast<int>(sizeof(output)), error, static_cast<int>(sizeof(error)));
        if (listed == 0 || std::string(output).find("lyrics.txt") == std::string::npos) {
            std::cerr << archiveName << " list failed: " << error << "\n";
            return false;
        }

        const int extracted = api.extract(archive.string().c_str(), unpacked.string().c_str(),
            password, 1, output, static_cast<int>(sizeof(output)), error, static_cast<int>(sizeof(error)));
        if (extracted == 0) {
            std::cerr << archiveName << " extract failed: " << error << "\n";
            return false;
        }

        const auto actual = unpacked / source.filename();
        if (ReadText(actual) != ReadText(source)) {
            std::cerr << archiveName << " roundtrip mismatch\n";
            return false;
        }

        if (password != nullptr) {
            const auto withoutPassword = root / (std::string("nopass-") + archiveName);
            std::filesystem::remove_all(withoutPassword);
            std::filesystem::create_directories(withoutPassword);
            const int opened = api.extract(archive.string().c_str(), withoutPassword.string().c_str(),
                nullptr, 1, output, static_cast<int>(sizeof(output)),
                error, static_cast<int>(sizeof(error)));
            if (opened != 0) {
                std::cerr << archiveName << " opened without password; create did not encrypt\n";
                return false;
            }
        }
        return true;
    }
}

int main(const int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: archive_formats_test <TerminalArchive.dll> [rar-sample]\n";
        return 2;
    }
    const HMODULE module = LoadLibraryA(argv[1]);
    if (module == nullptr) {
        std::cerr << "LoadLibrary failed: " << GetLastError() << "\n";
        return 3;
    }

    Api api;
    api.create = Resolve<CreateFn>(module, "TerminalArchive_Create");
    api.extract = Resolve<ExtractFn>(module, "TerminalArchive_Extract");
    api.list = Resolve<ListFn>(module, "TerminalArchive_List");
    if (!api.create || !api.extract || !api.list) {
        std::cerr << "missing exported C ABI\n";
        FreeLibrary(module);
        return 4;
    }

    const auto root = std::filesystem::temp_directory_path() / "terminal_archive_formats";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const auto source = root / "lyrics.txt";
    const std::string payload = "00:01.20 | terminal archive\n00:03.40 | libarchive backend\n";
    if (!WriteText(source, payload)) {
        std::cerr << "failed to create source\n";
        FreeLibrary(module);
        return 5;
    }

    bool ok = true;
    const auto originalCwd = std::filesystem::current_path();
    std::filesystem::current_path(root);
    ok = RoundTrip(api, root, source, "plain.zip", 1, nullptr) && ok;
    ok = RoundTrip(api, root, source, "secret.zip", 1, "P@ss123") && ok;
    ok = RoundTrip(api, root, source, "plain.7z", 2, nullptr) && ok;

    // 可选：用官方 Ark SDK 的 rar.rar 做一次解压验证。
    if (argc >= 3 && std::filesystem::exists(argv[2])) {
        const auto out = root / "rar-out";
        std::filesystem::create_directories(out);
        char output[4096]{};
        char error[512]{};
        const int extracted = api.extract(argv[2], out.string().c_str(), nullptr, 1,
            output, static_cast<int>(sizeof(output)), error, static_cast<int>(sizeof(error)));
        if (extracted == 0) {
            std::cerr << "rar extract failed: " << error << "\n";
            ok = false;
        } else {
            std::cout << "rar extract ok: " << output << "\n";
        }
    }

    std::filesystem::current_path(originalCwd);
    std::filesystem::remove_all(root);
    FreeLibrary(module);
    if (!ok) return 1;
    std::cout << "zip/7z/password roundtrip ok\n";
    return 0;
}
