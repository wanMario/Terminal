# Terminal —— Win32 控制台 UI 库（纯头文件）

模仿 HTML + CSS 盒模型的控制台界面引擎：DOM 树、布局、嵌套、焦点、主题配色，全部通过
`ConsoleBackBuffer` 双缓冲绘制，不直接写控制台，因此不闪烁、不撕裂，且能同时支持
**16 色属性**与 **24 位真彩**。

- 语言 / 标准：C++20，核心为 header-only（`.hpp`）；同时提供 `TerminalUI` 静态库，统一归档 JsonCore 的非头文件实现
- 平台：Windows（`WriteConsoleOutputW` / `_getch` / `SetConsoleMode`）
- 工具链验证：MinGW-W64 GCC 14.2.0（`C:\mingw64\bin\g++.exe`）、CMake 3.30.4（`MinGW Makefiles`）

---

## 目录结构

```
Terminal/
├── Terminal.hpp                 # 一揽子聚合头（include 它即可）
├── CMakeLists.txt               # TerminalUI 静态库 + TerminalArchive DLL + 4 个 demo + CTest
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
│   ├── TerminalLyrics.hpp       # Lyrics（.lrc 时间轴 / 元信息 / offset 解析）
│   ├── TerminalArchive.hpp      # Archive（动态加载 TerminalArchive.dll；libarchive 后端）
│   └── TerminalComponentManager.hpp # 类型注册表 + Json -> UI 树（JsonCore）
├── Components/
│   ├── TerminalText.hpp         # Text      静态文字（多行、对齐、主题化）
│   ├── TerminalButton.hpp       # Button    按钮（文字/边框色可配、聚焦加粗、交互换 hot 边框色）
│   ├── TerminalTextInput.hpp    # TextInput 输入框（选区/撤销重做/剪贴板/多行/MENU 菜单）
│   ├── TerminalSlider.hpp       # Slider    滑杆（键盘步进、比例填充）
│   ├── TerminalScrollView.hpp   # ScrollView 滚动容器（横竖滚动条 + 滚轮 + 拖动）
│   ├── TerminalImage.hpp        # UIImage   半格像素图片（PNG/JPG，WIC 解码）
│   ├── TerminalBorder.hpp       # Border    边框容器（标题、让出边框的内容区）
│   ├── TerminalPanel.hpp        # Panel     面板容器（标题栏、内边距、方向）
│   ├── TerminalDivider.hpp      # Divider   分隔线（可带标题，`─ 标题 ────`）
│   ├── TerminalBadge.hpp        # Badge     胶囊标签（可带 ● 状态点）
│   ├── TerminalSwitch.hpp       # Switch    开关（ON/OFF，消息驱动）
│   ├── TerminalChoiceGroup.hpp  # ChoiceGroup 单选/多选（键盘 + 鼠标 + 消息）
│   ├── TerminalTable.hpp        # Table     表格（TStrFmt 单元格 / 编辑 / 筛选 / CSV·XML）
│   ├── TerminalFilePicker.hpp   # FilePicker 原生/终端文件选择器（文件/文件夹/多选）
│   ├── TerminalWindow.hpp       # Window    可拖标题栏/可关闭弹窗，内容区可排版
│   ├── TerminalOverlayHost.hpp  # OverlayHost 悬浮层（不占布局，只让子窗口命中）
│   └── TerminalPageHolder.hpp   # PageHolder 可嵌套页面 + 每页独立局部队列
├── src/TerminalUI.cpp           # 静态库装配单元（提供 Terminal::Version() 符号）
├── src/TerminalArchiveDll.cpp   # 方案 D：TerminalArchive.dll 的 C ABI（进程内 libarchive）
├── third_party/libarchive/      # 随库分发的 libarchive / zlib / liblzma 静态库与头文件
├── examples/
│   ├── demo_common.hpp          # 共用交互主循环 / 单帧 / 布局转储 / 性能基准
│   ├── demo.cpp                 # 样式展示：边框 / 排布 / TStrFmt / 滚动 / 两个 Window
│   ├── table_editor_demo.cpp    # 表格编辑器：编辑 / 筛选 / CSV·XML 导出
│   ├── music_player_demo.cpp    # 音乐播放器：D:\CloudMusic / 倍速 / 进度 / LRC 时间轴（从此播放·改时间）
│   └── zip_tool_demo.cpp        # 压缩包工具：zip·7z·rar（libarchive DLL）
├── tests/terminal_selftest.cpp  # 纯逻辑自检（CTest 单独运行）
├── tests/archive_dll_test.cpp   # TerminalArchive.dll 加载 / C ABI 冒烟测试
├── tests/archive_formats_test.cpp # ZIP 密码 / 7z / RAR 后端测试
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
    app.cpp -Lbuild/lib -lTerminalUI -lole32 -luuid -lshell32 -static -o app.exe
```

