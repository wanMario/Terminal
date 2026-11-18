//
// Terminal / tests / archive_dll_test.cpp
// 验证 TerminalArchive.dll 可被 LoadLibrary 加载、暴露稳定的 C ABI，
// 并在内嵌后端下完成一次创建 / 列表 / 解压。
//

#include <windows.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {
    using AvailableFn = int (*)(void);
    using PathFn = void (*)(char*, int);
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
}

int main(const int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: archive_dll_test <TerminalArchive.dll>\n";
        return 2;
    }

    const HMODULE module = LoadLibraryA(argv[1]);
    if (module == nullptr) {
        std::cerr << "LoadLibrary failed: " << GetLastError() << "\n";
        return 3;
    }

    const auto available = Resolve<AvailableFn>(module, "TerminalArchive_Available");
    const auto path = Resolve<PathFn>(module, "TerminalArchive_BandizipPath");
    const auto create = Resolve<CreateFn>(module, "TerminalArchive_Create");
    const auto extract = Resolve<ExtractFn>(module, "TerminalArchive_Extract");
    const auto list = Resolve<ListFn>(module, "TerminalArchive_List");
    if (available == nullptr || path == nullptr || create == nullptr
        || extract == nullptr || list == nullptr) {
        std::cerr << "missing exported C ABI\n";
        FreeLibrary(module);
        return 4;
    }

    char bzPath[1024]{};
    path(bzPath, static_cast<int>(sizeof(bzPath)));
    std::cout << "available=" << available() << " bz=" << bzPath << "\n";

    char output[2048]{};
    char error[512]{};

    const auto root = std::filesystem::temp_directory_path() / "terminal_archive_dll_smoke";
    const auto source = root / "smoke.txt";
    const auto archive = root / "smoke.zip";
    const auto unpacked = root / "unpacked";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(unpacked);

    const std::string payload = "TerminalArchive embedded bz.exe smoke test\n";
    if (!WriteText(source, payload)) {
        std::cerr << "failed to create smoke source\n";
        FreeLibrary(module);
        return 5;
    }

    std::filesystem::current_path(root);
    const char* entries[] = { "smoke.txt" };
    const int created = create(archive.string().c_str(), entries, 1,
        nullptr, 1, 1, 0,
        output, static_cast<int>(sizeof(output)), error, static_cast<int>(sizeof(error)));
    if (created == 0) {
        std::cerr << "create failed: " << error << "\n";
        FreeLibrary(module);
        return 6;
    }

    const int listed = list(archive.string().c_str(), nullptr,
        output, static_cast<int>(sizeof(output)), error, static_cast<int>(sizeof(error)));
    if (listed == 0 || std::string(output).find("smoke.txt") == std::string::npos) {
        std::cerr << "list failed: " << error << "\n";
        FreeLibrary(module);
        return 7;
    }

    const int extracted = extract(archive.string().c_str(), unpacked.string().c_str(), nullptr, 1,
        output, static_cast<int>(sizeof(output)), error, static_cast<int>(sizeof(error)));
    if (extracted == 0) {
        std::cerr << "extract failed: " << error << "\n";
        FreeLibrary(module);
        return 8;
    }

    const std::string roundtrip = ReadText(unpacked / "smoke.txt");
    std::filesystem::current_path(std::filesystem::temp_directory_path());
    std::filesystem::remove_all(root);
    FreeLibrary(module);

    if (roundtrip != payload) {
        std::cerr << "roundtrip content mismatch\n";
        return 9;
    }
    std::cout << "create/list/extract ok\n";
    return 0;
}
