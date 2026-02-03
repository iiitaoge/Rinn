# PROJECT RINN: 系统架构与编码规范

**Systemic Narrative Engine** - 系统性涌现玩法引擎

---

## 1. 项目身份 (Identity)

| 属性 | 描述 |
|------|------|
| **目标** | 基于 C++20 的 HD-2D 风格系统性涌现引擎 |
| **核心哲学** | Data-Oriented Design (DOD) 优于 OOP |
| **验收标准** | 工业级健壮（内存安全）、高性能（10k+ 实体）、高扩展性（Lua 驱动） |

---

## 2. 技术栈 (Tech Stack)

### 核心技术

| 技术 | 用途 | 备注 |
|------|------|------|
| **C++20** | 核心语言 | Concepts, `std::format`, `std::ranges` |
| **Handmade ECS** | 架构 | Sparse Set, Entity Handle (ID + Generation) |
| **Lua 5.4 + Sol2** | 脚本 | C++ 负责机制，Lua 负责策略 |
| **Raylib + rlgl.h** | 渲染/平台 | 高级 API + 低级渲染控制 |
| **ImGui** | 调试 UI | via rlImGui |
| **nlohmann/json** | 序列化 | 存档/配置 |

### ECS 架构规范

- **Storage**: Sparse Set (Sparse Array + Dense Array)，保证内存连续性
- **Entity**: `uint32_t` Handle = `[Generation (16 bits) | Index (16 bits)]`
- **禁止继承**: `class Player : public Entity` ❌ 违法
- **逻辑实现**: 通过 System 处理组件数据

### 渲染技术路线

```
┌─────────────────────────────────────┐
│        Raylib 高级 API              │ ← 窗口/输入/音频/纹理加载
├─────────────────────────────────────┤
│         rlgl.h 低级 API             │ ← FBO/深度/多纹理/Shader
├─────────────────────────────────────┤
│       直接 OpenGL (备用)            │ ← 如 rlgl 不足时使用
└─────────────────────────────────────┘
```

---

## 3. 核心架构法则 (The Iron Rules)

### 3.1 内存与性能

| 规则 | 说明 |
|------|------|
| **No O(N²)** | Update 循环禁止 N² 复杂度，必须用空间划分 |
| **Contiguous Memory** | 组件数据紧凑排列，System 遍历缓存友好 |
| **RAII 资源管理** | 禁止裸指针拥有资源，使用 `unique_ptr` 或 Handle |

### 3.2 代码风格

```cpp
// ❌ Bad
void Update() { 
    for(int i=0; i<entities.size(); i++) { ... } 
}

// ✅ Good  
void Update(float dt) { 
    for (auto [pos, vel] : registry.view<Position, Velocity>()) { 
        pos.x += vel.x * dt; 
    } 
}
```

| 规则 | 说明 |
|------|------|
| **No Raw Loops** | 使用 Range-based for 或 `std::ranges` |
| **Const Correctness** | 只读组件用 `const T&`，不修改成员的函数标 `const` |
| **Handle 验证** | 实体引用通过 Handle (ID + Generation) 检查有效性 |

### 3.3 Lua 安全

```cpp
// ❌ Bad - 裸调可能 Crash
lua.script("broken syntax");

// ✅ Good - 安全封装
auto result = lua.safe_script("code", sol::script_pass_on_error);
if (!result.valid()) { handle_error(); }
```

### 3.4 曳光弹法则 (Tracer Bullet)

> **如果一个系统看不见，它就不存在。**

- 开发任何功能必须闭环: `Input → Logic → State Change → Render → Visual Debug`
- 必须实现 Debug Draw (碰撞盒、AI 视野、寻路网格)

---

## 4. 目录结构

```
src/
├── Core/           # ECS 核心 (Registry, SparseSet, View, Types)
├── Systems/        # C++ 系统 (Render, Physics, Input, Audio, Animation)
├── Scripting/      # Lua 绑定 (ScriptContext, LuaBinder)
├── Resources/      # 资源管理 (Texture, Shader, Audio)
├── Events/         # 事件系统 (EventBus)
├── Scene/          # 场景管理 (SceneManager, Prefab)
├── components/     # 组件定义 (Transform, Sprite, Velocity, Light...)
└── main.cpp        # 入口

shaders/            # GLSL 着色器
scripts/            # Lua 脚本
assets/             # 资源文件 (textures/, audio/, lut/)
docs/               # 文档
```

---

## 5. 开发阶段

| Phase | 目标 | 状态 |
|-------|------|------|
| **Phase 1** | 可玩基础 (Input → Physics → Collision) | 🔴 进行中 |
| **Phase 2** | 完整游戏循环 (Audio, Animation, Scene, Save) | ❌ |
| **Phase 3** | HD-2D 渲染 (Shader, 法线, 光照, 后期) | ❌ |
| **Phase 4** | 专业级 (Editor, Hot Reload, AI) | ❌ |

**整体完成度**: ~30%

---

## 6. 相关文档

| 文档 | 内容 |
|------|------|
| [完整引擎架构](Project_Rinn_完整引擎架构.md) | 模块地图、组件清单、文件结构 |
| [HD-2D渲染路线图](HD-2D渲染实现路线图.md) | Shader/光照/后期实现计划 |
| [开发时间表](开发时间表.md) | 每日任务、里程碑 |
| [进度报告](progress.md) | 详细开发日志 |
| [美术规范](Project%20Rinn%20HD-2D%20引擎技术架构与美术资源规范.md) | 贴图命名、资源流程 |