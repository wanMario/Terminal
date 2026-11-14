# JsonCore / wjson 性能基准

独立于 `TerminalUI` 的微基准，源码为 `bench/json_bench.cpp`。只比较 JSON 解析、遍历和序列化，不包含 UI 渲染。

## 测试条件

- 机器：Windows / MinGW-W64 GCC 14.2.0
- 编译：`-std=c++20 -O2 -DNDEBUG`
- 输入：一个 174,263 字节的 JSON 对象，`elements` 数组含 1500 个元素；每个元素有 `id / name / score / enabled / tags / nested`
- 每项测试跑 3 轮，取最好一轮
- `parse+walk` 120 次，`dump` 80 次
- 三个后端使用同一份输入，校验和一致：`676175400.0`

## 结果

| 后端 | parse + 遍历 (ms/次) | dump (ms/次) |
|---|---:|---:|
| 旧 `agl/wjson`（全局 `class JSON`） | 14.2443 | 17.4292 |
| JsonCore 原生（`JSON::JsonElement`） | **13.0064** | **2.3265** |
| `Terminal::Json` 适配层 | 23.0834 | 2.5562 |

## 解读

- JsonCore 原生解析 + 遍历比 wjson 快约 **8.7%**（14.2443 → 13.0064 ms）。
- JsonCore 的 `dump` 比 wjson 快约 **7.5 倍**（17.4292 → 2.3265 ms）。
- `Terminal::Json` 的 `dump` 基本保持 JsonCore 水平，只多约 9.9% 的包装开销。
- `Terminal::Json` 的 `parse+walk` 明显慢于原生 JsonCore，原因是基准里 `root.get("elements")` 会深拷贝整个数组；这是适配层为保证值语义付出的开销。UI 的 `LoadFromJSON` 每次载入只发生一次，不在每帧热路径上。

## 复现

在 `Terminal` 目录下执行：

```powershell
$env:PATH="C:\mingw64\bin;"+$env:PATH

# 旧 wjson
g++ -std=c++20 -O2 -DNDEBUG -DBENCH_WJSON `
    -ID:/wan1/C++/Libraries/agl/DataStructure/advanced/wjson `
    bench/json_bench.cpp -o bench_wjson.exe

# JsonCore 原生
g++ -std=c++20 -O2 -DNDEBUG -DBENCH_JSONCORE `
    -ID:/wan1/C++/Libraries/ToolHead/DataStructure/Json `
    bench/json_bench.cpp ../DataStructure/Json/JsonCore.cpp ../DataStructure/Json/Number.cpp `
    -o bench_jsoncore.exe

# Terminal::Json 适配层
g++ -std=c++20 -O2 -DNDEBUG -DBENCH_WRAPPER `
    -ID:/wan1/C++/Libraries/ToolHead -ID:/wan1/C++/Libraries/ToolHead/Terminal `
    -ID:/wan1/C++/Libraries/ToolHead/DataStructure/Json `
    bench/json_bench.cpp ../DataStructure/Json/JsonCore.cpp ../DataStructure/Json/Number.cpp `
    -o bench_wrapper.exe

./bench_wjson.exe
./bench_jsoncore.exe
./bench_wrapper.exe
```

这是一组单机相对结果，不代表所有负载下的绝对差距；正式决策建议在目标机器上重复跑几次。
