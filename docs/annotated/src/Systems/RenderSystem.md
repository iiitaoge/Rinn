# RenderSystem.hpp — 渲染系统（深度注释版）

> 文件路径: `src/Systems/RenderSystem.hpp`  
> 角色: 封装 Raylib 的窗口管理和绘制 API 为无状态自由函数。

---

## 文件级设计意图

**System = 无状态函数集合**。`RenderSystem` 没有成员变量——所有状态要么在 Raylib 全局上下文中，要么在 `Registry`/`ResourceManager` 中。System 只是操作数据的"算法"。

**为什么用 `namespace` 而非 `class`？**

| 方案 | 状态管理 | 调用语法 | 适合场景 |
|------|---------|---------|---------|
| **namespace（当前）** | 无状态 | `RenderSystem::Init(...)` | 纯函数集合 |
| 静态类 | 可有静态状态 | `RenderSystem::Init(...)` | 需要共享私有状态 |
| 单例 | 有实例状态 | `RenderSystem::instance().init()` | 有丰富内部状态 |

当前 System 不需要状态 → namespace 最简。这也遵循 DOD：System 是操作数据的纯函数，数据存在 Registry 中。

---

## 逐行注释

### Init — 窗口初始化

```cpp
inline void Init(int width, int height, const char* title) {
    InitWindow(width, height, title);
    SetTargetFPS(144);
}
```

> **语法知识 — `const char*` vs `std::string`**:
>
> Raylib 是 C 语言库，API 只接受 C 字符串 (`const char*`)。这里直接用 C 字符串避免 `string → c_str()` 的转换。
>
> **`const char*`**: 指向不可修改的字符数组。`const` 修饰 `char`（指针指向的数据），避免函数内部意外修改字符串内容。

**`SetTargetFPS(144)`**: Raylib 内部用 `Sleep` 限帧。如果渲染只需 2ms/帧，剩余 ~5ms 让出 CPU 给其他进程。

**设计选择**: 硬编码 144fps。更灵活的做法是作为参数传入或从配置文件读取。

---

### 帧循环控制

```cpp
inline void BeginFrame(Color clear_color = RAYWHITE) {
    BeginDrawing();
    ClearBackground(clear_color);
}
```

> **语法知识 — 默认参数**:
>
> `Color clear_color = RAYWHITE` — 调用时不传则用默认值。
>
> **默认参数的陷阱**:
> - 只能在声明中指定，不能在定义中重复
> - 默认参数从右往左必须连续（不能中间跳过）
> - 在头文件中声明的默认值对所有使用者可见

**`BeginDrawing` + `ClearBackground` 的底层**: Raylib 使用 OpenGL 双缓冲。`BeginDrawing` 切换到后缓冲区，`ClearBackground` 调用 `glClearColor` + `glClear`。

---

### DrawSprites — 精灵绘制

```cpp
inline void DrawSprites(Registry& reg, ResourceManager& res) {
    for (Entity e : reg.view<Transform, Sprite>()) {
        const auto& t = reg.get<Transform>(e);
        const auto& s = reg.get<Sprite>(e);
        Texture2D& tex = res.get_texture(s.texture_id);

        Rectangle src = { 0.0f, 0.0f, (float)tex.width, (float)tex.height };
        Rectangle dst = { t.x, t.y, s.width, s.height };
        DrawTexturePro(tex, src, dst, { 0.0f, 0.0f }, 0.0f, WHITE);
    }
}
```

> **语法知识 — `DrawTexturePro` 参数解析**:
>
> ```cpp
> DrawTexturePro(
>     tex,           // 纹理对象
>     src,           // 源矩形: 从纹理的哪个区域采样 (Spritesheet 裁剪)
>     dst,           // 目标矩形: 画到屏幕的哪个位置+大小
>     { 0, 0 },      // 旋转原点 (相对于 dst 左上角)
>     0.0f,          // 旋转角度 (度)
>     WHITE          // 颜色调制 (WHITE = 不变色)
> );
> ```
>
> **颜色调制**: `WHITE = {255, 255, 255, 255}`。每个像素颜色乘以调制色再除以 255。用 RED 调制 = 只保留红色通道。用 `{255,255,255,128}` = 半透明。

> **语法知识 — `::DrawText` 全局限定**:
>
> ```cpp
> inline void DrawText(const char* text, int x, int y, int size, Color color) {
>     ::DrawText(text, x, y, size, color);
> }
> ```
> `::` 前缀表示全局命名空间。因为当前 namespace `Rinn::RenderSystem` 也有 `DrawText`，不加 `::` 会递归调用自己 → 栈溢出。

**性能分析**:

`DrawSprites` 中每个实体：
1. `reg.get<Transform>(e)` — 2 次数组寻址 (~2ns)
2. `reg.get<Sprite>(e)` — 2 次数组寻址 (~2ns)
3. `res.get_texture(s.texture_id)` — 1 次数组寻址 (~1ns)
4. `DrawTexturePro(...)` — GPU 绘制调用 (~100-1000ns，取决于批处理)

瓶颈在 GPU 调用，而非 ECS 查询。1000 个精灵 → 1000 次 draw call → 约 1ms（无批处理）。

**缺陷**: 没有渲染排序（Z-order/layer）。所有精灵按 SparseSet 中的顺序绘制。正确做法需要按 `Transform.layer` 排序或分批渲染。

---

## 文件级总结

| 设计决策 | 选择 | 理由 |
|---------|------|------|
| System 形式 | namespace 自由函数 | 无状态，与 DOD 一致 |
| Raylib 封装深度 | 薄封装（1:1 转发） | 不增加不必要的抽象层 |
| 帧率限制 | 硬编码 144fps | 简单原型，后续可配置 |
| 绘制排序 | 无 | 缺陷，需要按 layer 排序 |