`-lTerminalUI` 要放在源文件/目标文件后面，静态链接才会正确解析。MinGW 加上 `-static`
可以把 libstdc++ / libgcc / libwinpthread 一起静态链入，避免系统里其他 MinGW 的旧 DLL
抢先加载导致“找不到入口点”。

### Archive DLL（方案 D）

如果不想让 C++ 调用方包含 `TerminalArchive.hpp`，构建 `TerminalArchive` 目标：

```powershell
cmake --build build --target TerminalArchive
# MinGW 产物：build/TerminalArchive.dll
```

DLL 只导出 C ABI，内部使用 **静态链接的 libarchive**，在进程内完成 ZIP/7z 创建与 ZIP/7z/RAR 解压；不释放 `bz.exe`、不启动任何子进程。导出函数：

```text
TerminalArchive_Available() -> int
TerminalArchive_BandizipPath(char* buffer, int bufferSize)   # 兼容旧调用，返回 "libarchive (built-in)"
TerminalArchive_Create(archive, entries, count, password, level, format, recurse, out, outSize, err, errSize) -> int
TerminalArchive_Extract(archive, destination, password, overwrite, out, outSize, err, errSize) -> int
TerminalArchive_List(archive, password, out, outSize, err, errSize) -> int
TerminalArchive_Test(archive, password, out, outSize, err, errSize) -> int
```

- `format`：`0` 自动、`1` zip、`2` 7z（`3` rar 仅解压）。
- 返回 `1` 成功、`0` 失败；`error` 缓冲区接收失败原因。
- 后端源码在 `third_party/libarchive/`：`libarchive.a` + `libzlibstatic.a` + `liblzma.a`，
  链接时只额外使用 Windows 系统库 `bcrypt` / `xmllite` / `ole32`。
- 因此**分发时仍然只需要 `TerminalArchive.dll` 一个文件**，不需要 `bin/bz.exe`，也不需要安装 Bandizip。
- ZIP 创建支持 ZipCrypto 密码；读取 ZIP / RAR 密码可用。libarchive 的 7z 读取不支持
  “加密头”（Bandizip 生成的带密码 7z 默认就是加密头），会在 `error` 中明确报错。
- DLL 已用 `-static -static-libgcc -static-libstdc++` 链接，`objdump -p` 只显示
  系统 DLL（`KERNEL32.dll` / UCRT / `bcrypt.dll` / `ole32.dll` / `XmlLite.dll`），
  不依赖 MinGW 的 `libstdc++-6.dll / libgcc_s_seh-1.dll / libwinpthread-1.dll`。

`ctest` 中的 `archive_dll_api` 会加载 DLL，执行 ZIP 创建 / 列表 / 解压；
`archive_formats_api` 进一步覆盖 ZIP 密码与 7z 往返。RAR 解压可在测试程序后追加
一个 `.rar` 样本路径。

### 直接用 g++

```powershell
g++ -std=c++20 -Wall -Wextra -static `
    -ID:/wan1/C++/Libraries/ToolHead -ID:/wan1/C++/Libraries -ID:/wan1/C++/Libraries/ToolHead/Terminal `
    -ID:/wan1/C++/Libraries/ToolHead/DataStructure/Json `
    examples/demo.cpp src/TerminalUI.cpp `
    ../DataStructure/Json/JsonCore.cpp ../DataStructure/Json/Number.cpp -o terminal_demo.exe `
    -lole32 -luuid -lshell32
```

也可以改用静态库（先构建 `TerminalUI`）：

```powershell
g++ -std=c++20 -Wall -Wextra -static `
    -ID:/wan1/C++/Libraries/ToolHead -ID:/wan1/C++/Libraries -ID:/wan1/C++/Libraries/ToolHead/Terminal `
    -ID:/wan1/C++/Libraries/ToolHead/DataStructure/Json `
    examples/demo.cpp -Lbuild/lib -lTerminalUI -lole32 -luuid -lshell32 -o terminal_demo.exe
```

`src/TerminalUI.cpp` 提供 `Terminal::Version()`；JsonCore 的 `.cpp` 提供解析/容器实现，
漏掉任一部分都会链接失败。

