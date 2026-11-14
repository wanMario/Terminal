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

add_executable(terminal_demo examples/demo.cpp)
target_link_libraries(terminal_demo PRIVATE TerminalUI)

# MinGW 运行时静态链接：避免系统 PATH 中其他 MinGW（例如 CLion 自带旧版）
# 抢先加载旧 libstdc++ / libgcc / libwinpthread，导致双击 exe 时报“找不到入口点”。
if (WIN32 AND NOT MSVC)
    target_link_options(terminal_demo PRIVATE -static)
endif()

# Windows 下额外依赖：kernel32（控制台 API）+ user32（原始输入 / 窗口消息 / 像素坐标换算）
if (WIN32)
    target_link_libraries(TerminalUI PUBLIC kernel32 user32)
endif()

enable_testing()
add_test(NAME selftest COMMAND terminal_demo --selftest)
