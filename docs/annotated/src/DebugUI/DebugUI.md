# DebugUI.hpp — ImGui 调试面板（深度注释版）

> 文件路径: `src/DebugUI/DebugUI.hpp`  
> 角色: 基于 Dear ImGui + rlImGui 的实时调试界面。

---

## 文件级设计意图

**Dear ImGui 的核心哲学 — 即时模式 GUI (IMGUI)** vs 传统 **保留模式 GUI (RMGUI)**:

| 特性 | 即时模式 (ImGui) | 保留模式 (Qt/WPF/HTML) |
|------|-----------------|----------------------|
| 状态管理 | **无**，每帧重建 UI 树 | 有，UI 元素持久存在 |
| 代码风格 | 过程式，if/for 即 UI | 声明式，绑定/事件驱动 |
| 适合 | **调试工具**、编辑器原型 | 生产级应用 |
| 学习曲线 | 极低 | 高 |

**为什么调试 UI 用 IMGUI？** 调试面板需求变动频繁（添加新组件、新字段），IMGUI 只需多写一行 `ImGui::Text(...)` 即可，无需修改 UI 树结构、信号连接等。

---

## 逐行注释

### Init — 初始化

```cpp
inline void Init() { rlImGuiSetup(true); }
inline void Shutdown() { rlImGuiShutdown(); }
```

**`rlImGuiSetup(true)`**: `true` = 启用暗色主题。rlImGui 负责：
1. 创建 ImGui 上下文 (`ImGui::CreateContext()`)
2. 设置 Raylib 输入后端（把 Raylib 的键盘/鼠标事件转发给 ImGui）
3. 设置 Raylib 渲染后端（把 ImGui 的绘制指令翻译为 Raylib 绘制调用）

---

### DrawEntityInspector — 实体检查器

```cpp
inline void DrawEntityInspector(Registry& reg) {
    ImGui::Begin("Entity Inspector");
```

> **语法知识 — `ImGui::Begin` / `ImGui::End` 配对**:
>
> `Begin("title")` 创建窗口。所有在 `Begin` 和 `End` 之间的 ImGui 调用都绘制在该窗口内。
>
> **必须配对**: 每个 `Begin` 都必须有对应的 `End`，否则 ImGui 内部栈错乱 → 崩溃。
> 
> `Begin` 返回 `bool`: `true` = 窗口可见（未折叠），`false` = 已折叠。即使返回 `false`，也**必须调用 `End`**。

---

```cpp
    static bool show_transform = true;
    static bool show_velocity = false;
    static bool show_sprite = false;
    static bool show_collider = false;
```

> **语法知识 — `static` 局部变量在即时模式 GUI 中的角色**:
>
> IMGUI 没有持久化的 UI 对象。那复选框的"选中/未选中"状态存在哪里？答案: `static` 局部变量。
>
> ```cpp
> static bool show_transform = true;
> ImGui::Checkbox("Transform", &show_transform);
> //                            ↑ 传入指针，ImGui 直接修改这个变量
> ```
>
> 每帧调用时:
> 1. `show_transform` 保持上一帧的值（`static` 保证跨帧保持）
> 2. `Checkbox` 绘制复选框，如果用户点击 → 翻转 `show_transform`
> 3. 后续代码根据 `show_transform` 决定是否绘制 Transform 面板
>
> **缺陷**: `static` 局部变量是全局隐式状态。如果创建两个 Inspector 窗口，它们会共享同一份 `show_transform` → Bug。解决方案: 用 ImGui 的 ID 系统或将状态提取到结构体中。

---

### TreeNode — 可折叠面板

```cpp
    if (show_transform) {
        auto& pool = reg.pool<Transform>();
        if (ImGui::TreeNode("Transform", "Transform (%zu)", pool.size())) {
```

> **语法知识 — `ImGui::TreeNode` 与 printf 格式化**:
>
> ```cpp
> ImGui::TreeNode("id", "format string %zu", value);
> ```
> - 第一个参数 `"Transform"` 是 **ID**（ImGui 用它区分不同的 TreeNode）
> - 第二个参数是 printf 风格的显示文本
> - `%zu`: `size_t` 类型的格式化占位符（`z` = size_t，`u` = unsigned）
>
> 返回 `true` = 节点已展开，应绘制子内容。返回 `false` = 已折叠，跳过绘制。

---

```cpp
            for (size_t i = 0; i < pool.size(); ++i) {
                Entity e = pool.raw_entity_data()[i];
                auto& t = pool.raw_data()[i];
                ImGui::Text("  [%d] (%.1f, %.1f)", e.index(), t.x, t.y);
            }
            ImGui::TreePop();
```

**绕过 View，直接遍历 SparseSet 的 Dense 数组**:

| 遍历方式 | 用途 | 开销 |
|---------|------|------|
| `reg.view<T>()` | 遍历有组件 T 的实体 | 创建 View + 迭代 |
| **`pool.raw_data()` 直接访问** | 遍历组件 T 的**所有数据** | 零开销，裸数组 |

这里用直接访问是因为我们要**展示所有 Transform 组件**，不需要交叉检查其他组件 → View 没有额外价值。

> **`ImGui::TreePop()`**: 与 `TreeNode` 配对。结束折叠区域。**必须在 `TreeNode` 返回 `true` 时调用**。常见 Bug: 忘记调用 TreePop → ImGui 栈不平衡 → 后续所有 UI 错乱。

---

### Draw — 主入口

```cpp
inline void Draw(Registry& reg) {
    rlImGuiBegin();
    DrawEntityInspector(reg);
    rlImGuiEnd();
}
```

**`rlImGuiBegin/End`**: 开始/结束一帧 ImGui。所有 ImGui 调用必须在这两者之间。`End` 时 rlImGui 将 ImGui 的命令列表翻译为 Raylib 绘制调用并提交。

**绘制顺序**: `main.cpp` 中 DebugUI 在 DrawSprites 之后调用 → ImGui 面板绘制在精灵之上。

---

## 文件级总结

| 设计决策 | 选择 | 理由 |
|---------|------|------|
| GUI 模式 | 即时模式 (ImGui) | 调试用，快速迭代 |
| 状态存储 | static 局部变量 | ImGui 惯用模式 |
| 数据访问 | raw_data() 直接访问 | 调试面板展示全部数据，无需 View 过滤 |
| 桥接层 | rlImGui | Raylib ↔ ImGui 的标准桥接 |