其他三个 demo 只需替换上面的源文件：

```powershell
g++ -std=c++20 -Wall -Wextra -static `
    -ID:/wan1/C++/Libraries/ToolHead -ID:/wan1/C++/Libraries -ID:/wan1/C++/Libraries/ToolHead/Terminal `
    -ID:/wan1/C++/Libraries/ToolHead/DataStructure/Json `
    examples/table_editor_demo.cpp -Lbuild/lib -lTerminalUI -lole32 -luuid -lshell32 -o table_editor_demo.exe
```

`music_player_demo.cpp` / `zip_tool_demo.cpp` 同理。自检程序单独编译：

```powershell
g++ -std=c++20 -Wall -Wextra -static `
    -ID:/wan1/C++/Libraries/ToolHead -ID:/wan1/C++/Libraries -ID:/wan1/C++/Libraries/ToolHead/Terminal `
    -ID:/wan1/C++/Libraries/ToolHead/DataStructure/Json `
    tests/terminal_selftest.cpp -Lbuild/lib -lTerminalUI -lole32 -luuid -lshell32 -o terminal_selftest.exe
```

依赖只有同仓库的 `ToolHead/Math/Geometry/G2D`（`Point.hpp` / `Rect.hpp`）和
`ToolHead/DataStructure/Json`（JsonCore）。`UIImage` 的 PNG/JPG 解码通过运行时加载
`windowscodecs.dll` / `ole32.dll` 完成，音频后端通过运行时加载 `mfplay.dll` / `winmm.dll`，
所以 g++ 命令**不需要**额外写 `-lwindowscodecs -lmfplay -lwinmm`；但原生文件选择器
`PickNative()` 使用 `IFileOpenDialog`，需要 `-lole32 -luuid -lshell32`（CMake 已自动处理）。

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

### 富文本：TStrFmt / TStr

`TStrFmt` 把带控制符的 UTF-8 字符串解析成若干 `TStrRun{ text, Style }`；
`ConsoleBackBuffer` 的数据结构不变，`TStr::WriteTo()` 只是分段调用 `WriteText`。

```cpp
using namespace Terminal;

const Style base(Color::Parse("#ffffaa"), Colors::Black);
TStrFmt fmt(base);                          // 基础颜色
const auto rich = fmt.Parse(
    "{Red}这是{#ffffff}一{#FFFFFF}个{/r}变"
    "{&12}色{DarkBlue}字{Blue}符串！{r}用来\\{\\}表示，"
    "还可以{/r*0.8 + Red*0.6}混色和{/c}彩虹!!!!!{/r}");

rich.WriteTo(buf, 2, 3);                    // 按段写入文字 + Style
```

| 控制符 | 含义 |
|---|---|
| `{#RRGGBB}` / `{#RGB}` | `Color::Parse` 解析 RGB |
| `{&x}` | 16 色索引 `x`（0~15） |
| `{Red}` / `{DarkBlue}` | 具名颜色 |
| `{/r}` | 重置为基础颜色（`{r}` 是兼容简写） |
| `{/c}` | 彩虹模式，从红色开始，每个 Unicode 码点推进 |
| `\{` `\}` `\\` | 输出字面量 `{` `}` `\` |

颜色表达式按 `*` > `+` / `|` 的优先级解析：

```text
{/r*0.8 + Red*0.6}   // 基础色 ×0.8 + 红色 ×0.6，逐通道截断到 0~255
{!Red}               // 逐通道 0xff - x
{Red | Blue}         // (a+b)*0.5
{(Red + Blue)*0.5}   // 括号改变优先级
```

同一 `Style` 的相邻文字会自动合并成一段，CJK / UTF-8 按码点处理；
`TStr::Plain()` 取纯文本，`TStr::Width()` 计算最大行显示宽度。

### 表格（Table）

`Table` 是独立组件，不内建滚动条；把 `MeasureWidth()` / `MeasureHeight()` 交给外层
`ScrollView` 即可获得横/竖滚动。表头和单元格都按 `TStrFmt` 解析，因此单元格里可以直接写
`{Red}Alice{/r}`；编辑提交时保存原串，不会把颜色丢掉。

```cpp
auto table = std::make_shared<Table>();
table->AddColumn("姓名", 10);
table->AddColumn("年龄", 6, LayoutPos::Right);
table->AddColumn("状态", 10);

