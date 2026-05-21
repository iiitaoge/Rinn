# Project Rinn

<p align="center">
  <b>一个基于 C++20 / ECS / Lua / raylib 的 HD-2D 游戏引擎原型</b>
</p>

<p align="center">
  <img alt="C++20" src="https://img.shields.io/badge/C++-20-00599C?style=flat-square&logo=cplusplus&logoColor=white">
  <img alt="CMake" src="https://img.shields.io/badge/CMake-3.28+-064F8C?style=flat-square&logo=cmake&logoColor=white">
  <img alt="raylib" src="https://img.shields.io/badge/raylib-5.5-FFFFFF?style=flat-square&logo=raylib&logoColor=black">
  <img alt="Lua" src="https://img.shields.io/badge/Lua-sol2-2C2D72?style=flat-square&logo=lua&logoColor=white">
</p>

Project Rinn 是一个从零实现核心机制的游戏引擎学习项目。它以自研 ECS 为底座，集成 raylib 渲染、Lua 脚本、Tiled 地图加载、Dear ImGui 调试面板，以及一个围绕需求、情绪、评价、决策、执行和对白展开的 NPC AI 管线。

项目目标不是封装一个大而全的通用引擎，而是把游戏运行时里最关键的机制拆开、写透、测清楚：实体生命周期如何管理，组件如何连续存储，系统如何组织每帧数据流，脚本如何驱动场景，HD-2D 视觉风格如何落到渲染管线里。

## Highlights

| 模块 | 当前能力 |
|---|---|
| ECS Core | Entity 句柄、版本号复用、SparseSet、Registry、多组件 View |
| Rendering | raylib + Camera3D，地面瓦片与立式精灵渲染，基础法线光照验证 |
| Scripting | Lua 5.4 + sol2，脚本创建实体、挂载组件、驱动地图与逻辑 |
| Resources | 纹理资源 ID 化管理，支持角色贴图、法线贴图、地图资源 |
| Gameplay Systems | 物理、碰撞、输入、音频、事件、实体命名、对白气泡 |
| NPC AI | Need / Emotion / Appraisal / Decision / ActionExecution / Line 管线 |
| Debugging | Dear ImGui + rlImGui 调试面板，观察实体、组件和事件流 |
| Testing | GoogleTest 功能测试，cache / false sharing / pipeline 性能实验 |

## Tech Stack

- **Language**: C++20
- **Build**: CMake 3.28+
- **Rendering / Audio / Input**: raylib 5.5
- **UI**: Dear ImGui + rlImGui
- **Scripting**: Lua + sol2
- **Testing**: GoogleTest
- **Assets**: Tiled Lua map, PNG textures, normal maps, OGG audio

## Project Structure

```text
Project_Rinn/
├─ assets/              # 运行时资源：纹理、法线贴图、字体、音频、shader
├─ docs/                # 架构说明、审计报告、论文材料
├─ external/            # 本地第三方依赖：Lua、sol2
├─ scripts/             # Lua 场景、地图加载与 gameplay 脚本
├─ src/
│  ├─ Components/       # ECS 组件定义
│  ├─ Core/             # Entity、SparseSet、Registry、View
│  ├─ Resources/        # 资源管理
│  ├─ Scripting/        # Lua 初始化与 C++ 绑定
│  ├─ Systems/          # 渲染、物理、碰撞、AI、事件、音频等系统
│  └─ UI/               # ImGui 调试面板
├─ tests/               # 功能测试与性能 benchmark
├─ tools/               # 贴图切片与资源处理工具
└─ CMakeLists.txt
```

## Build

> 当前项目主要面向 Windows + MSVC 开发环境。

先确保本地依赖存在：

```text
external/lua/CMakeLists.txt
external/sol2/include/
```

配置并构建主程序：

```powershell
cmake -S . -B build
cmake --build build --config Debug
```

运行：

```powershell
.\build\Debug\Project_Rinn.exe
```

如果从 IDE 启动，请注意当前代码中部分资源路径仍依赖相对工作目录。推荐先从 CMake 构建目录运行，后续可把资源复制到可执行文件旁边来消除路径差异。

## Test

构建并运行全部功能测试与性能测试：

```powershell
.\scripts\run_tests.ps1
```

只运行 ECS 核心功能测试：

```powershell
.\scripts\run_tests.ps1 -Target core
```

只运行系统测试：

```powershell
.\scripts\run_tests.ps1 -Target systems
```

测试报告会生成到：

```text
reports/
```

## Runtime Flow

Project Rinn 的主循环集中在 `src/main.cpp`，大致数据流如下：

```text
Lua on_update
    ↓
PhysicSystem
    ↓
CollisionSystem
    ↓
EmotionDecaySystem
    ↓
DecisionSystem
    ↓
ActionExecutionSystem
    ↓
EventBus::Drain
    ↓
RenderSystem + LineSystem + Debug UI
```

这个顺序让脚本先提交意图，C++ 系统再统一推进世界状态，最后把事件、对白、画面和调试信息呈现出来。

## Core Design Notes

### Entity

Entity 使用 32 位句柄表达：

```text
[ generation: 16 bits | index: 16 bits ]
```

这样可以在实体销毁并复用 index 后，通过 generation 区分旧句柄，避免常见的悬空实体引用问题。

### SparseSet

组件池采用 SparseSet：

- `Sparse[entity.index]` 保存实体到 dense 下标的映射
- `Dense[]` 连续存储组件数据
- `DenseToEntity[]` 保存 dense 下标回到实体的映射

这让组件增删查保持接近 O(1)，同时让系统遍历尽量走连续内存。

### View

多组件查询会选择最小组件池作为驱动池，再检查实体是否同时拥有其他组件。这个策略减少了无效实体扫描，是当前 ECS 查询性能的核心优化点。


## Roadmap

- [x] ECS 核心：Entity / SparseSet / Registry / View
- [x] Lua 脚本绑定与地图加载
- [x] raylib 渲染、输入、音频基础集成
- [x] Dear ImGui 调试面板
- [x] NPC AI 管线原型
- [x] ECS 与系统测试
- [ ] 资源路径规范化与打包复制
- [ ] Render / Audio / Collision 全局状态收敛到显式 Context
- [ ] Lua 错误保护与运行时错误覆盖层
- [ ] HD-2D 后处理：bloom、tilt-shift、阴影与深度排序完善
- [ ] 可视化编辑器

## Development Philosophy

Project Rinn 的取向是先把机制写明白，再谈抽象。每个模块都尽量保留可以被测试、被审计、被论文引用的结构：ECS 核心强调数据布局，系统层强调帧内执行顺序，脚本层强调数据驱动，渲染层强调 HD-2D 风格验证。

这也是这个项目最适合继续扩展的地方：它不是把复杂性藏起来，而是把复杂性摆在可以观察、可以测量、可以改进的位置。
