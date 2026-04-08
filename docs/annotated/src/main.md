# main.cpp — 程序入口（深度注释版）

> 文件路径: `src/main.cpp`  
> 角色: 引擎的启动点和主循环。初始化子系统 → 加载 Lua → 运行游戏循环 → 清理退出。

---

## 文件级设计意图

**游戏循环架构**: 当前使用最简单的"固定顺序单线程循环"。

| 架构 | 特点 | 适合 |
|------|------|------|
| **固定顺序循环（当前）** | Input→Logic→Physics→Collision→Render | 小型项目原型 |
| 固定时间步 | Physics 每帧固定 dt，Render 可变帧率 | 物理精度要求高 |
| Job System | System 并行执行（依赖图调度） | 大型项目，多核利用 |

---

## 逐行注释

### using 声明

```cpp
using namespace::Rinn;
```

> **语法知识 — `using namespace`**:
>
> 将命名空间中所有名称引入当前作用域。之后可以写 `Registry` 代替 `Rinn::Registry`。
>
> **适用场景**: 在 `.cpp` 文件（翻译单元）中安全。在 `.hpp` 头文件中**强烈不推荐**——会污染所有包含该头文件的文件。
>
> **`::` 在 `namespace` 前**: 多余但合法。`using namespace::Rinn` 等价于 `using namespace Rinn`。

---

### 初始化阶段

```cpp
int main() {
    RenderSystem::Init(1600, 1400, "Rinn");
    DebugUI::Init();
    Registry reg;
    ResourceManager res;
```

**初始化顺序至关重要**:
1. **先 Raylib**: ImGui 依赖 Raylib 的 OpenGL 上下文
2. **再 ImGui**: 需要已存在的窗口
3. **然后 ECS**: 不依赖图形系统
4. **最后 Lua**: 绑定时需要 `reg` 和 `res` 已存在

**析构逆序**: C++ 局部变量按声明的**反向顺序**析构:
```
析构: res → reg → (lua → res → reg 都析构后) → DebugUI::Shutdown → RenderSystem::Shutdown
```
但这里 `lua`、`reg`、`res` 的析构顺序需要注意: `lua` 在 `reg` 之后声明，所以 `lua` 先析构。`lua` 析构时释放 Lua VM，其中持有的 lambda 捕获 `&reg` 和 `&res` 此时仍然有效（后析构）→ 安全。

---

### Lua 加载

```cpp
    sol::state lua;
    Init_lua(lua);
    bind(lua, reg, res);
    auto result = lua.script_file("../../../scripts/main.lua");
```

> **语法知识 — `auto result`**:
>
> `auto` 让编译器从右侧表达式推导类型。这里推导为 `sol::protected_function_result`。
>
> **`auto` 的好处**: 避免写冗长的类型名。尤其模板类型如 `sol::protected_function_result` 手写容易出错。
>
> **`auto` 的缺陷**: 需要查看函数签名才知道实际类型。对初学者不友好。

**路径 `../../../scripts/main.lua`**:

```
可执行文件位置: build/out/Debug/Project_Rinn.exe
脚本位置: scripts/main.lua

从 exe 到脚本: ../../.. (上三级 = 回到项目根) + /scripts/main.lua
```

**设计缺陷**: 硬编码相对路径。如果构建目录结构变化 → 找不到脚本。改进方案: 用 `CMAKE_SOURCE_DIR` 在编译时注入项目根路径。

---

### 主循环

```cpp
    while (!RenderSystem::ShouldClose()) {
        RenderSystem::BeginFrame();
```

**每帧执行顺序**:

```
1. BeginFrame      ← 清屏
2. DrawText(FPS)   ← HUD
3. lua["on_update"]()  ← Lua 输入处理 → 设置 Velocity
4. PhysicSystem::update  ← 位置 += 速度 × dt
5. CollisionSystem::detect + resolve  ← 碰撞检测与推开
6. DrawSprites     ← 绘制所有精灵
7. DebugUI::Draw   ← ImGui 调试面板
8. EndFrame        ← 缓冲区交换，显示
```

---

```cpp
        RenderSystem::DrawText(
            std::format("FPS: {}", RenderSystem::FPS()).c_str(),
            10, 10, 20, GREEN);
```

> **语法知识 — `std::format` (C++20)**:
>
> 类型安全的字符串格式化（类似 Python 的 f-string）:
> ```cpp
> std::format("Hello, {}!", name)         // 自动推断类型
> std::format("PI = {:.2f}", 3.14159)     // 保留 2 位小数: "PI = 3.14"
> std::format("{:05d}", 42)               // 零填充: "00042"
> ```
>
> **比 `printf` 安全**: `printf` 中类型和占位符不匹配时直接 UB（未定义行为），`std::format` 编译期检查类型。
>
> **比 `ostringstream` 快**: 无动态内存分配（大多实现），直接生成字符串。

---

```cpp
        lua["on_update"]();
```

> **语法知识 — `sol::state` 的 `operator[]`**:
>
> `lua["on_update"]` 返回一个 `sol::proxy` 对象。对它调用 `()` 会执行 Lua 全局函数 `on_update`。
>
> 如果 `on_update` 不存在 → Sol2 抛异常或返回 nil（取决于配置）。当前代码没有错误处理——如果 Lua 脚本缺少 `on_update` 函数 → 运行时崩溃。

---

```cpp
        auto hits = CollisionSystem::detect(reg);
        for (auto& h : hits) {
            std::cout << std::format("Collision: {} <-> {}\n", h.a.index(), h.b.index());
        }
        CollisionSystem::resolve(reg, hits);
```

**碰撞日志**: 每帧打印所有碰撞对到控制台。

**设计缺陷**: `std::cout` 在每帧碰撞时输出会严重影响性能（控制台 IO 极慢，~1ms/行）。应该只在调试模式下启用，或改用 ImGui 日志窗口。

---

### 清理阶段

```cpp
    res.unload_all();
    RenderSystem::Shutdown();
    return 0;
}
```

**`res.unload_all()` 在 `Shutdown()` 之前**: `UnloadTexture` 需要有效的 OpenGL 上下文（Raylib 窗口仍在）。如果先关窗口再卸纹理 → OpenGL 调用失败 → 可能崩溃。

---

## 文件级总结

| 设计决策 | 选择 | 理由 |
|---------|------|------|
| 循环架构 | 单线程固定顺序 | 简单原型 |
| 脚本路径 | 硬编码相对路径 | 缺陷: 应编译时注入 |
| FPS 显示 | std::format | C++20 类型安全格式化 |
| 初始化顺序 | Raylib→ImGui→ECS→Lua | 依赖关系决定 |
| 碰撞日志 | std::cout | 缺陷: 性能差，应用 ImGui 替代 |