table->AddRow({ "{Red}Alice{/r}", "20", "{#00ff88}在线{/r}" });
table->AddRow({ "Bob", "31", "离线" });
table->SetEditable(true);
table->layout.width = table->MeasureWidth();
table->layout.height = table->MeasureHeight();
scroll->AddChild(table);
```

- **编辑**：单击选中，双击进入编辑；键盘 `Enter` / 可打印字符也可进入，`Enter` 提交，
  `Esc` 取消，方向键/Tab 移动。提交时触发 `onCellChanged(row, col, text)`。
- **筛选**：`SetFilter(col, query)` 只匹配该列的 `TStrFmt` 可见纯文字（不区分 ASCII
  大小写），`ClearFilter()` 恢复；`VisibleRows()` 返回真实数据行下标，因此筛选不会改变
  编辑/选中使用的行列编号。
- **导入导出**：`ImportCsv / ExportCsv`；`ImportExcel / ExportExcel` 接受
  `.csv`、`.tsv` 与 SpreadsheetML `.xml`（Excel 2003 XML，Excel 能直接打开）。
  **不提供 `.xlsx`**：本库不引入 zip/deflate 依赖，`ExportExcel("*.xlsx")` 返回 `false`。
  CSV/XML 保留单元格原始 `TStrFmt` 串；不保留颜色样式（样式由运行时主题决定）。
- **外观**：`headerRow`、`striped`、`showGrid` 可配；边框自动走
  `Box::Effective(Box::Single)`，ASCII 兼容模式下自动降级。

### 元件一览

| 元件 | 关键成员 / 方法 | 键盘 | 鼠标 |
|---|---|---|---|
| `Text` | `SetText / GetText / SetLines / SetAlign / SetStyle` | — | — |
| `Button` | `SetLabel / OnClick / SetFrame / SetTextColor / SetBorderColor / SetHotBorderColor / SetColors`；聚焦加粗、交互换边框色 | Enter、Space | 悬停高亮、点击触发 |
| `TextInput` | `SetLabel / SetValue / GetValue / SetPlaceholder / SetPrefix / SetPassword / SetMaxLength / SetMultiline / SetMaxLines / SetOverwrite / SetSelectionMode / OnSubmit / OnChange` | 方向键、Home/End、退格、Delete、UTF-8 输入、`Insert` 插入/替换、`Ctrl+C/V/X/Z/Y/A`、`Shift+方向键` 扩选 | 点击定位光标、拖选 |
| `Slider` | `SetRange / SetValue / SetStep / OnChange / OnCommit / IsDragging`、`Ratio()`、`Track()`、`SetByPosition(cellX)` | ←→、Home/End、PageUp/PageDown、`h`/`l` | 点击跳值、按住拖动，**亚格精度**（每像素可改值）；`OnCommit` 在抬起时提交昂贵的 seek/speed 操作 |
| `ScrollView` | `SetScroll / SetScrollX / ScrollBy / ScrollByX / Page / SetWheelStep / SetShowScrollbar / SetShowHScrollbar`、`MaxScroll() / MaxScrollX()` | PageUp/PageDown、Home/End、←/→ | 滚轮（Shift+滚轮横向）、点/拖右侧竖向滚动条、底部横向滚动条 |
| `UIImage` | `SetImage(path) / SetPixels(w,h,rgba) / SetFit(Contain·Cover·Stretch) / Clear()` | — | — |
| `Border` | `SetChars / SetTitle / SetTitleAlign / SetBorderColor` | — | — |
| `Panel` | `SetFlow / SetGap / SetPadding / SetTitle / SetFill` | — | — |
| `Divider` | `SetTitle / SetChars / SetColors(line, title)`，无标题时整行横线 | — | — |
| `Badge` | `SetText / SetDot / SetColors(bg, fg)`，宽度 = 文本 + 圆点/内边距 | — | — |
| `Switch` | `SetLabel / SetValue / SetColors(on, off) / OnChange` | Space/Enter | 整行悬停高亮、点击切换 |
| `ChoiceGroup` | `SetOptions / SetMulti / SetSelected / OnChange` | Up/Down、Space/Enter | 悬停行高亮、点击行选中/切换 |
| `Table` | `AddColumn / AddRow / SetCell / GetCell / SetEditable / SetFilter / ClearFilter / AutoFitColumns / ImportCsv / ExportCsv / ImportExcel / ExportExcel` | 方向键/Tab 移动、Enter/输入进入编辑、Esc 取消 | 单击选中、双击编辑；可编辑单元格为 IBeam |
| `FilePickerWindow` | `Open / Cancel / NavigateTo / Refresh / IsDone / WasCancelled / Result / onAccepted / onCancelled`；`PickNative(options)` 为 Windows 原生入口 | Esc 取消 | 目录行/文件行选择、按钮打开、路径输入回车导航 |
| `OverlayHost` | `AddChild / Arrange`、`HitTest` 只覆盖可见子窗口 | — | 不占布局高度的悬浮层，窗口可拖到全屏，窗口外点击穿透 |
| `Window` | `SetTitle / SetFrame / MoveTo / Show / SetClosable / SetDraggable / SetResizable / SetMinSize` | Esc 由宿主处理 | 标题栏拖动、右上角 `[x]` 关闭；按住 Ctrl 边框高亮 8 个把手后可拖拽改尺寸，内容区子控件照常接收鼠标 |

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

判定顺序（`Box::IsAnsiSupported()` / `Box::DetectAsciiFallback()`）：

1. `TERMINAL_BOXES=ascii` 强制 ASCII，`TERMINAL_BOXES=unicode` 强制 Unicode；
2. `WT_SESSION`、`TERM_PROGRAM`、`ConEmuANSI`、`ANSICON`、`TERM=xterm/screen/tmux/cygwin` → 现代终端，保留 Unicode；
3. 输出代码页不是 UTF-8 → ASCII；
4. Win11（build 22000+）或已经打开 `ENABLE_VIRTUAL_TERMINAL_PROCESSING` 的现代终端 → 保留 Unicode；
5. 其余没有 ANSI 能力的环境（Win10 默认 conhost 等）→ ASCII。

> 判定只看终端能力，不扫描缓冲区内容，所以 CJK 标题、悬浮窗口遮挡、
> 滑杆 `●` 紧贴边框都不会再触发兼容模式。

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
横向超过视口时整体左移 `scrollX` 列，超出部分靠 `UI::Render` 的裁剪挡住。
内容溢出时右侧自动出现竖向滚动条，底部自动出现横向滚动条，二者互不重叠。

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
  拖动时用 `ev.FineY()` 的亚格比例换算，滚轮一格为 `wheelStep` 行。
- **横向滚动条**：轨道 `─` + 滑块 `█`；内容自然宽度超过视口时自动出现，点/拖行为同上，
  使用 `ev.FineX()`；`Shift+滚轮` 按 `wheelStepX` 横向滚动，焦点在容器时 ←/→ 逐列移动。
- **滚动条几何**：竖条只占视口高度（不会侵入横向条），横条只占视口宽度（不会侵入竖向条），
  右下角不会互相覆盖。
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
win->SetClosable(true).SetDraggable(true).SetResizable(true).SetMinSize(24, 8);
win->AddChild(sw);
win->MoveTo(8, 4);
```

