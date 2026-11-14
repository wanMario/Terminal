# Terminal —— Win32 控制台 UI 库（纯头文件）

模仿 HTML + CSS 盒模型的控制台界面引擎：DOM 树、布局、嵌套、焦点、主题配色，全部通过
`ConsoleBackBuffer` 双缓冲绘制，不直接写控制台，因此不闪烁、不撕裂，且能同时支持
**16 色属性**与 **24 位真彩**。

- 语言 / 标准：C++20，header-only（`.hpp`），无需编译库
- 平台：Windows（`WriteConsoleOutputW` / `_getch` / `SetConsoleMode`）
- 工具链验证：MinGW-W64 GCC 14.2.0（`C:\mingw64\bin\g++.exe`）、CMake 3.30.4（`MinGW Makefiles`）

---

## 目录结构

```
Terminal/
├── Terminal.hpp                 # 一揽子聚合头（include 它即可）
├── CMakeLists.txt               # STATIC 库 TerminalUI + terminal_demo 可执行 + ctest
├── Utils/
│   ├── Outputs.hpp              # 控制台句柄 / 光标 / 16 色 DWORD 常量（原有文件，保留）
│   ├── TerminalChar.hpp         # SKey 枚举 + xChar 按键事件 + 完整按键表
│   ├── Color.hpp                # Color / Style / Colors / ANSI 真彩输出
│   ├── Utf8.hpp                 # UTF-8 编解码 + 显示宽度（CJK/Emoji 双宽）
│   ├── BoxChars.hpp             # 单线/双线/圆角/粗线/虚线/ASCII 边框字符集
│   └── Theme.hpp                # Dark（GitHub Dark 真彩）/ Light / Neon 三套主题
├── Core/
│   ├── TerminalBuffer.hpp       # ConsoleBackBuffer：双缓冲 + 裁剪 + 脏矩形 + ANSI 导出
│   ├── UI.hpp                   # UI 基类：bounds/style/LayoutSpec/children/焦点/渲染
│   ├── Layout.hpp               # Row/Column 弹性布局求解（grow / gap / 交叉轴对齐）
│   ├── Input.hpp                # 按键泵：ClickAvailable 轮询 + UTF-8 多字节拼装
│   ├── TerminalMouse.hpp        # 鼠标：控制台记录 -> 事件 + 命中测试路由（悬停/捕获/拖拽）
│   ├── TerminalKeyFocus.hpp     # FocusManager：DFS 收集 + Tab 轮转 + 按键分发
│   ├── TerminalMessage.hpp      # Message + MessageQueue + LocalMessageQueue（sender / Json 负载）
│   ├── TerminalJson.hpp         # Json 适配层（底层 JsonCore，保留旧 Json API）
│   ├── TerminalAudio.hpp        # AudioPlayer（Media Foundation 优先，MCI 兜底；运行时加载）
│   └── TerminalComponentManager.hpp # 类型注册表 + Json -> UI 树（JsonCore）
├── Components/
│   ├── TerminalText.hpp         # Text      静态文字（多行、对齐、主题化）
│   ├── TerminalButton.hpp       # Button    按钮（文字/边框色可配、聚焦加粗、交互换 hot 边框色）
│   ├── TerminalTextInput.hpp    # TextInput 输入框（选区/撤销重做/剪贴板/多行/MENU 菜单）
│   ├── TerminalSlider.hpp       # Slider    滑杆（键盘步进、比例填充）
│   ├── TerminalScrollView.hpp   # ScrollView 滚动容器（竖向滚动条 + 滚轮 + 拖动）
│   ├── TerminalImage.hpp        # UIImage   半格像素图片（PNG/JPG，WIC 解码）
│   ├── TerminalBorder.hpp       # Border    边框容器（标题、让出边框的内容区）
│   ├── TerminalPanel.hpp        # Panel     面板容器（标题栏、内边距、方向）
│   ├── TerminalDivider.hpp      # Divider   分隔线（可带标题，`─ 标题 ────`）
│   ├── TerminalBadge.hpp        # Badge     胶囊标签（可带 ● 状态点）
│   ├── TerminalSwitch.hpp       # Switch    开关（ON/OFF，消息驱动）
│   ├── TerminalChoiceGroup.hpp  # ChoiceGroup 单选/多选（键盘 + 鼠标 + 消息）
│   ├── TerminalWindow.hpp       # Window    可拖标题栏/可关闭弹窗，内容区可排版
│   └── TerminalPageHolder.hpp   # PageHolder 可嵌套页面 + 每页独立局部队列
├── src/TerminalUI.cpp           # 静态库装配单元（提供 Terminal::Version() 符号）
├── examples/demo.cpp            # 演示 + 自检 + 交互 + 布局转储 + 性能基准
├── assets/terminal_logo.png     # demo 用的半格像素图标
├── preview.png                  # 80x30 整屏预览（含滚动列表与图片）
├── preview_focus.png            # 按钮三态预览（常态 / 聚焦 / 交互）
├── .vscode/                     # tasks / launch / c_cpp_properties / settings（MinGW 工具链）
└── README.md
```

---

## 编译

### CMake（推荐）

```powershell
cmake -S . -B build -G "MinGW Makefiles" `
      -DCMAKE_CXX_COMPILER=C:/mingw64/bin/g++.exe `
      -DCMAKE_MAKE_PROGRAM=C:/mingw64/bin/mingw32-make.exe
cmake --build build -j 4
cd build; ctest --output-on-failure
```

`CMakeLists.txt` 会把 `ToolHead/`（Math 依赖）、`Libraries/` 与
`ToolHead/DataStructure/Json`（JsonCore）加入 include 路径；`TerminalUI` 归档同时编入
`JsonCore.cpp` / `Number.cpp`，因此本目录可以独立构建。

### 静态库

`TerminalUI` 现在是 `STATIC` 目标，单独构建它即可得到归档：

```powershell
cmake --build build --target TerminalUI
# MinGW 产物：build/lib/libTerminalUI.a
# MSVC 产物： build/lib/TerminalUI.lib
```

