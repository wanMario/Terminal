//
// Terminal / src / TerminalUI.cpp
// 静态库装配单元：把聚合头编译一次，并提供一个稳定的库符号。
//
// 说明：本库的实现仍然以 inline / 模板形式写在头文件里；使用方需要
// 同时包含 Terminal 及依赖目录，并链接 build/lib/libTerminalUI.a。
// 这个 .cpp 不复制实现，只负责生成归档并让链接器有确定的符号可拉取。
//

#include "Terminal.hpp"

namespace Terminal {
    const char* Version() noexcept {
        return "TerminalUI 0.1.0";
    }
}
