# PROJECT RINN: SYSTEM ARCHITECTURE & CODING STANDARDS

## 1. 项目身份 (Identity)
* **目标**: 构建一个基于 C++20 的**系统性叙事引擎 (Systemic Narrative Engine / Emergent Sim)**。
* **核心哲学**: Data-Oriented Design (DOD) 优于 Object-Oriented Programming (OOP)。
* **验收标准**: 必须达到工业级健壮性（无崩溃、内存安全），高性能（支持 10k+ 实体），且具备高扩展性（Lua 驱动逻辑）。

## 2. 技术栈 (Tech Stack) - C++20 Strict
* **Language**: C++20 (MSVC/Clang). 必须使用 Concepts (`requires`), `std::format`, `std::span`, `std::ranges`。
* **Core Architecture**: Handmade ECS (Entity Component System).
    * **Storage**: Sparse Set (Sparse Array + Dense Array) 实现组件池，保证内存连续性。
    * **Identification**: `Entity` 只是一个 `uint32_t` ID。
    * **Polymorphism**: **禁止** 继承（如 `class Player : public Entity` 是违法的）。逻辑通过 System 处理组件数据来实现。
* **Scripting**: Lua 5.4 + Sol2.
    * **Safety**: 所有 C++ 调用 Lua 必须封装在 `safe_script` 或 `protected_function` 中，严禁裸调导致 Crash。
    * **Role**: C++ 负责机制 (Mechanism/Performance)，Lua 负责策略 (Policy/Gameplay)。
* **Rendering**: Raylib (封装层).
    * 直接使用 Raylib API，但必须通过 RAII 包装资源（Texture, Font, Audio）。
* **UI**: ImGui (via rlImGui) 用于调试工具和编辑器面板。
* **Serialization**: nlohmann/json。

## 3. 核心架构法则 (The Iron Rules)

### 3.1 内存与性能 (The Performance Mandate)
* **No O(N^2)**: 严禁在 Update 循环中进行 N^2 复杂度的交互。必须使用空间划分 (Spatial Partitioning, Grid/Quadtree)。
* **Contiguous Memory**: 组件数据必须紧凑排列。System 遍历必须是对缓存友好的 (Cache Friendly)。

### 3.2 代码风格与安全 (Code Aesthetics & Safety)
* **No Raw Loops**: 拒绝使用 `for (int i=0; i<n; i++)`。
    * **必须使用**: Iterators, Range-based for (`for (auto& entity : view)`), 或 `std::ranges::for_each`。
    * **Context**: 参考 `context_ecs.txt` 中的迭代器实现。
* **Const Correctness**:
    * 只读组件必须使用 `const T&`。
    * 不修改类成员的函数必须标记为 `const`。
* **Resource Management**:
    * **禁止**使用裸指针 (`T*`) 拥有资源。必须使用 `std::unique_ptr`, `std::shared_ptr` 或自定义的 Handle 系统。
    * **Handle System**: 实体引用必须通过 Handle (ID + Generation) 检查有效性，防止悬空指针访问。

### 3.3 曳光弹法则 (Tracer Bullet Workflow)
* 开发任何功能时，不要只写后端。
* **必须闭环**: Input -> Event -> Lua Logic -> ECS State Change -> Render -> **Visual Debug**.
* **Visual Verification**: 如果一个系统看不见，它就不存在。必须实现 Debug Draw (如：绘制 AI 的视野扇区、寻路网格、碰撞盒)。

## 4. 目录结构与模块 (Module Structure)
* `src/Core/`: ECS 核心 (Registry, SparseSet, View), 内存管理。
* `src/Systems/`: 纯 C++ 系统 (RenderSystem, PhysicsSystem)。
* `src/Scripting/`: Lua 绑定 (LuaBinder, ScriptContext)。
* `src/Resources/`: 资源管理器 (TextureManager, StringPool)。
* `src/Game/`: 游戏具体逻辑的入口，Main Loop。

## 5. 当前开发阶段 (Current Phase: Phase 1)
* **目标**: 建立核心循环 (Infrastructure & Loop)。
* **当前任务**:
    1.  完善基于 Sparse Set 的 ECS `Registry::view<T...>()` 功能。
    2.  实现安全的 Lua 绑定，使 Lua 能控制实体的创建和移动。
    3.  确保热重载 (Hot-reload) 机制可用。

## 6. 指令示例 (Prompt Examples)
当编写代码时，Cursor 必须遵循：
* ❌ **Bad**: `void Update() { for(int i=0; i<entities.size(); i++) { ... } }`
* ✅ **Good**: `void Update(float dt) { for (auto [pos, vel] : registry.view<Position, Velocity>()) { pos.x += vel.x * dt; } }`

* ❌ **Bad**: `if (entity.type == "Wolf") { Attack(sheep); }` (Hard-coding)
* ✅ **Good**: `lua["on_tick"](entity_handle);` (Script-driven)