使用方仍然需要包含头文件（本库的实现是 inline/模板，归档负责统一交付 `Terminal::Version()`
以及 JsonCore 的 `JsonCore.cpp / Number.cpp`）：

```powershell
g++ -std=c++20 -Wall -Wextra `
    -ID:/wan1/C++/Libraries/ToolHead -ID:/wan1/C++/Libraries -ID:/wan1/C++/Libraries/ToolHead/Terminal `
    -ID:/wan1/C++/Libraries/ToolHead/DataStructure/Json `
    app.cpp -Lbuild/lib -lTerminalUI -o app.exe
```

`-lTerminalUI` 要放在源文件/目标文件后面，静态链接才会正确解析。

### 直接用 g++

```powershell
g++ -std=c++20 -Wall -Wextra `
    -ID:/wan1/C++/Libraries/ToolHead -ID:/wan1/C++/Libraries -ID:/wan1/C++/Libraries/ToolHead/Terminal `
    -ID:/wan1/C++/Libraries/ToolHead/DataStructure/Json `
    examples/demo.cpp src/TerminalUI.cpp `
    ../DataStructure/Json/JsonCore.cpp ../DataStructure/Json/Number.cpp -o demo.exe
```

也可以改用静态库（先构建 `TerminalUI`）：

```powershell
g++ -std=c++20 -Wall -Wextra `
    -ID:/wan1/C++/Libraries/ToolHead -ID:/wan1/C++/Libraries -ID:/wan1/C++/Libraries/ToolHead/Terminal `
    -ID:/wan1/C++/Libraries/ToolHead/DataStructure/Json `
    examples/demo.cpp -Lbuild/lib -lTerminalUI -o demo.exe
```

`src/TerminalUI.cpp` 提供 `Terminal::Version()`；JsonCore 的 `.cpp` 提供解析/容器实现，
漏掉任一部分都会链接失败。

依赖只有同仓库的 `ToolHead/Math/Geometry/G2D`（`Point.hpp` / `Rect.hpp`）和
`ToolHead/DataStructure/Json`（JsonCore）。`UIImage` 的 PNG/JPG 解码通过运行时加载
`windowscodecs.dll` / `ole32.dll` 完成，音频后端通过运行时加载 `mfplay.dll` / `winmm.dll`，
所以 g++ 命令**不需要**额外写 `-lwindowscodecs -lole32 -lmfplay -lwinmm`。

---

## 用法

```cpp
#include "Terminal.hpp"
using namespace Terminal;

int main() {
    Theme::UseDark();

    auto root = std::make_shared<Panel>(Flow::Column);   // 根容器
    root->layout.Padding(1);
    root->SetBackgroundColor(Color::OfIndex(0));

    auto title = std::make_shared<Text>("Hello 控制台");
    title->SetStyle(Style(Color::Parse("#FF8C00"), Colors::Black).SetBold());

    auto ok = std::make_shared<Button>("确定");
    ok->OnClick([] { /* ... */ });

    root->AddChild(title);
    root->AddChild(ok);

    ConsoleBackBuffer buf = ConsoleBackBuffer::FromConsole();
    root->Arrange(Rect2Di{ 0, 0, buf.Width(), buf.Height() });   // 布局
    root->Mount();                                              // 挂载（收集焦点等）
    FocusManager::Instance().SetRoot(root.get());

    buf.Clear(Style(Theme::Current().text, Theme::Current().background));
    root->Render(buf);
    buf.Flush();               // 16 色：WriteConsoleOutputW
    // buf.FlushAnsi();        // 24 位真彩：ANSI 38;2 / 48;2
}
```

### 布局模型（对应 CSS）

| CSS | 这里的 API |
|---|---|
| `display:flex; flex-direction` | `ui.layout.FlowDir(Flow::Row / Flow::Column)` |
| `flex: N` | `ui.layout.Grow(N)` |
| `gap` | `ui.layout.Gap(n)` |
| `padding` / `margin` | `ui.layout.Padding(n)` / `Margin(n)`（可单边） |
| `width/height` | `ui.layout.Width(n)` / `Height(n)`（`-1` = 自适应） |
| `min-width/height` | `ui.layout.MinWidth/MinHeight` |
| `align-items` | `ui.layout.Align(Align::Start/Center/End/Stretch)` |

容器（`Panel`、`Border`、`UIComponentManager`）默认 `Align::Stretch`，子元素在交叉轴自动拉满；
`Text` / `Button` / `Slider` 等叶子按内容自适应。`Border` 的 `ContentBounds()` 会先让出 1 格边框，
`Panel` 有标题时让出 1 行标题栏。

### 元件一览

| 元件 | 关键成员 / 方法 | 键盘 | 鼠标 |
|---|---|---|---|
| `Text` | `SetText / GetText / SetLines / SetAlign / SetStyle` | — | — |
| `Button` | `SetLabel / OnClick / SetFrame / SetTextColor / SetBorderColor / SetHotBorderColor / SetColors`；聚焦加粗、交互换边框色 | Enter、Space | 悬停高亮、点击触发 |
| `TextInput` | `SetLabel / SetValue / GetValue / SetPlaceholder / SetPrefix / SetPassword / SetMaxLength / SetMultiline / SetMaxLines / SetOverwrite / SetSelectionMode / OnSubmit / OnChange` | 方向键、Home/End、退格、Delete、UTF-8 输入、`Insert` 插入/替换、`Ctrl+C/V/X/Z/Y/A`、`Shift+方向键` 扩选 | 点击定位光标、拖选 |
| `Slider` | `SetRange / SetValue / SetStep / OnChanged`、`Ratio()`、`Track()`、`SetByPosition(cellX)` | ←→、Home/End、PageUp/PageDown、`h`/`l` | 点击跳值、按住拖动，**亚格精度**（每像素可改值） |
| `ScrollView` | `SetScroll / ScrollBy / Page / SetWheelStep / SetShowScrollbar`、`MaxScroll()` | PageUp/PageDown、Home/End | 滚轮、点/拖右侧竖向滚动条 |
| `UIImage` | `SetImage(path) / SetPixels(w,h,rgba) / SetFit(Contain·Cover·Stretch) / Clear()` | — | — |
| `Border` | `SetChars / SetTitle / SetTitleAlign / SetBorderColor` | — | — |
| `Panel` | `SetFlow / SetGap / SetPadding / SetTitle / SetFill` | — | — |
| `Divider` | `SetTitle / SetChars / SetColors(line, title)`，无标题时整行横线 | — | — |
| `Badge` | `SetText / SetDot / SetColors(bg, fg)`，宽度 = 文本 + 圆点/内边距 | — | — |

