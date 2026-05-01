# 第 3 章 引擎总体架构与模块划分

本章自顶向下介绍 Project Rinn 的总体架构。3.1 节明确设计目标与约束，3.2 节给出分层架构图并说明各层职责，3.3 节列出当前模块的文件组织，3.4 节解析每帧主循环的数据流。本章特别强调架构上为后续模块（编辑器、AI、动画等）所预留的扩展位置，以确保新增功能时不需要重排已有结构。

## 3.1 设计目标与约束

引擎的设计在如下几个维度上做出了显式取舍。

**目标平台与并发模型**。引擎以 Windows 桌面平台为首要目标，工具链使用 Visual Studio 2022 + MSVC。运行模型采取单线程主循环，所有 System 在同一帧内顺序执行。这一选择使得所有数据竞争问题在源头被消除，便于教学展示与单步调试，代价是放弃了多核扩展性；多线程调度被列入第 9 章的展望。

**容量上限与编译期约束**。为保证 SparseSet 的 Sparse 数组占用可控（每池约 32 KB），将最大实体数 `MAX_ENTITIES` 固定为 16384；为使签名（`std::bitset<MAX_COMPONENTS>`）压缩为 8 字节并便于在 64 位机器上一字操作，将最大组件类型数 `MAX_COMPONENTS` 固定为 64。两者均以 `constexpr` 常量声明在 `Core/Types.hpp` 中，便于编译期检查与位运算优化。

**组件类型的编译期校验**。所有数据组件必须满足 `std::is_aggregate_v` 与 `std::is_trivially_copyable_v` 两项约束，标签组件必须满足 `std::is_empty_v`。这些约束通过 `static_assert` 在 `Components/Components.hpp` 末尾集中校验，任何违反约束的组件定义会在编译期立即失败，避免了运行到序列化或拷贝阶段才发现问题的代价。

**扩展性优先于完备性**。在功能广度与可读性之间，引擎选择后者：每个子系统保持简洁、单一职责，宁可少做也不复杂化；组件、系统、脚本绑定的添加路径力求三步以内即可完成。这与本课题"自下而上的可拆解样本"定位相一致。

## 3.2 分层架构图

Project Rinn 的整体结构可划分为五层，自底向上分别是：核心层、子系统层、资源与脚本层、调试层、应用层。下图给出了模块在各层的归属，虚线方框表示当前未实现、为后续工作预留的位置。

```
┌─────────────────────────────────────────────────────────────────┐
│  应用层 (Application)                                           │
│  ┌──────────────┐   ┌─────────────┐   ┌──────────────────────┐ │
│  │  scripts/    │   │  scripts/   │   │  (预留: 可视化编辑器  │ │
│  │  main.lua    │   │  剧情数据   │   │   / AI 行为蓝图)      │ │
│  └──────────────┘   └─────────────┘   └──────────────────────┘ │
├─────────────────────────────────────────────────────────────────┤
│  调试层 (Debug)                                                 │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │  DebugUI (Dear ImGui + rlImGui)                          │   │
│  └──────────────────────────────────────────────────────────┘   │
├─────────────────────────────────────────────────────────────────┤
│  资源与脚本层 (Resources & Scripting)                           │
│  ┌──────────────────┐  ┌──────────────────────────────────┐    │
│  │  ResourceManager │  │  ScriptContext + LuaBinder (sol2)│    │
│  └──────────────────┘  └──────────────────────────────────┘    │
├─────────────────────────────────────────────────────────────────┤
│  子系统层 (Systems)                                             │
│  ┌─────────┐ ┌─────────┐ ┌──────────┐ ┌───────┐ ┌──────────┐  │
│  │ Render  │ │ Physic  │ │Collision │ │ Input │ │  Audio   │  │
│  └─────────┘ └─────────┘ └──────────┘ └───────┘ └──────────┘  │
│  ┌── 预留: Animation / AI / Editor 子系统 ─────────────────┐   │
│  └──────────────────────────────────────────────────────────┘   │
├─────────────────────────────────────────────────────────────────┤
│  核心层 (Core ECS)                                              │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │  Entity 句柄 │ EntityPool │ SparseSet<T> │ Registry     │   │
│  │              │            │ View<T...>   │ ComponentID  │   │
│  └──────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
                          ▲
                          │  raylib 5.5 / lua / sol2 / Dear ImGui (第三方依赖)
```