- `Switch`：Space/Enter 或鼠标左键按下/抬起投递 `"toggle"`，`OnMessage("toggle")` 切换并发出 `"change"`；
  也接受 `"set"` + `payload.value`；整行都是鼠标热区，悬停有底色反馈。
- `ChoiceGroup`：Up/Down 移动，Space/Enter 或鼠标点击行投递 `"toggle"`，悬停行高亮；单选会清空其他项，
  多选独立切换；变化后发出带 `indices / index / multi` 的 `"change"`。demo 左卡片的滚动区里同时放了
  单选（暗色/亮色/霓虹）和多选（高对比/减少动画/紧凑模式）两个实例。
- `Window`：标题栏左键拖拽（内部更新 `windowPos` / bounds），右上角 `[x]` 或消息 `"close"` 关闭；
  `SetDraggable(false)` 可关掉标题栏拖动，`SetResizable(true)` 才启用缩放（默认关闭）。
  缩放时按住 Ctrl 并把鼠标移到边框，边框转为 warning 色并显示 8 个把手（4 角 + 4 边中点，
  均为单宽的 `+`，避免 `◆` 在部分 CJK 终端被按双宽处理而误触发 ASCII 兼容），
  从任意把手拖拽即可改宽高；`SetMinSize` 给最小尺寸，
  窗口不会越出父容器，拖拽过程中内容布局在下一帧收敛。demo 的 `MusicWindow` 已开启并设为 32×10 起。
  内容区通过 `ContentBounds()` 让出边框和标题栏，子元素照常布局。它是**父容器内部**的浮动面板，
  受 `UI::Render` 的 `PushClip(bounds)` 限制；库内置的 `OverlayHost` 把它托到根级，
  从而实现全屏拖动与点击穿透。