旧名兼容：`UIText`、`UIButton`、`UITextInput`、`UISlider`、`UIScrollView`、`UIImageView`、`UIBorder`、`UIPanel`、
`UIDivider`、`UIBadge` 均为 `using` 别名；
旧的 `Terminal::InputV1` 已下沉到 `Terminal::Legacy::InputV1`。

### 色彩

- **16 色**：`Colors::` 具名调色板（`Red`/`Gray`/`BrightBlue`…），`Color::OfIndex(i)`，
  `Style::ToAttr()` 产出 Win32 属性位，`Flush()` 走 `WriteConsoleOutputW`。
- **24 位真彩**：`Color::Parse("#FF8C00")`、`Color::OfRGB(r,g,b)`、`Color::Blend/Gradient`；
  `buf.SetTrueColor(true)` 后 `ToAnsiText()` / `FlushAnsi()` 输出 `ESC[38;2;r;g;b`、
  `ESC[48;2;r;g;b`（`EnableVirtualTerminal` 已在内部调用）。
- **主题**：`Theme::UseDark() / UseLight() / UseNeon()` 或 `ApplyByName("light")`，
  元件默认从 `Theme::Current()` 取色，也可以用 `Style` 单独覆盖。
- **默认深色是 GitHub Dark 真彩色板**（`Theme::Dark()`，全部用 `Color::Parse("#RRGGBB")`）：
  背景 `#0D1117`、卡片面 `#161B22`、交替面 `#21262D`、正文 `#C9D1D9`、次要 `#8B949E`、
  强调 `#58A6FF`、成功 `#3FB950`、警告 `#D29922`、危险 `#F85149`、信息 `#79C0FF`、
  边框 `#30363D`、激活底 `#1F6FEB`、轨道 `#21262D` / 填充 `#2F81F7`、滑块 `#F0F6FC`。
  在 Windows Terminal / VS Code 里会自动走真彩，观感和网页深色主题一致。
- 真彩与 16 色不是二选一：缓冲同时保存 **CHAR_INFO 属性 + 每格精确 RGB**，
  `Flush()` 用属性（兼容老 conhost），`FlushAnsi()` 用 RGB（现代终端）。
  非真彩色时，`Color` 会按加权距离量化到最近的 16 色（`NearestIndex()`）。

### 真彩默认可用（自动探测）

不需要任何开关：`FlushAuto()` 自己判断当前终端能不能吃 24 位真彩。

```cpp
ConsoleBackBuffer buf = ConsoleBackBuffer::FromConsole();
buf.SetTrueColor(true);   // 一直在缓冲里保留精确 RGB（不影响 16 色路径）
...
buf.FlushAuto();          // 终端支持 → FlushAnsi()；否则自动退回 Flush()（16 色）
```

探测顺序（`ConsoleBackBuffer::SupportsTrueColor()`）：

1. 环境变量 `WT_SESSION`（Windows Terminal）、`TERM_PROGRAM`（VS Code / 其他现代终端）→ 认定支持；
2. `GetConsoleMode` 已带 `ENABLE_VIRTUAL_TERMINAL_PROCESSING` → 支持；
3. 否则尝试 `EnableVirtualTerminal()` 打开 VT，成功即支持；
4. 输出被重定向到文件/管道（无控制台句柄）→ 不支持，走 16 色。

demo 的交互模式默认就是「自动」：能真彩就真彩，不能就 16 色。想强制指定时用
`--truecolor` / `--mono`。

### 边框兼容（Win10 / 非 ANSI 自动降级）

`Border` / `Button` / `TextInput` / `Divider` 的边框默认走 Unicode 制表符
（`┌─┐│╭╮` 等）。在旧 conhost（包括 Win10 默认控制台）或非 ANSI 终端里，
这些字符可能变方块或乱码，所以库内置了自动降级：**所有边框统一换成
`+ - |` 的 `Box::Ascii`**。

判定顺序（`Box::DetectAsciiFallback()`）：

1. `TERMINAL_BOXES=ascii` 强制 ASCII，`TERMINAL_BOXES=unicode` 强制 Unicode；
2. `WT_SESSION`（Windows Terminal）、`TERM_PROGRAM`（VS Code 等）、
   `ConEmuANSI`、`ANSICON`、`TERM=xterm/screen/tmux/cygwin` → 现代终端，保留 Unicode；
3. 输出代码页不是 UTF-8 → ASCII；
4. 其余没有现代终端标记的环境（Win10 默认 conhost 等）→ ASCII。

降级只换边框字符和圆点，文字、颜色、标题、布局都不变：边框统一成
`+ - |`，Badge 状态点 / Slider 滑块 / demo 状态文字里的 `●` 换成窄圆点 `·`
（`Glyphs::Dot()` / `Glyphs::DotUtf8()`）。也可以在代码里显式覆盖：

```cpp
Terminal::Box::SetAsciiFallback(true);    // 全部 ASCII
Terminal::Box::SetAsciiFallback(false);   // 全部保留 Unicode
Terminal::Box::AutoAsciiFallback();       // 恢复自动判断
```

### 背景继承与焦点样式

CSS 里背景会从父元素继承，控制台 UI 也照这个规则来：`UI::EffectiveBackground()` 会

1. 自身显式设过背景（`SetBackground` / `SetBackgroundColor`，即 `style.opaque`）→ 用自己的；
2. 否则沿 `parent` 链往上找最近一个设了背景的祖先；
3. 都没有才回落 `Theme::Current().background`。