**核心层**承载 ECS 的基础抽象，不依赖任何其他模块；它所提供的 Entity、组件池、Registry、View 是引擎的"语法基底"。

**子系统层**为引擎提供"动作能力"。每个子系统通过 `Registry&` 访问数据，对外暴露最小 API（如 `RenderSystem::DrawSprites(reg, res)`、`PhysicSystem::update(reg, dt)`），系统之间不直接互相依赖。预留的虚线框表示未来加入的 AnimationSystem / AISystem / EditorSystem 等可以按同样模式插入此层而不影响其他模块。

**资源与脚本层**承担 ECS 与外部世界（磁盘资源、用户脚本）之间的桥接。`ResourceManager` 使用 ID 化的纹理句柄替代裸指针；`LuaBinder` 把组件操作、资源加载、输入查询、相机控制等接口暴露给 Lua。

**调试层**仅在开发与教学场景下启用，基于 Dear ImGui 提供实时实体检视器，能够在运行时直观查看任意组件池的内容。

**应用层**由 Lua 脚本承担。`scripts/main.lua` 是引擎启动后唯一被加载的入口脚本，它负责加载 Tiled 地图、放置玩家与 NPC、声明 `on_update` 钩子；`scripts/dialogue_data.lua` 等则以纯数据形式承载具体内容。

值得强调的是，**未来加入编辑器或 AI 行为系统时，所需的修改全部位于"子系统层"或"应用层"**，不会侵入核心层，也不需要拆改前述章节所描述的任何机制。这正是本架构在毕业设计之外保持持续演化能力的关键。

## 3.3 模块清单与文件结构

引擎源码统一组织在 `src/` 目录下，按子目录区分模块。表 3.1 给出当前模块清单与对应文件。

**表 3.1 Project Rinn 模块清单**

| 模块 | 文件 | 职责 |
|------|------|------|
| 核心类型 | `src/Core/Types.hpp` | Entity 句柄、Entity_index、Signature、容量常量 |
| 组件 ID | `src/Core/ComponentID.hpp` | 模板化组件类型 ID 分配 |
| 稀疏集 | `src/Core/SparseSet.hpp` | `ISparseSet` 接口与 `SparseSet<T>` 模板 |
| 注册表 | `src/Core/Registry.hpp` | `EntityPool`、`Registry`、`View<T...>` |
| 组件 | `src/Components/Components.hpp` | `Transform`、`Sprite`、`Velocity`、`Collider`、`TextBubble`、标签组件 |
| 资源 | `src/Resources/ResourceManager.hpp` | 纹理 ID 化与生命周期管理 |
| 脚本 | `src/Scripting/ScriptContext.hpp` | Lua state 初始化 |
| 脚本绑定 | `src/Scripting/LuaBinder.hpp` | sol2 双向绑定 |
| 渲染 | `src/Systems/RenderSystem.hpp` | 3D 相机、精灵 3D 化、字体、对话气泡 |
| 物理 | `src/Systems/PhysicSystem.hpp` | 位置积分 |
| 碰撞 | `src/Systems/CollisionSystem.hpp` | 空间哈希 + AABB + MTV 解算 |
| 输入 | `src/Systems/InputSystem.hpp` | raylib 输入封装 |
| 音频 | `src/Systems/AudioSystem.hpp` | 单 BGM 流 |
| 调试 UI | `src/DebugUI/DebugUI.hpp` | 实体检视器 |
| 入口 | `src/main.cpp` | 主循环 |