`OverlayHost` 现在属于库组件（`TerminalOverlayHost.hpp`）：

```cpp
auto overlay = std::make_shared<OverlayHost>();
auto win = std::make_shared<Window>("设置", 36, 12);
win->SetResizable(true);
win->MoveTo(8, 4);
overlay->AddChild(win);
root->AddChild(overlay);
```

它自身不占布局高度，`bounds` 始终是整块画布，因此 `Window` 缩放时
`parent->GetBounds()` 不会被缩成窗口自身（这是“只能缩小、放大被夹住”的根因）；
命中测试只遍历可见子窗口，窗口外的鼠标会穿透到下层控件。

### 文件选择器（FilePicker）

**A. Windows 原生对话框**

```cpp
FilePickerOptions opts;
opts.title = "选择文件";
opts.allowMultiple = true;                 // 多选
opts.selectFolders = false;                // true = 选文件夹
opts.filters = { { "代码", "*.cpp;*.hpp" }, { "所有文件", "*.*" } };

const auto paths = PickNative(opts);       // 阻塞，返回空表示取消
```

`PickNative` 使用 `IFileOpenDialog`：支持文件类型筛选、文件夹选择、多选，标题栏和
Windows 原生对话框一致。文件夹模式使用 `FOS_PICKFOLDERS`；多选用 `FOS_ALLOWMULTISELECT`。

**B. Terminal Window 对话框**

```cpp
auto picker = std::make_shared<FilePickerWindow>(opts);
picker->Show(false);
picker->MoveTo(4, 2);
picker->onAccepted = [](const std::vector<std::string>& paths) { /* ... */ };
picker->onCancelled = [] { /* ... */ };
overlay->AddChild(picker);

// 按钮点击时
picker->Open();
// 外部主循环中查询
if (picker->IsDone()) { auto result = picker->Result(); }
```

- 目录导航：路径输入框回车跳转；“上级”按钮回到父目录。
- 列表：目录项显示 `[DIR]`；文件按当前筛选器匹配；单击选中，双击目录进入、双击文件直接返回；
  点“打开”时选中目录会进入该目录，选中文件则返回；多选模式下可一次返回多个文件。
- 文件夹模式：列表只显示目录；未选中任何目录时返回当前目录。
- `Show(false)` 只是隐藏；构造时不会扫描文件系统，`Open()` / `NavigateTo()` 才读取目录。
- 终端版不阻塞主循环，因此没有“模态等待”语义；调用方在 `IsDone()` 后取结果。

原生对话框在 MinGW 下需要额外链接 `ole32 / uuid / shell32`；CMake 已自动加上。

### 压缩包（Archive）

压缩包后端使用 **TerminalArchive.dll**：内部的 libarchive 静态库在进程内完成
zip/deflate/7z/rar 编解码，不调用 `bz.exe`，也不需要安装 Bandizip。C++ API 直接动态加载 DLL：

```cpp
#include <Terminal/Terminal.hpp>

Archive::Options options;
options.password = "secret";       // 空 = 不加密
options.level = 5;                 // 0=仅存储，9=最大压缩
options.overwrite = true;          // 解压时覆盖

// .zip / .7z 可创建；.rar 只读（后端没有 RAR 编码器）
auto created = Archive::Create("out/data.zip", { "assets", "README.md" }, options);
auto extracted = Archive::Extract("out/data.zip", "out/unpacked", options);
auto listed = Archive::List("out/data.zip", "secret");
auto tested = Archive::Test("out/data.zip", "secret");
```

- `Archive::Available()` 检查 exe 同目录的 `TerminalArchive.dll` 是否存在且导出完整。
  构建目录里 DLL 与 demo 同目录，直接运行即可；分发时把 DLL 和 exe 放一起。
- 支持 `.zip`、`.7z`、`.rar` 解压；`.zip` 与 `.7z` 创建。
- ZIP 创建支持 ZipCrypto 密码；ZIP / RAR 解压支持密码，错误密码返回 `ok=false`。
  libarchive 不能读取“加密头”的 7z（Bandizip 的带密码 7z 默认如此），会明确报错。
- 创建格式由扩展名自动推断，也可用 `Options::format` 显式指定。
- 不经过 `cmd` / PowerShell，也不启动子进程；密码只作为 C ABI 参数传入。
- DLL 缺失时不会崩溃：`Available()` 返回 false，API 返回带 `error` 的失败结果。

### 音频（AudioPlayer）