`Text / Button / Slider / TextInput / Divider / Panel / Border` 的绘制之所以统一走这个方法，
是为了避免"卡片里挖了个洞"：元件不再各自去取 `theme.background`，而是坐在谁的背景上就用谁的颜色。
渲染时也统一用 `Style` 重载写入缓冲（而不是先 `ToAttr()`），否则 24 位底色会被压成 16 色索引，
出现黑块。

按钮的**底色刻意不开放配置入口**：`Button` 没有 `Colors(bg, fg)` 这类方法，
`bg` 也不参与 JSON 配置（只认 `color` 作为文字色）。配色字段（`normalBg / focusBg / pressBg` 等）
仍然保留在类里，但所有状态背景都等于 `EffectiveBackground()`；
真要换底色请用通用的 `SetBackground()` —— 那是显式的容器级覆盖，不是按钮的专属入口。

按钮的三态（默认值跟随主题，demo 里显式设成参考图的红/青）：

| 状态 | 表现 |
|---|---|
| 正常 | 常态文字色 + 常态边框色（默认 `theme.text` / `theme.border`） |
| 聚焦 | 文字加粗，边框换成双线（`Box::Double`）表达「加粗」，颜色沿用常态边框 |
| 交互（悬停/按下） | 边框换成 `hotBorderColor`（默认 `theme.accent`），文字不变；背景全程不变 |

```cpp
btn->SetColors(fg, borderColor, hotBorderColor);  // 一次配好三态
btn->SetColor(c);             // 边框和文字同色：常态文字+常态边框一起设
btn->SetTextColor(fg);        // 只改文字色；未显式设边框时边框跟随文字色
btn->SetBorderColor(c);       // 常态边框色
btn->SetHotBorderColor(c);    // 交互边框色
btn->SetThickBorderOnFocus(false); // 关掉聚焦双线
```

参考图那套红框红字 / 青色交互框不是库里的写死默认，而是 demo 用
`SetColor(theme.danger)` + `SetHotBorderColor(RGB(45,212,191))` 配出来的。

### 输入框（TextInput）

UI 结构是「**标签 + 一个空格 + 框**」：标签在框外左侧（`SetLabel("昵称")`），
框内是内容/占位（`placeholder`，默认"输入…"）。`SetPrefix()` 仍保留，用于需要在框内
再加前缀的老用法，默认空。

```cpp
auto in = std::make_shared<TextInput>();
in->SetLabel("昵称");                 // 框外：昵称 + 空格 + 框
in->SetPlaceholder("输入昵称后回车");
in->layout.Height(3);                 // 单行输入框：1 行内容 + 上下边框
in->SetMultiline(true);               // 多行：内容行数自动增长
in->OnSubmit([](const std::string& v){ /* 回车提交 */ });
in->OnChange([](const std::string& v){ /* 每次编辑 */ });
```

**编辑能力**

| 能力 | 入口 |
|---|---|
| 插入 / 替换模式 | `SetOverwrite(bool)`、`SetMode(EditMode::Insert/Replace)`；`Insert` 键切换 |
| 选择模式（方向键直接扩选） | `SetSelectionMode(true)` / `SetMode(EditMode::Select)` |
| 选区 | 鼠标拖选、`Shift+←/→/↑/↓/Home/End`、`Ctrl+A`；`SelectedText()` |
| 剪贴板 | `Ctrl+C` 复制、`Ctrl+V` 粘贴、`Ctrl+X` 剪切（Win32 `CF_UNICODETEXT`，UTF-8 互转） |
| 撤销 / 重做 | `Ctrl+Z` 撤销，`Ctrl+Shift+Z` / `Ctrl+Y` 重做（`Undo()` / `Redo()`，保留 200 步） |
| 按词移动 / 删除 | `Ctrl+←/→` 只移动光标（**不会删除选中内容**）；`Ctrl+Backspace / Ctrl+Delete` 按词删除；`Ctrl+Home/End` 到全文首尾 |
| MENU 键（`VK_APPS`） | 在输入框内第一行弹出「复制 剪切 粘贴 撤销 重做 全选」菜单条，`←/→` 选择、`Enter` 执行、`Esc` 关闭，也可鼠标点选 |
| 分页 | `PageUp / PageDown` 多行按可见行翻页，单行跳到首/尾 |
| 长度上限 | `SetMaxLength(n)`：插入、粘贴、`SetValue`、调小上限都会把内容截到 n 个码点 |
| 多行 | `SetMultiline(true)`；`Enter` 插入换行而非提交；`↑/↓` 按列移动；`maxLines`（默认 6）封顶后输入框内部滚动（`scrollLine`） |

**水平滚动**：`scroll` 以**显示列**为单位（不是码点下标），宽字符不会算错；光标靠近右边界时
保留约 `min(max(textW/4,1),4)` 列的余量，左右都留出上下文，不再「贴边只剩一个字」。

**多字节渲染**：渲染按码点迭代并用 `Utf8::CharWidth` 推进显示列，宽字符占两格、第二格跳过，
因此在汉字/Emoji 中间插入不再错位；自检里有 "宽字符之后的文字位置正确" 断言。

> 快捷键依赖控制台送来 C0 控制码（`Ctrl+C`=3、`Ctrl+V`=22、`Ctrl+Z`=26、`Ctrl+Y`=25）。
> `Mouse::Enable()` 会关掉 `ENABLE_PROCESSED_INPUT`，否则 `Ctrl+C` 会被系统截成退出信号。

### 滚动容器（ScrollView）

`ScrollView` 是 Column 容器：子元素按完整内容高度排布，再整体上移 `scroll` 行，
超出部分靠 `UI::Render` 的裁剪挡住；内容溢出时右侧自动出现竖向滚动条。

```cpp
auto sc = std::make_shared<ScrollView>();
sc->layout.grow = 1;             // 撑满父容器剩余高度（填满左卡片）
sc->SetWheelStep(2);             // 滚轮一格滚 2 行
sc->AddChild(row1);
sc->AddChild(row2);
```

