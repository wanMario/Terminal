cmake_minimum_required(VERSION 3.16)
project(TerminalUI LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 静态库：主体仍是 header-only；src/TerminalUI.cpp 负责 Version() 锚点。
# JSON 统一由 ToolHead/DataStructure/Json（JsonCore）承载，需要把它的实现单元一起归档。
# 依赖同仓库的 Math（ToolHead/Math）；不再依赖 agl/wjson。
add_library(TerminalUI STATIC
    src/TerminalUI.cpp
    ../DataStructure/Json/JsonCore.cpp
    ../DataStructure/Json/Number.cpp
)
target_include_directories(TerminalUI PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..        # ToolHead  (Math/...)
    ${CMAKE_CURRENT_SOURCE_DIR}/../..      # Libraries
    ${CMAKE_CURRENT_SOURCE_DIR}            # Terminal  (Core/... Utils/... Components/...)
    ${CMAKE_CURRENT_SOURCE_DIR}/../DataStructure/Json  # JsonCore
)
target_compile_features(TerminalUI PUBLIC cxx_std_20)
if (MSVC)
    target_compile_options(TerminalUI PUBLIC /utf-8 /W4)
else()
    target_compile_options(TerminalUI PUBLIC -Wall -Wextra)
endif()

set_target_properties(TerminalUI PROPERTIES
    OUTPUT_NAME TerminalUI
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

# 方案 D：独立的 TerminalArchive.dll，导出稳定 C ABI，不暴露 C++ 运行时 ABI。
# 后端为静态链接的 libarchive：进程内完成 ZIP/7z 创建、ZIP/7z/RAR 解压，不再启动 bz.exe。
add_library(TerminalArchive SHARED src/TerminalArchiveDll.cpp)
target_include_directories(TerminalArchive PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libarchive/include
)
target_compile_definitions(TerminalArchive PRIVATE LIBARCHIVE_STATIC)
target_compile_features(TerminalArchive PRIVATE cxx_std_20)
if (MSVC)
    target_compile_options(TerminalArchive PRIVATE /utf-8 /W4)
else()
    target_compile_options(TerminalArchive PRIVATE -Wall -Wextra)
endif()
if (WIN32)
    target_link_libraries(TerminalArchive PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libarchive/lib/libarchive.a"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libarchive/lib/libzlibstatic.a"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libarchive/lib/liblzma.a"
        kernel32 bcrypt xmllite
    )
endif()
if (WIN32 AND NOT MSVC)
    # DLL 不依赖外置 libstdc++ / libgcc / libwinpthread。
    target_link_options(TerminalArchive PRIVATE -static -static-libgcc -static-libstdc++)
endif()
set_target_properties(TerminalArchive PROPERTIES
    OUTPUT_NAME TerminalArchive
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# libarchive 静态后端已经替代 Bandizip 资源；不再嵌入 bz.exe。

# 演示程序：按功能拆成独立目标，每个都可以单独运行。
#   terminal_demo       样式展示（边框 / 排布 / TStrFmt / 滚动 / 两个 Window）
#   table_editor_demo   表格编辑器
#   music_player_demo   音乐播放器 + 页面内 LRC 编辑
#   zip_tool_demo       压缩包工具（Bandizip 后端）
set(TERMINAL_DEMO_TARGETS
    terminal_demo
    table_editor_demo
    music_player_demo
    zip_tool_demo
)
add_executable(terminal_demo examples/demo.cpp)
target_link_libraries(terminal_demo PRIVATE TerminalUI)
foreach(demo_target IN LISTS TERMINAL_DEMO_TARGETS)
    if (demo_target STREQUAL "terminal_demo")
        continue()
    endif()
    add_executable(${demo_target} examples/${demo_target}.cpp)
    target_link_libraries(${demo_target} PRIVATE TerminalUI)
endforeach()

# 静态自检独立成 CTest 目标，不再挂在 demo 的 --selftest 上。
add_executable(terminal_selftest tests/terminal_selftest.cpp)
target_link_libraries(terminal_selftest PRIVATE TerminalUI)

# MinGW 运行时静态链接：避免系统 PATH 中其他 MinGW（例如 CLion 自带旧版）
# 抢先加载旧 libstdc++ / libgcc / libwinpthread，导致双击 exe 时报“找不到入口点”。
if (WIN32 AND NOT MSVC)
    foreach(demo_target IN LISTS TERMINAL_DEMO_TARGETS)
        target_link_options(${demo_target} PRIVATE -static)
    endforeach()
    target_link_options(terminal_selftest PRIVATE -static)
endif()

# Windows 下额外依赖：kernel32（控制台 API）+ user32（原始输入 / 窗口消息 / 像素坐标换算）
# 文件选择器使用 IFileOpenDialog：ole32/uuid 提供 COM，shell32 提供 SHCreateItemFromParsingName。
if (WIN32)
    target_link_libraries(TerminalUI PUBLIC kernel32 user32 ole32 uuid shell32)
endif()

enable_testing()
add_test(NAME terminal_selftest COMMAND terminal_selftest)
# 验证 DLL 能加载并导出 C ABI。
add_executable(archive_dll_test tests/archive_dll_test.cpp)
target_link_libraries(archive_dll_test PRIVATE kernel32)
add_dependencies(archive_dll_test TerminalArchive)
add_test(NAME archive_dll_api COMMAND archive_dll_test $<TARGET_FILE:TerminalArchive>)
set_tests_properties(archive_dll_api PROPERTIES ENVIRONMENT "TERMINAL_ARCHIVE_EMBED=1")
# 覆盖 ZIP 密码与 7z，确认后端不是只在 ZIP 明文路径上能跑。
add_executable(archive_formats_test tests/archive_formats_test.cpp)
target_link_libraries(archive_formats_test PRIVATE kernel32)
add_dependencies(archive_formats_test TerminalArchive)
add_test(NAME archive_formats_api COMMAND archive_formats_test $<TARGET_FILE:TerminalArchive>)
# 每个 demo 的 --frame 也纳入 CTest：至少保证构建产物能完成一次整屏渲染。
foreach(demo_target IN LISTS TERMINAL_DEMO_TARGETS)
    add_test(NAME ${demo_target}_frame COMMAND ${demo_target} --frame)
endforeach()
