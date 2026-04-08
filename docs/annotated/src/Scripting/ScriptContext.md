# ScriptContext.hpp — Lua 运行时初始化（深度注释版）

> 文件路径: `src/Scripting/ScriptContext.hpp`  
> 角色: 初始化 Lua 虚拟机，选择性开启标准库。仅 6 行有效代码，但设计决策值得深入分析。

---

## 文件级设计意图

**架构哲学**: "C++ 做机制，Lua 做策略"。
- **C++ 侧**: 提供引擎能力（渲染、碰撞、组件管理）
- **Lua 侧**: 决定游戏行为（敌人 AI、关卡逻辑、对话系统）

**为什么选 Lua 而非其他脚本语言？**

| 语言 | 集成难度 | 性能 | 生态 | 安全性 |
|------|---------|------|------|--------|
| **Lua** | **极简（2文件）** | **↑↑** LuaJIT 接近 C | 游戏行业标准 | ✓ 可沙箱 |
| Python | 中（CPython 嵌入） | ↓ GIL | 庞大 | ⚠️ 系统调用 |
| JavaScript | 难（V8 嵌入庞大） | ↑ JIT | Web 生态 | ⚠️ |
| C# | 难（Mono/CoreCLR） | ↑ | Unity 生态 | ✓ |
| Wren | 简单 | ↑ | 小众 | ✓ |

**Lua 胜出**: 极小的嵌入体积（~200KB）、极高的性能、游戏行业 30 年的使用历史（魔兽世界、Roblox、Corona SDK）。

---

## 逐行注释

```cpp
#pragma once
#include <sol/sol.hpp>
```

> **语法知识 — `sol/sol.hpp`**:
>
> Sol2 是纯头文件库 (header-only library)。`#include <sol/sol.hpp>` 会展开为大量模板代码，导致编译时间增加（可能 +5-10 秒）。这是 header-only 库的 trade-off: 零配置 vs 编译慢。
>
> Sol2 的层次:
> ```
> 你的代码 → Sol2 (C++ 封装) → Lua C API → Lua VM
> ```
> Sol2 把底层的 `lua_pushinteger`, `lua_getfield` 等 C 函数封装为类型安全的 C++ 接口。

---

```cpp
namespace Rinn {
    inline void Init_lua(sol::state& lua) {
        lua.open_libraries(sol::lib::base, sol::lib::math);
    }
}
```

> **语法知识 — `sol::state`**:
>
> `sol::state` 封装了 `lua_State*`。它是 RAII 的:
> - 构造时: 调用 `luaL_newstate()` 创建新的 Lua 虚拟机
> - 析构时: 调用 `lua_close()` 释放所有 Lua 内存
>
> **`sol::state&` 引用传参**: `sol::state` 不可拷贝（因为它独占一个 Lua VM），必须传引用。

**`open_libraries` — 标准库选择**:

| 库 | 提供的函数 | 是否开启 | 理由 |
|----|-----------|---------|------|
| `sol::lib::base` | `print, type, pairs, ipairs, tostring, error, pcall, select, unpack, require` | ✓ | 基本功能，脚本无法不用 |
| `sol::lib::math` | `math.sqrt, math.abs, math.floor, math.ceil, math.sin, math.cos, math.random` | ✓ | 游戏逻辑需要数学运算 |
| `sol::lib::string` | `string.format, string.find, string.sub, string.len, string.rep` | ✗ | 游戏逻辑少用字符串处理 |
| `sol::lib::table` | `table.insert, table.remove, table.sort, table.concat` | ✗ | 基础表操作用 `[]` 和 `#` 就够 |
| `sol::lib::io` | `io.open, io.read, io.write, io.close` | ✗ | **安全风险**：允许脚本读写任意文件 |
| `sol::lib::os` | `os.execute, os.remove, os.rename, os.clock` | ✗ | **安全风险**：允许脚本执行系统命令 |
| `sol::lib::debug` | `debug.getinfo, debug.sethook, debug.getlocal` | ✗ | **安全风险**：可以修改运行时环境 |
| `sol::lib::package` | `require, package.path` | ✗ | 不需要模块加载系统 |

**设计选择**: 最小权限原则。只开放脚本**必须**用到的功能，关闭所有有安全风险的 API。这在 Modding 场景尤为重要——恶意脚本无法通过 `os.execute` 执行系统命令。

**缺陷**: 如果后续需要文件操作（如保存/加载游戏存档），需要在 C++ 侧封装安全的文件 API 再暴露给 Lua，而非直接开放 `io` 库。