- **滚轮**：`OnMouse` 收到 `MouseAction::Wheel`，`wheelDelta / 120 * wheelStep`
  换算成行数（向上为正 → `scroll` 减小），事件冒泡到最近的 ScrollView。
- **竖向滚动条**：轨道 `│` + 滑块 `█`；点轨道空白直接跳到该位置，按住滑块可拖动，
  拖动使用 `FineY()` 的亚格坐标，手感连续。
- **键盘**：`PageUp/PageDown` 翻一屏、`Home/End` 到顶/底。
- **消息**：`scrollTo` / `scrollBy` / `page`，可被消息队列驱动。
- 布局精灵：`layout.grow > 0` 时 `MeasureHeight()` 返回 1，不按内容高度抢主轴空间，
  剩余高度由父级分配；否则按内容自动高度。

### 图片（UIImage）

半格像素方案：**1 个字符格 = 上下 2 个像素**，用 `▀`（上半块）承载
`前景色 = 上像素`、`背景色 = 下像素`，所以：

- 分辨率 = `列数 × 行数 × 2`，80x24 全屏约 80×48 像素；
- 完全走 `ConsoleBackBuffer`，能参与 DOM 布局、裁剪、脏矩形、16 色/真彩切换；
- PNG / JPG 用 Windows 自带的 **WIC** 解码（`windowscodecs`），不引第三方图片库；
- 透明像素按 `EffectiveBackground()` 混合，不会在卡片里挖洞。

```cpp
auto logo = std::make_shared<UIImage>();
logo->SetImage("assets/terminal_logo.png");
logo->layout.Width(24);
logo->layout.height = 8;             // 8 行 = 16 像素高
logo->SetFit(ImageFit::Contain);     // Contain / Cover / Stretch
```

也可以直接送内存像素（测试/程序生成图，不需要解码文件）：

```cpp
img->SetPixels(2, 2, {
    255,0,0,255,  255,0,0,255,      // 上排红
    0,0,255,255,  0,0,255,255,      // 下排蓝
});
```

缩放用最近邻采样；`Cover` 会按目标比例裁掉源图边缘，`Contain` 居中留边，
`Stretch` 直接铺满。24 位真彩下每个像素都是精确 RGB；16 色模式会自动量化。

> 半格只适合图标、头像、小尺寸预览；要 1:1 像素级显示请直接用图形 API，本库的定位是精简终端 UI。

### 消息队列（MessageQueue）

交互回调只负责**投递消息**，状态变更统一在主循环处理：`Message` 自己带目标对象（`UI*`），
处理时直接调它的 `OnMessage`，不需要在主循环里写 if/else 分发表。

```cpp
#include "Core/TerminalMessage.hpp"

// 触发函数（OnClick / OnChange / 键盘回调）里只入队
ok->onClick = [weakInput] {
    if (auto in = weakInput.lock()) MessageQueue::Post(in.get(), "submit");
};
cancel->onClick = [root] { MessageQueue::Post(root, "quit"); };

// 主 While() 统一处理
while (running) {
    MessageQueue::ProcessAll();   // 每条 -> msg.target->OnMessage(msg)
    ...
}
```

- 组件内置消息：`Text` 认 `setText / append / clear`；`Button` 认 `click / setText`；
  `Slider` 认 `setValue / step`；`TextInput` 认 `setText / append / submit / clear /
  copy / cut / paste / undo / redo`；容器只需覆写 `OnMessage` 就能接管自定义消息（demo 的根
  节点用 `"quit"` 退出）。
- 队列是进程内单例（`Post / ProcessOne / ProcessAll / Size / Empty / Clear`），投递侧加了锁；
  `ProcessAll()` 会把处理过程中新投递的消息也一起排空。
- 消息只保存裸 `UI*`：对象生命周期由 DOM 树保证；退出前调用 `MessageQueue::Clear()` 丢弃残留消息。

### 消息升级：发送者 + JSON 负载

`Message` 现在额外带 `sender`（发出消息的控件地址）和 `payload`（Json 负载），旧的
`text / number` 字段保留，所以现有组件的 `OnMessage` 不需要改。

```cpp
#include "Core/TerminalMessage.hpp"

Json info = MsgJson::Object();
info["value"] = Json(2.0);
info["name"]  = Json(std::string("demo"));

// 带 sender 的定向投递
MessageQueue::PostFrom(sw.get(), sw.get(), "toggle", info);

// 或者手动构造
Message msg;
msg.sender = btn.get();
msg.target = label.get();
msg.type   = "setText";
msg.payload = info;
MessageQueue::Post(msg);
```

> `Json` 是 `Core/TerminalJson.hpp` 提供的 JsonCore 适配类：内部完全由
> `ToolHead/DataStructure/Json` 的 `JSON::JsonElement` 承载，对外保留
> `parse / get / GetArray / IsObject` 等旧风格接口。整个工程的 JSON 已统一走 JsonCore，
> 不再依赖 `agl/wjson`。`Json` 只存在于 `Terminal` 命名空间，避免和 JsonCore 的
> `namespace JSON` 同名冲突；demo 里用 `using namespace Terminal;` 后直接写 `Json` 即可。

### 可嵌套页面（PageHolder）

每个 `PageHolder` 拥有一份 `LocalMessageQueue`，页面内消息不会被全局队列抢走；目标对象
没有消费（`OnMessage` 返回 false）时，消息自动把 target 改成父页面并冒泡。

```cpp
auto outer = std::make_shared<PageHolder>("outer");
auto inner = std::make_shared<PageHolder>("inner");
outer->AddChild(inner);                 // OnMount 自动找到 parentPage

inner->PostLocal(label.get(), "setText", "子页面消息");
Check(inner->PendingLocal() == 1);
inner->ProcessLocal();                  // 子页面处理；未消费 -> outer 队列
Check(outer->PendingLocal() == 1);
outer->ProcessLocal();                  // 根页面处理完
```

