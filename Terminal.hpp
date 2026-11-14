//
// Terminal / Terminal.hpp
// 一揽子聚合头：include 它即可使用整套控制台 UI 库
//
//   #include <ToolHead/Terminal/Terminal.hpp>
//   （本文件所在目录与 ToolHead 同级配置见 CMakeLists.txt 的 include 路径）
//

#pragma once

// ---- Utils：字符 / 颜色 / 度量 / 边框字符 / 主题 ---------------------
#include "Utils/Outputs.hpp"      // 原控制台基础能力（句柄、光标、16 色属性位）
#include "Utils/TerminalChar.hpp" // SKey + xChar + 按键表
#include "Utils/Color.hpp"        // Color / Style / Colors / ANSI 真彩
#include "Utils/Utf8.hpp"         // UTF-8 编解码 + 显示宽度（CJK 双宽）
#include "Utils/BoxChars.hpp"     // 单线 / 双线 / 圆角 / 粗线 / 虚线边框字符
#include "Utils/Theme.hpp"        // Dark / Light / Neon 配色主题

// ---- Core：缓冲 / 元素基类 / 布局 / 输入 / 焦点 / 组件管理器 ---------
#include "Core/TerminalBuffer.hpp"  // ConsoleBackBuffer：双缓冲 + 裁剪 + 脏矩形
#include "Core/UI.hpp"              // UI 基类 + LayoutSpec + Flow/Align/LayoutPos
#include "Core/Layout.hpp"          // Row/Column 弹性布局求解
#include "Core/Input.hpp"           // 键盘读取（含 UTF-8 多字节拼装）
#include "Core/TerminalMouse.hpp"   // 鼠标事件（Win32 记录 + 命中测试路由）
#include "Core/TerminalKeyFocus.hpp"// FocusManager：Tab 焦点轮转
#include "Core/TerminalJson.hpp"    // Json 适配层（底层 JsonCore）
#include "Core/TerminalMessage.hpp" // MessageQueue：交互回调投递 / 主循环按对象处理
#include "Core/TerminalAudio.hpp"   // AudioPlayer：MF 优先 / MCI 兜底、暂停、倍速
#include "Core/TerminalComponentManager.hpp" // 类型注册表 + Json -> UI 树

// ---- Components：元件 ------------------------------------------------
#include "Components/TerminalText.hpp"      // Text      静态文字
#include "Components/TerminalButton.hpp"    // Button    按钮
#include "Components/TerminalTextInput.hpp" // TextInput 输入框（含 Legacy::InputV1）
#include "Components/TerminalSlider.hpp"    // Slider    滑杆
#include "Components/TerminalScrollView.hpp"// ScrollView 滚动容器（竖向滚动条 + 滚轮）
#include "Components/TerminalImage.hpp"     // UIImage   半格像素图片（PNG/JPG，WIC 解码）
#include "Components/TerminalBorder.hpp"    // Border    边框容器
#include "Components/TerminalPanel.hpp"     // Panel     面板容器（标题 + 内边距）
#include "Components/TerminalDivider.hpp"   // Divider   水平分隔线（可带标题）
#include "Components/TerminalBadge.hpp"     // Badge     胶囊标签（带状态点）
#include "Components/TerminalSwitch.hpp"    // Switch    开关
#include "Components/TerminalChoiceGroup.hpp" // ChoiceGroup 单选/多选
#include "Components/TerminalWindow.hpp"    // Window    可拖动/关闭弹窗
#include "Components/TerminalPageHolder.hpp"// PageHolder 可嵌套页面 + 局部队列

namespace Terminal {
    // 静态库锚点：声明在聚合头，定义在 src/TerminalUI.cpp。
    // 头文件库的模板/内联实现仍在头里，这个符号用于确认 libTerminalUI.a 被正确链接。
    const char* Version() noexcept;
}
