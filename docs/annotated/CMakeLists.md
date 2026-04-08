# CMakeLists.txt — 构建系统（深度注释版）

> 文件路径: `CMakeLists.txt`  
> 角色: CMake 构建配置。定义项目结构、第三方库管理、编译选项、可选测试。

---

## 文件级设计意图

**CMake 的角色**: 跨平台构建系统生成器。CMake 本身不编译代码——它**生成**其他构建系统的配置文件（Visual Studio .sln、Makefiles、Ninja 等）。

**依赖管理策略**: 本项目混合使用两种方式:

| 方式 | 用于 | 优势 | 缺陷 |
|------|------|------|------|
| **FetchContent (自动下载)** | Raylib, ImGui, Google Test | 一键构建，无需手动准备 | 首次构建慢，依赖网络 |
| **本地子目录** | Lua, Sol2 | 不依赖网络，版本完全可控 | 需要手动下载放到 `external/` |

---

## 逐行注释

### 项目基础

```cmake
cmake_minimum_required(VERSION 3.28)
```

> **语法知识 — CMake 版本要求**:
>
> CMake 每个版本引入新功能/策略。`3.28` 要求确保 `FetchContent`、C++20 支持等功能可用。如果用户的 CMake 版本低于 3.28 → 配置时直接报错。

```cmake
project(Project_Rinn VERSION 0.1.0 LANGUAGES CXX C)
```

> **`LANGUAGES CXX C`**: 启用 C++ 和 C 编译器。Lua 源码是 C 语言，需要 C 编译器。不声明 C → Lua 编译失败。

```cmake
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
```

> **`CXX_STANDARD 20`**: 要求 C++20。`REQUIRED ON` = 如果编译器不支持 C++20 直接报错（而非静默降级到 C++17）。

---

### Raylib — FetchContent 拉取

```cmake
include(FetchContent)
FetchContent_Declare(raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        5.5
)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(raylib)
```

> **语法知识 — FetchContent 三步曲**:
>
> 1. `include(FetchContent)` — 加载 CMake 模块
> 2. `FetchContent_Declare(name ...)` — 声明依赖（名称、来源、版本）
> 3. `FetchContent_MakeAvailable(name)` — 实际下载并添加到构建
>
> **首次 `cmake --configure` 时**: 从 GitHub 克隆 raylib 仓库到 `_deps/raylib-src/`。后续构建复用缓存，不重新下载。
>
> **`GIT_TAG 5.5`**: 锁定具体版本。保证可复现构建——即使 raylib 仓库更新了也不受影响。
>
> **`set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)`**: Raylib 默认编译示例程序（~30 个），非常慢。`FORCE` 强制覆盖 Raylib CMakeLists.txt 中的默认值。

---

### Lua — 本地子目录

```cmake
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/external/lua/CMakeLists.txt")
    message(FATAL_ERROR "❌ 找不到 Lua！...")
endif()
add_subdirectory(external/lua)
```

> **`add_subdirectory(path)`**: 将指定目录中的 `CMakeLists.txt` 作为子项目处理。子项目的 target 会自动加入当前构建。

**为什么 Lua 不用 FetchContent？** 可能的原因:
1. 使用的 Lua 版本是第三方 CMake 包装（如 `marovira/lua`），而非官方 Lua 仓库
2. 开发者偏好手动管理核心依赖的版本

---

### Sol2 — INTERFACE 库

```cmake
add_library(sol2 INTERFACE)
target_include_directories(sol2 INTERFACE "${CMAKE_SOURCE_DIR}/external/sol2/include")
target_link_libraries(sol2 INTERFACE liblua)
```