- `PostLocal(target, type, payload)`：投递到本页队列，`sender` 默认为本页；
- `PostLocal(target, type, text, number)`：兼容旧文本/数值消息；
- `ProcessLocal(maxCount)`：按顺序处理本页队列，未消费自动冒泡；
- `SendUp(msg)`：手动把消息送到父页面；
- `SetActive(false)` 只隐藏页面，不清空队列；`PendingLocal()` 可观察队列长度。
- 页面消息仍由主循环/页面自己调用 `ProcessLocal()`，不会替代全局 `MessageQueue`。

### Switch / ChoiceGroup / Window

```cpp
auto sw = std::make_shared<Switch>();
sw->SetLabel("自动保存").SetValue(true, false);
sw->onChange = [](bool on) { /* ... */ };

auto choice = std::make_shared<ChoiceGroup>();
choice->SetOptions({ "浅色", "深色", "跟随系统" });
choice->SetMulti(false);                 // true = 复选框组
choice->onChange = [](const std::vector<int>& selected) { /* ... */ };

auto win = std::make_shared<Window>("设置", 36, 12);
win->SetClosable(true).SetDraggable(true);
win->AddChild(sw);
win->MoveTo(8, 4);
```

- `Switch`：Space/Enter 投递 `"toggle"`，`OnMessage("toggle")` 切换并发出 `"change"`；
  也接受 `"set"` + `payload.value`。
- `ChoiceGroup`：Up/Down 移动，Space/Enter 或鼠标点击行投递 `"toggle"`；单选会清空其他项，
  多选独立切换；变化后发出带 `indices / index / multi` 的 `"change"`。
- `Window`：标题栏左键拖拽（内部更新 `windowPos` / bounds），右上角 `[x]` 或消息 `"close"` 关闭；
  内容区通过 `ContentBounds()` 让出边框和标题栏，子元素照常布局。它仍是**父容器内部**的
  浮动面板，不会突破 `UI::Render` 的 `PushClip(bounds)`。

### 音频（AudioPlayer）

```cpp
AudioPlayer player;
player.Open("music.mp3");          // WAV / MP3，MF 不支持时自动尝试 MCI
player.Play();
player.SetSpeed(1.5);              // 0.25x ~ 4x
player.Pause();
player.Resume();
player.Seek(12.0);                 // 秒
player.SetVolume(60);              // 0~100

// MFPlay 依赖窗口消息泵；控制台主循环每帧调用一次
player.Update();
std::cout << player.BackendName() << " " << player.Position() << "/" << player.Duration();
```

- 首选 Media Foundation（`mfplay.dll`），`SetSpeed` 实测对 WAV 返回成功；MCI 只作为兜底。
- 两个后端都用 `LoadLibraryW` + `GetProcAddress`，使用方不需要额外 `-lwinmm/-lmfplay`。
- `SpeedSupported()` 可查询当前文件和后端是否接受倍速；MCI 对 waveaudio 的 `speed` 通常不支持。
- `OnMessage` 支持 `play / pause / resume / stop / seek / speed`，方便接页面消息队列。

### 排版守卫（边框对齐）

`ConsoleBackBuffer::EnsureAlignment()` 在每帧刷新前扫描宽字符：

1. 如果 `●` 或全角字符紧邻非空格（终端会把它按双宽渲染，后面的边框/文字会被顶歪），
   先把 `●` 换成窄圆点 `·`，其他全角字符换成空格，并清掉被占用的下一格；
2. 发现异常时顺手调用 `Box::SetAsciiFallback(true)`，下一帧边框字符强制走 `Box::Ascii`；
3. 返回 `AlignmentReport{ detected, fixed, switchedAscii }`，demo 在 `switchedAscii` 时
   立即 `root->Invalidate()`，下一帧重绘成 ASCII 边框。

```cpp
root->Render(buf);
const auto rep = buf.EnsureAlignment();
if (rep.switchedAscii) root->Invalidate();
buf.FlushAuto();
```

### 鼠标交互

控制台鼠标走 Win32 事件（`ReadConsoleInputW` 的 `MOUSE_EVENT_RECORD`），不需要 VT 鼠标协议，
所以和现有的真彩输出可以同时开。

```cpp
Mouse::Enable();                       // 开 ENABLE_MOUSE_INPUT，并关掉 QuickEdit
MouseRouter::Instance().SetRoot(root.get());

while (running) {
    xMouse ev;
    while (Mouse::PollMouse(ev)) MouseRouter::Instance().Dispatch(ev);

    xChar key;
    while (Mouse::PollKey(key)) {        // 键盘/鼠标共用同一个事件泵
        if (IsKey(key, SKey::Esc)) running = false;
        else FocusManager::Instance().Dispatch(key);
    }
    // ... 渲染
}
MouseRouter::Instance().Clear();
Mouse::Disable();                      // 恢复原控制台模式
```

- **事件模型**：`xMouse{ x, y, fx, fy, fine, button, action, wheelDelta, shift/ctrl/alt }`，
  `MouseAction` = `Move / Down / Up / DoubleClick / Wheel / Leave`，
  `MouseButton` = `Left / Right / Middle / X1 / X2`。坐标已换算成窗口单元格坐标（不是缓冲坐标）。
- **原始输入（亚格精度）**：`MOUSE_EVENT_RECORD` 只精确到字符格，滑杆按格换算就变成"一格一档"。
  为此 `Mouse::Enable()` 会额外注册一份 `WM_INPUT` 原始鼠标（`RIDEV_INPUTSINK`，后台窗口也能收），
  并在每个控制台鼠标事件上「定锚」——锚点是事件给的准确单元格 + 当时的屏幕像素位置，之后按
  字符格像素尺寸把像素位移折成小数格偏移。这样不需要控制台窗口的屏幕原点
  （Windows Terminal / ConPTY 下 `GetConsoleWindow` 给的是伪窗口），事件一到又自动校正漂移。
  字符格像素尺寸不再只信 `GetCurrentConsoleFontEx`（不同 DPI / 终端缩放会报错值）：每收到一条
  控制台事件，就用「相邻两次事件的格差 vs 像素差」实测一次 `px/格`，限幅 3–64（宽）/ 6–96（高）
  后做 0.8/0.2 指数平滑，实测值可用后优先于字体接口。
  注册失败或无窗口时静默退回纯单元格坐标，功能不受影响；`Mouse::HasFinePosition()` 可查询状态。