```cpp
AudioPlayer player;
player.Open("music.mp3");          // MP3 / FLAC / WAV / M4A；MF 不支持时自动尝试 MCI
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

- 首选 Media Foundation（`mfplay.dll`），实测 MP3 / FLAC / WAV 均可播放，`SetSpeed` 对 WAV 返回成功；MCI 只作为兜底。
- 两个后端都用 `LoadLibraryW` + `GetProcAddress`，使用方不需要额外 `-lwinmm/-lmfplay`。
- `SpeedSupported()` 可查询当前文件和后端是否接受倍速；MCI 对 waveaudio 的 `speed` 通常不支持。
- `OnMessage` 支持 `play / pause / resume / stop / seek / speed`，方便接页面消息队列。
- `music_player_demo` 扫描 `D:\CloudMusic`（递归，MP3/FLAC/WAV/M4A/AAC/OGG/WMA），
  页面内嵌左右两栏：左侧 `ChoiceGroup` 歌单，右侧进度 / 倍速 `Slider`、播放按钮和 LRC 时间轴编辑器；
  不再使用 `Window` / `OverlayHost`，因此不会阻塞拖动渲染。`OnUpdate()` 每帧泵 MF 消息，播完自动下一首。
  进度条拖动时暂停自动回写，因此暂停状态也可以直接拖到任意位置，松开后才 `Seek`。

### 歌词（Lyrics / LRC 编辑）

```cpp
auto doc = Lyrics::Document::LoadForAudio("music.mp3"); // 同目录同名 .lrc，找不到返回空文档
doc.lines.push_back({ 12.5, "新的歌词" });              // 秒 + 文本
doc.lines[0].text = "改写第一句";
doc.SaveForAudio("music.mp3");                         // 写回 music.lrc（自动创建父目录）

auto now = doc.At(player.Position());                  // 当前时间对应的歌词，空串表示还没到
```

- `Parse` 支持 `[ti:] / [ar:] / [al:] / [offset:]`、`[mm:ss]`、`[mm:ss.xx]` 与一行多时间标签。
- `FormatTime` / `ParseTime` 是 `mm:ss.xx` 的互转；`Serialize` 会按时间排序并保留元信息。
- `SavePath` / `SaveForAudio` 写 UTF-8 `.lrc`；`LrcPathForAudio` 只推导路径，不要求文件存在。
- demo 的 LRC 时间轴：单击一行选中，`▶` 标出当前播放句；`从此播放` 跳到选中句时间并开始播放；
  `时间` 输入框和 `歌词` 输入框都可按 Enter 应用，改时间后自动按时间重排；`此刻插入` 在当前播放
  位置插入，`删除所选` 删除选中句，`保存` 写回同名 `.lrc`；切歌前会自动保存未提交的修改。
  播放推进时只刷新新旧两行，不重建整条时间轴；播放跟随只在当前句走出视口时滚动一行。

### 边框兼容（只判断 ANSI / Unicode 能力）

库不再逐帧扫描缓冲区、猜测“宽字符有没有把边框顶歪”。兼容模式只在渲染入口
按终端能力决定一次：

```cpp
if (Terminal::Box::DetectAsciiFallback()) {
    // 统一使用 + - | 边框（Box::Ascii）
}
```

`Box::IsAnsiSupported()` 的判定：

1. `WT_SESSION`、`TERM_PROGRAM`、`ConEmuANSI`、`ANSICON`，或 `TERM` 含
   `xterm / screen / tmux / cygwin` → 现代终端，保留 Unicode；
2. 输出代码页不是 UTF-8 → ASCII；
3. Win11（build 22000+）或已开启 `ENABLE_VIRTUAL_TERMINAL_PROCESSING` → 保留 Unicode；
4. 其余旧 conhost → ASCII。

`TERMINAL_BOXES=ascii` / `unicode` 仍可强制覆盖。这样可以避免悬浮 `Window` 盖住
底层半个汉字、缩放把手 `+` 或滑杆 `●` 紧贴边框时，被误判成排版错乱而整屏切 ASCII。

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
- **指针样式**：`UI::SetMouseCursor()` / `CursorAt()` 声明控件语义。默认输入框 `IBeam`，
  按钮 / 开关 / 选项 / 滑杆 `Hand`，`ScrollView` 竖条 `SizeNS`、横条 `SizeWE`，
  `Window` 四边 `SizeWE/SizeNS`、四角 `SizeAll`、标题栏 `SizeAll`。
  `MouseRouter::ApplyCursor()` 在形状变化时调用 Win32 `SetCursor`；Windows Terminal / conhost
  的指针由宿主窗口管理，部分环境会忽略该请求，此时仍保留控件自己的悬停高亮。
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

自检已从 demo 中拆出，由 CTest 运行（1 个逻辑自检 + 2 个 Archive DLL 测试 + 4 个 demo 的 `--frame` 冒烟测试）：

```powershell
cd build; ctest --output-on-failure
# 或单独运行：build\terminal_selftest.exe
```

四个演示各自独立，参数一致：

```powershell
terminal_demo --frame        # 样式展示：边框 / 排布 / TStrFmt / 滚动 / 两个 Window（文字 + 单选框）
table_editor_demo --frame    # 表格编辑器：可编辑 / 姓名筛选 / 添加行 / 导出 CSV·XML
music_player_demo --frame    # 音乐播放器（页面内嵌，无 Window）：播放控制 + LRC 时间轴流式编辑/保存
zip_tool_demo --frame        # 压缩包工具：zip·7z 创建 / 解压 / 列表 / 测试 / 密码