> **语法知识 — `INTERFACE` 库**:
>
> CMake 的三种库类型:
> | 类型 | 有 .cpp 要编译？ | 用途 |
> |------|----------------|------|
> | `STATIC` | ✓ | 静态链接库 (.lib/.a) |
> | `SHARED` | ✓ | 动态链接库 (.dll/.so) |
> | **`INTERFACE`** | **✗** | **纯头文件库** |
>
> Sol2 是 header-only：没有 `.cpp` 文件要编译。`INTERFACE` 库只传播 include 路径和依赖给使用者。
>
> **`INTERFACE` 传播语义**: `target_link_libraries(sol2 INTERFACE liblua)` 表示"任何链接 sol2 的 target 也自动链接 liblua"。这是传递性依赖。

---

### 主可执行文件

```cmake
add_executable(${PROJECT_NAME}
    src/main.cpp
    src/Core/ComponentID.hpp
    ...
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${rlimgui_SOURCE_DIR}/rlImGui.cpp
)
```

> **`.hpp` 文件在 target 中的作用**:
>
> 头文件列在 `add_executable` 中**不会被编译**（只有 `.cpp` 被编译）。但它们会出现在 IDE 的项目文件列表中，方便导航。

> **`${imgui_SOURCE_DIR}`**: FetchContent 自动设置的变量，指向下载的 imgui 源代码目录。ImGui 有 `.cpp` 文件必须编译——它不是 header-only 的。

---

### 编译选项

```cmake
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${imgui_SOURCE_DIR}
    ${rlimgui_SOURCE_DIR}
)
```

> **语法知识 — `PUBLIC` / `PRIVATE` / `INTERFACE`**:
>
> | 关键字 | 谁能用这些路径 |
> |--------|-------------|
> | `PUBLIC` | 自己 + 所有链接到自己的 target |
> | `PRIVATE` | 只有自己 |
> | `INTERFACE` | 只有链接到自己的 target（自己不用） |
>
> 可执行文件通常用 `PRIVATE`——没人会链接一个 `.exe`。

---

### MSVC 编译选项

```cmake
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
    /W4
    /permissive-
    /utf-8
    /wd5321
    )
endif()
```

| 选项 | 含义 | 设计意图 |
|------|------|---------|
| `/W4` | 警告等级 4（次高） | 捕获常见 Bug，但不像 `/Wall` 那样产生大量第三方库噪音 |
| `/permissive-` | 严格标准模式 | 禁用 MSVC 非标准扩展（如 `::` 前缀的成员访问），确保代码可移植 |
| `/utf-8` | 源文件和执行字符集用 UTF-8 | 支持中文字符串字面量 |
| `/wd5321` | 禁用警告 C5321 | Sol2 产生的特定警告，无法修复 |

---

### Google Test — 可选测试

```cmake
option(BUILD_TESTS "Build ECS unit tests with Google Test" OFF)
```

> **`option(NAME "description" DEFAULT)`**: 定义用户可配置的开关。命令行 `cmake -DBUILD_TESTS=ON ..` 开启。

```cmake
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
```

> **MSVC CRT 链接**: MSVC 有两套 C 运行时库:
> - `/MD` (共享 CRT): 使用 msvcrt.dll
> - `/MT` (静态 CRT): 静态链接 CRT
>
> 项目和 Google Test 必须使用相同的 CRT，否则链接错误。`gtest_force_shared_crt ON` 强制 GTest 用 `/MD`，与 MSVC 默认行为一致。

```cmake
include(GoogleTest)
gtest_discover_tests(ecs_tests)
```

> **`gtest_discover_tests`**: CMake 在构建后运行测试可执行文件带 `--gtest_list_tests` 参数，自动发现所有测试用例。之后 `ctest` 可以运行它们。

---

## 文件级总结

| 设计决策 | 选择 | 理由 |
|---------|------|------|
| 依赖管理 | FetchContent + 本地 | 核心依赖精准控制，辅助依赖自动拉取 |
| C++ 标准 | C++20 REQUIRED | 需要 concepts, format, countr_zero |
| 库类型 | Sol2=INTERFACE | 纯头文件库 |
| 测试 | 可选 OFF | 不强制所有开发者编译测试 |