- **格内位移也会上报**：控制台只在跨格时才发 `MOUSE_MOVED`，所以 `PollMouse` 会先把原始输入的
  格内位移合成一条 `Move`（`x/y` 取整供命中测试，`fx/fy` 带小数），否则亚格坐标根本到不了上层。
  元件的取值逻辑用 `ev.FineX()` / `ev.Fraction(from, len)`，没有原始输入时自动等于整数格。
- **拖出元件范围不会停**：控制台在指针离开可见区域后就不再发 `MOUSE_MOVED`，只剩 `WM_INPUT` 增量。
  因此锚点不再因为"偏离太远"被丢弃（那正是滑杆拖出去就停住的原因），拖动中被捕获的元件
  也不会因为 `OnMouseLeave` 断开拖拽；比例一律由 `SetByRatio` 夹到 `[0,1]`。
- **滚轮**：`MOUSE_WHEELED` 被解析成 `MouseAction::Wheel` + `wheelDelta`（正数向上），
  从命中的元件一路冒泡；`ScrollView` 用 `wheelDelta / 120 * wheelStep` 换算成滚动行数。
- **滑杆的换算**：`Slider::SetByPosition(double cellX)` 与绘制严格对齐（轨道占
  `[trackX, trackX + trackW)`），点击/拖动都走亚格坐标；`SetByPixel(int)` 是整数格入口，等价兼容。
  120 列宽下 100 步的滑杆原来只有 ~20 个落点，现在每像素都能改值。
- **命中测试**：`MouseRouter::HitTest(root, x, y)` 深度优先、后加入的子元素优先（等同于 z 序在上），
  返回最内层命中的元件；`Dispatch` 再沿 parent 链冒泡，容器也能接住事件。
- **焦点与捕获**：按到可聚焦元件会自动 `FocusManager::Focus(it)`；按下后被按元件成为捕获者，
  后续移动/抬起都发给它，因此 **滑杆可以按住拖动**，抬起后捕获自动释放。
- **悬停**：悬停对象变化时自动 `OnMouseEnter / OnMouseLeave`，元件借此做高亮（按钮、滑杆均已接入）。
- **键盘/鼠标不抢**：`PollMouse` 读到的键盘记录会被塞进内部队列，由 `Mouse::PollKey` 取走，
  中文 UTF-8 与方向键照旧能进输入框；鼠标关闭时 `Mouse::PollKey` 自动退回 `Input::PollKey`。
- **安全退出**：`Mouse::Disable()` 会恢复 `Enable()` 前的控制台模式（QuickEdit 也能回来）。

### 渲染性能

- **脏标记**：`UI::Invalidate()` 把「自身及子树需要重绘」沿 parent 链上冒泡，
  `UI::Render()` 遇到 `dirty == false` 整棵子树直接跳过。状态改变的 setter
  （`SetText`/`SetValue`/`SetStyle`/焦点切换/尺寸变化…）都会自动 `Invalidate()`。
- **空闲零开销**：界面无变化时 `Render` / `Flush` 都不产生控制台写入，主循环里只跑输入轮询。
- **布局短路**：`Arrange()` 在「目标矩形没变 + 无需重排」时直接返回，不重复递归子元素。
- **写格才算脏**：`WriteCell` / `Fill` 逐格比较字符、属性、RGB，真正变了才标脏；
  `Flush()` 只提交脏矩形。
- **ANSI 只刷脏区**：`FlushAnsi()` 按行定位、只输出脏区内的行，行内同色连续段只发一次 SGR；
  宽字符的第二格（占位空格）不输出，交给终端自然占位，避免「一 个 字 符」式的双倍间距。
- `--bench` 可直接看数据（120x40 画面，200 帧）：

  ```
  [bench] 脏标记路径 : 0.002 ms/frame（静态界面只重绘 1 次）
  [bench] 全量重绘   : 0.25  ms/frame（120x40，含滚动列表 / 图片 / 多滑杆）
  [bench] ANSI 生成  : 0.38  ms/frame, 21747 bytes/frame
  ```

  （数值与机器、终端尺寸、界面复杂度有关，仅作量级参考。）

### JSON 驱动

`UIComponentManager` 内置类型注册表，支持 `text / button / input / slider / scroll / image / border / panel / divider / badge / switch / choices / singlechoice / multichoice / window / page / spacer`：

```cpp
UIComponentManager mgr;
mgr.LoadFromText(R"({
  "elements": [{
    "UItype": "panel", "flow": "column", "gap": 1, "padding": 1,
    "children": [
      { "UItype": "text",   "text": "用户名", "color": "cyan" },
      { "UItype": "input",  "placeholder": "请输入", "grow": 1 },
      { "UItype": "slider", "label": "音量", "min": 0, "max": 100, "value": 35 }
    ]
  }]
})");
mgr.Arrange(Rect2Di{ 0, 0, 60, 20 });
```

也可以用 `UIComponentManager::RegisterType("mytype", [](const Json& js) { ... })` 注册自定义元件。
`Json` 由 `Core/TerminalJson.hpp` 提供，底层是 JsonCore；需要原生 `JsonElement` 时可直接用
`js.element()`。

---

## demo 的入口

```powershell
terminal_demo --selftest     # 168 项逻辑自检（颜色/UTF-8/缓冲/布局/元件/边框降级/图片/滚动/消息/PageHolder/新控件/排版守卫）
terminal_demo --frame        # 渲染一帧，按 24 位真彩输出 ANSI（可重定向到文件检查配色）
terminal_demo --dump         # 打印布局树（bounds + pref，排查布局问题）
terminal_demo                # 交互：Tab 焦点、拖动 R/G/B 滑杆改昵称颜色、左卡片滚轮滚动、右卡片半格像素图标、Esc/q 退出
terminal_demo --truecolor    # 强制走 ANSI 真彩刷新
terminal_demo --mono         # 强制 16 色属性刷新（老 conhost / 远程终端排查用）
terminal_demo --bench 200    # 渲染性能基准（脏标记 / 全量重绘 / ANSI 生成三条数据）
```