# 任意 demo 都支持：
#   --dump       打印布局树（bounds + pref，排查布局问题）
#   --bench 200  渲染性能基准（脏标记 / 全量重绘 / ANSI 生成三条数据）
#   --truecolor  强制 ANSI 真彩；--mono 强制 16 色属性
```

不带参数启动即为交互模式：Tab 切换焦点、鼠标点击/拖动/滚轮、Esc 或 q 退出。
`terminal_selftest` 现在由 CTest 调用，返回非 0 即失败。

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
- 边框兼容模式替换边框字符和圆点（`●` → `·`）；滑杆轨道 `━`、滚动条 `█` / `│` / `─`、
  图片半格 `▀` 仍使用 Unicode，非 UTF-8 终端里需要自行换主题/字符或关闭这些元件。
- `UIImage` 用半格像素，纵向分辨率只有「行数 ×2」；缩放是最近邻，没有抗锯齿。
  目前只解 PNG / JPG 静态帧，GIF / WebP / APNG 动图不解析；运行时动态加载 `windowscodecs.dll` / `ole32.dll`。
- `ScrollView` 已实现横/竖双轴滚动与滑条；嵌套滚动和滚动惯性暂未做；
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
- `Window` 作为普通子元素时只在父容器内部浮动，不能突破 `UI::Render` 的 `PushClip(bounds)`；
  库内置的 `OverlayHost` 只把可见子窗口作为命中区域，因此窗口可以拖到全屏，窗口外的点击会穿透到下层。
  库本身不提供全屏遮罩、模态焦点锁和跨父级 Z 序。
- `AudioPlayer` 的 MF 后端依赖 `Update()` 泵窗口消息；不调用时可能停在 Stopped。
  MCI 兜底对 waveaudio 通常不支持 `speed`，`SetSpeed` 会返回 false。
- 兼容模式只判断终端是否支持 ANSI/Unicode，不再逐帧检查缓冲区里的宽字符对齐。
  因此终端字体缩放、第三方终端把 CJK 渲染成非标准宽度时，库不会自动修字符；
  需要用户调整终端字体或显式 `TERMINAL_BOXES=ascii`。
- `Window` 的半格 `UIImage` 按字符格近似显示，不读取终端真实像素宽度。
- JsonCore 不是纯头文件，`JsonCore.cpp / Number.cpp` 必须参与编译；CMake 已自动归档，
  直接用 g++ 时需要显式带上这两个源文件。
- `Json` 是值语义包装：普通拷贝、`get()` / `GetArray()` 遍历都会得到独立副本；
  只有 `operator[]` 返回指向父节点的视图（用于 `json["key"] = value` 这种写入）。
- MinGW 下的 `terminal_demo` 默认用 `-static` 链接运行时，避免系统 PATH 里其他 MinGW
  （例如 CLion 自带旧版）抢先加载 `libstdc++-6.dll`，出现“找不到程序入口点”。
  如果改成动态链接，必须保证 `C:\mingw64\bin` 排在 PATH 前面，或把
  `libstdc++-6.dll / libgcc_s_seh-1.dll / libwinpthread-1.dll` 复制到 exe 同目录。
- 剪贴板走 Win32 `CF_UNICODETEXT`：无剪贴板权限（服务/会话隔离）时复制粘贴静默失败并返回 false。
- 鼠标坐标依赖 `srWindow` 原点换算；窗口被拖出滚动区时坐标仍按当前可视窗口计算。