构建系统采用 CMake 3.28，第三方依赖通过 `FetchContent`（raylib、Dear ImGui、rlImGui、GoogleTest）与本地子目录（lua、sol2）混合管理；测试通过 `cmake -DBUILD_TESTS=ON ..` 显式启用。MSVC 编译选项设置为 `/W4 /permissive- /utf-8`，强制项目维持高标准的诊断输出与严格的标准遵循。

## 3.4 主循环与数据流

引擎的主循环位于 `src/main.cpp`，结构极为简洁。启动阶段依次完成窗口、音频、调试 UI 与 ECS 注册表的初始化，然后通过 `lua.script_file` 加载 `scripts/main.lua` 并执行其顶层语句（建立场景、放置实体、订阅 `on_update`），最后进入 `while (!ShouldClose())` 循环。

```
┌────────────────────────────────────────────────┐
│  Init: Window / Audio / DebugUI / Registry     │
│        ResourceManager / Lua state / Bindings  │
└────────────────────────┬───────────────────────┘
                         │
                         ▼
                ┌────────────────────┐
                │  Load main.lua     │  ← 顶层执行：建立场景
                └────────┬───────────┘
                         │
        ┌────────────────▼─────────────────┐
        │  while (!ShouldClose())          │
        │   ┌──────────────────────────┐   │
        │   │ 1. BeginFrame (3D)       │   │
        │   │ 2. lua["on_update"]()    │   │  ← 策略层
        │   │ 3. PhysicSystem::update  │   │  ← 机制层
        │   │ 4. CollisionSystem::    │   │
        │   │    detect + resolve     │   │
        │   │ 5. RenderSystem::        │   │
        │   │    DrawSprites           │   │
        │   │ 6. EndCameraMode         │   │
        │   │ 7. DrawTextBubbles       │   │  ← 屏幕空间 UI
        │   │ 8. AudioSystem::Update   │   │
        │   │ 9. DrawText (FPS)        │   │
        │   │10. DebugUI::Draw         │   │
        │   │11. EndFrame              │   │
        │   └──────────────────────────┘   │
        └──────────────────────────────────┘
```

执行顺序背后体现了几条设计原则。

**先 Lua 后 C++**：每帧开头先调用 `lua["on_update"]()`，让脚本完成"读取输入 → 设置玩家速度 → 推进剧情状态机"等高层逻辑；随后 C++ 物理系统按既定 dt 推进位置；接着碰撞系统校正位置以避免穿模。这种"策略可崩，机制不能崩"的分层意味着即使 Lua 出现错误，C++ 物理与渲染仍能继续推进，便于在 Lua 端做容错与降级。

**渲染分两段**：在 `BeginMode3D / EndMode3D` 之间提交 3D 精灵；退出 3D 模式后再用 `GetWorldToScreen` 将世界位置投影到屏幕坐标，绘制对话气泡等屏幕空间 UI。这种两段式避免了 UI 元素被透视相机扭曲，又复用了相同的世界坐标。

**调试 UI 在最后**：DebugUI 在所有游戏内容之上叠加，确保不会被场景遮挡，且在屏幕空间响应鼠标。

**未来扩展的插入点**。以下是可预见的扩展方向及其在主循环中的预期插入位置：

- **AnimationSystem**：插在 PhysicSystem 之后、CollisionSystem 之前，根据状态机更新精灵帧。
- **AISystem**：插在 Lua `on_update` 与 PhysicSystem 之间，负责由 C++ 主导的高频 AI 决策；Lua 仅承担低频策略。
- **EditorSystem**：与 DebugUI 同层，但单独占据 ImGui 子窗口；编辑模式下可暂停 PhysicSystem 与 CollisionSystem。
- **PostProcessingSystem**：插在所有 3D 提交之后、UI 之前，承担 bloom / tilt-shift 等离屏渲染。

这些扩展均不需要改动现有 System 的接口或 ECS 核心。

---

至此，引擎的总体架构已经清晰。第 4 章将深入核心层，详细介绍 ECS 各组成部分的设计与实现。