`--selftest` 会被 `ctest` 调用，返回非 0 即失败。

---

## 已知限制

- 24 位真彩只在 ANSI 路径（`ToAnsiText` / `FlushAnsi` / `OutRgb`）生效；
  `Flush()`（`WriteConsoleOutputW`）受 Win32 控制台逐格属性限制，只有 16 色。
  真彩现在默认自动探测（见上），无需手动加参数；老 conhost 会自动退回 16 色。
- 脏标记是「子树级」：某个元件变化会让它的祖先链路一起重绘，因此单次重绘仍是
  整棵脏子树（对 80x24 级别界面开销可忽略，见 `--bench`）。若要更细的局部刷新，
  可在 `OnRender` 里只画自己的 bounds。
- 宽字符（CJK/Emoji）按"2 格"近似排布，组合字符与零宽字符按 0 宽处理。
- 只支持 Windows 控制台 API，未做 Linux termios 后端。
- 布局求解不实现 flex-wrap，超出空间时按比例压缩（最小 1 格）。
- `UI::OnKey` 走单焦点模型（`FocusManager`），没有全局快捷键拦截。
- 按钮底色只能通过通用的 `SetBackground()` 显式覆盖（容器级操作），没有按钮专属的 bg 入口；
  组件的 `Style` 必须整体传给缓冲的 `Style` 重载，别先 `ToAttr()`，否则底色会被降采样成 16 色。
- 消息队列只保存裸 `UI*`，对象析构后残留消息会被跳过但无法自动识别（退出前 `MessageQueue::Clear()`）；
  队列是同步消费，没有延迟投递 / 优先级 / 后台线程调度。
- MENU 键（`VK_APPS`）依赖终端把它作为按键记录送来；部分精简键盘 / 远程终端没有这个键，
  此时仍可用 `Ctrl+C/V/X/Z/Y`。菜单条画在输入框内第一行（受元件裁剪限制，不做越界弹窗）。
- 边框兼容模式替换边框字符和圆点（`●` → `·`）；滑杆轨道 `━`、滚动条 `█` / `│`、
  图片半格 `▀` 仍使用 Unicode，非 UTF-8 终端里需要自行换主题/字符或关闭这些元件。
- `UIImage` 用半格像素，纵向分辨率只有「行数 ×2」；缩放是最近邻，没有抗锯齿。
  目前只解 PNG / JPG 静态帧，GIF / WebP / APNG 动图不解析；运行时动态加载 `windowscodecs.dll` / `ole32.dll`。
- `ScrollView` 只实现竖向滚动与竖向滚动条（横向滚动、嵌套滚动、滚动惯性暂未做）；
  内容仍是普通子元素，会被父级裁剪，滚出视口的部分不参与命中测试。
- 鼠标走 Windows 控制台记录，仅在「本机真实控制台窗口」里有效：输出被重定向到文件/管道、
  或运行在只提供 VT 鼠标协议的终端包装层里时，`Mouse::Enable()` 会失败并自动退回纯键盘模式。
  未实现 VT 鼠标协议（`ESC[?1000h` / SGR 扩展）；控制台原生记录下滚轮事件已解析，
  `ScrollView` 已按 `wheelDelta/120 × wheelStep` 处理。
- 亚格精度依赖原始输入：注册是「增强项」，失败时自动退回单元格精度。格像素尺寸先用
  `GetCurrentConsoleFontEx`，随后由「相邻两次控制台事件实测」校正（0.8/0.2 平滑），
  因此非整数缩放只会在最初几像素内略有误差，事件到来即收敛；不累积。
- 输入框的 `Ctrl+C/V/X/Z/Y` 依赖控制台把按键送成 C0 控制码；`Mouse::Enable()` 会关掉
  `ENABLE_PROCESSED_INPUT` 来避免 `Ctrl+C` 被系统截走。只用 `Input::ReadKey()`（conio）时
  `Ctrl+C` 仍会直接结束进程，且无法区分 `Ctrl+Shift+Z`（与 `Ctrl+Z` 同码 26）。
- 多行输入框靠 `MeasureHeight()` 随行数变大，需要父容器重新 `Arrange()`：demo 主循环在
  `root->layoutDirty` 时会重排一遍；静态界面请自行在 `value` 变化后调用 `Arrange()`。
- `PageHolder` 的局部队列不会自动扫描整棵 DOM 判断消息归属；`PostLocal` 是显式投递。
  目标对象没消费时只向父 PageHolder 冒泡一次，根页面未消费的消息直接丢弃。
- `Window` 是父容器内部的浮动面板，不能突破 `UI::Render` 的 `PushClip(bounds)`；
  没有全屏遮罩、模态焦点锁和跨父级 Z 序。
- `AudioPlayer` 的 MF 后端依赖 `Update()` 泵窗口消息；不调用时可能停在 Stopped。
  MCI 兜底对 waveaudio 通常不支持 `speed`，`SetSpeed` 会返回 false。
- `EnsureAlignment()` 只能基于缓冲区里「宽字符下一格是否被占用」做启发式判断，
  无法读取终端字体的真实像素宽度；它能修掉 `●/全角字符` 压格，但不能保证所有
  第三方终端字体缩放下的绝对对齐。
- JsonCore 不是纯头文件，`JsonCore.cpp / Number.cpp` 必须参与编译；CMake 已自动归档，
  直接用 g++ 时需要显式带上这两个源文件。
- `Json` 是值语义包装：普通拷贝、`get()` / `GetArray()` 遍历都会得到独立副本；
  只有 `operator[]` 返回指向父节点的视图（用于 `json["key"] = value` 这种写入）。
- 剪贴板走 Win32 `CF_UNICODETEXT`：无剪贴板权限（服务/会话隔离）时复制粘贴静默失败并返回 false。
- 鼠标坐标依赖 `srWindow` 原点换算；窗口被拖出滚动区时坐标仍按当前可视窗口计算。
