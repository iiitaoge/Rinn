# Components.hpp — 组件定义与编译期校验（深度注释版）

> 文件路径: `src/Components/Components.hpp`  
> 角色: 定义所有**游戏数据组件**和**标签组件**。通过 `static_assert` 在编译期强制约束组件的内存特性。

---

## 文件级设计意图

**ECS 的"C"字**: 组件是**纯数据**——没有方法，没有继承，没有虚函数。它们只是结构化的内存块。逻辑由 System 处理。

**为什么纯数据？**
| 方面 | 纯数据组件（当前） | 带方法的组件 |
|------|-------------------|-------------|
| Cache 效率 | ✓ sizeof 最小，紧密排列 | ✗ vtable 指针增加 8B |
| 序列化 | ✓ `memcpy` 即可 | ✗ 需要自定义序列化 |
| 线程安全 | ✓ 数据并行天然安全 | ✗ 方法可能有副作用 |
| 代码组织 | ⚠️ 数据和逻辑分离 | ✓ 直觉上"面向对象" |

**Data-Oriented Design**: 与传统 OOP（数据+行为绑定在对象上）相反，DOD 将数据和行为完全分离。组件是数据的集合，System 是行为的集合，通过 Entity ID 关联。

---

## 依赖关系

```cpp
#pragma once
#include <raylib.h>
#include <type_traits>
```

| 依赖 | 原因 |
|------|------|
| `<raylib.h>` | 预备使用 Raylib 类型如 `Color`。目前未直接引用。 |
| `<type_traits>` | 编译期类型检查工具 |

> **语法知识 — `<type_traits>`**: C++11 标准库，提供编译期类型信息查询模板。是"静态反射"的基础。常用的有：
> - `std::is_aggregate_v<T>` — T 是聚合类型？
> - `std::is_trivially_copyable_v<T>` — T 可以 memcpy？
> - `std::is_empty_v<T>` — T 没有非静态数据成员？
> - `std::is_pod_v<T>` — T 是 POD 类型？(C++20 弃用)

---

## 数据组件

### Transform — 空间位置

```cpp
struct Transform {
    float x, y;
    int layer = 0;
};
```

**内存布局分析**:
```
偏移:  [0]     [4]     [8]
字段:  | x     | y     | layer |
类型:  float   float   int
大小:  4B      4B      4B
总计:  12 bytes, 无填充
```

> **语法知识 — NSDMI (Non-Static Data Member Initializer)**:
>
> `int layer = 0;` 是 C++11 引入的"非静态数据成员默认初始化器"。如果构造时没有显式提供 `layer` 的值，默认用 0。
>
> ```cpp
> Transform t1{1.0f, 2.0f};      // layer = 0（使用默认值）
> Transform t2{1.0f, 2.0f, 5};   // layer = 5（显式覆盖）
> ```
>
> **注意**: NSDMI 不影响聚合类型的判定（C++14 起）。C++11 中有 NSDMI 会使类型不再是聚合的——这是 C++11 到 C++14 的一个行为变化。

**设计选择**: 用 `float` 而非 `double`——4 字节 vs 8 字节。2D 游戏中 `float` 精度足够（~7 位有效数字，覆盖 ±1000 万的范围），且让 Transform 仅 12B，一条 64B Cache Line 能装 5 个。

**缺陷**: 缺少旋转和缩放。完整的 2D Transform 通常需要 `{x, y, rotation, scaleX, scaleY}`（20B）。当前是最小化设计，后续按需添加。

---

### Sprite — 精灵渲染

```cpp
struct Sprite {
    uint16_t texture_id;
    float width, height;
};
```

**内存布局分析**:
```
偏移:  [0]        [2] [4]     [8]
字段:  | tex_id   |pad| width | height |
类型:  uint16_t   2B  float   float
大小:  2B         2B  4B      4B
总计:  12 bytes（含 2B 填充）
```

> **语法知识 — 结构体内存对齐 (Alignment Padding)**:
>
> CPU 要求 `float` 在 4 字节对齐的地址上读取（否则性能下降或触发异常）。`texture_id` 占 2 字节，后面的 `width` 需要在偏移 4 处开始 → 编译器自动插入 2 字节填充。
>
> 可以用 `#pragma pack(1)` 强制无填充，但会导致未对齐访问的性能问题。

**设计选择**: 用 `uint16_t` 索引而非 `Texture2D*` 指针。

| 方案 | 大小 | 序列化 | Cache |
|------|------|--------|-------|
| `Texture2D*` 指针 | 8B | ✗ 指针不可序列化 | 差（间接寻址） |
| **`uint16_t` 索引** | **2B** | **✓ 数字可序列化** | **好（值类型）** |

---

### Velocity — 速度

```cpp
struct Velocity {
    float vx, vy;
};
```

8 bytes，一条 Cache Line 装 8 个。`PhysicSystem` 每帧做 `t.x += v.vx * dt`。

---

### Collider — 碰撞盒

```cpp
struct Collider {
    float width, height;
    float offset_x = 0.0f;
    float offset_y = 0.0f;
    bool is_trigger = false;
    bool is_static = false;
};
```

**内存布局分析**:
```
偏移:  [0]     [4]     [8]       [12]      [16]     [17]    [18-19]
字段:  | w     | h     | off_x   | off_y   |trigger |static | pad  |
大小:  4B      4B      4B        4B        1B       1B      2B
总计:  20 bytes（含 2B 尾部填充，对齐到 4 的倍数）
```

| 字段 | 设计意图 |
|------|---------|
| `width, height` | AABB 碰撞盒尺寸 |
| `offset_x/y` | 碰撞盒相对于 Transform 位置的偏移。允许碰撞盒不与精灵中心对齐（例如只覆盖角色的脚部） |
| `is_trigger` | `true` = 不阻挡运动，只触发事件（例如"进入商店区域"） |
| `is_static` | `true` = 静态物体，碰撞检测可以优化（静态 vs 静态不需要检测） |

**缺陷**: 目前 `is_trigger` 和 `is_static` 没有被 `CollisionSystem` 使用——它们是预留的接口。

---

### Emotion — 情绪

```cpp
struct Emotion {
    float fear, anger, greed;
};
```

12 bytes。用于未来的 AI 决策系统（如 Boids 行为加权）。目前是占位组件。

---

## 标签组件 (Tag Components)

```cpp
struct IsPlayer {};
struct IsEnemy {};
struct IsDead {};
struct IsStatic {};
```

> **语法知识 — 空结构体的 sizeof**:
>
> C++ 标准规定：每个对象必须有唯一的地址 → 空类至少占 1 字节。
> ```cpp
> static_assert(sizeof(IsPlayer) == 1);  // 1 byte
> ```
>
> **但在 SparseSet 中**: `vector<IsPlayer>` 的每个元素占 1 字节。1000 个标签组件 = 1000 字节 Dense 数组。
>
> **优化方案（未实现）**: 特化 `SparseSet<T>` 当 `is_empty_v<T>` 时只存 Sparse 不存 Dense（EnTT 做了这个优化）。这会将标签组件的 Dense 内存降为零。

**设计意图**: 标签组件不存储数据，仅靠"实体是否拥有此组件"来标记身份。配合 View 使用实现类型过滤：
```cpp
// 只遍历玩家实体
for (Entity e : reg.view<Transform, IsPlayer>()) { ... }

// 只遍历敌方实体
for (Entity e : reg.view<Transform, IsEnemy>()) { ... }
```

**为什么不用继承表达 is-a 关系？** OOP 方式是 `class Player : public Entity`。但 ECS 中 Entity 是轻量句柄，没有类层次。标签组件用组合代替继承，更灵活（可以运行时动态添加/移除标签）。

---

## 编译期校验

### 聚合类型检查

```cpp
static_assert(std::is_aggregate_v<Transform>,  "Transform must be aggregate");
static_assert(std::is_aggregate_v<Sprite>,     "Sprite must be aggregate");
static_assert(std::is_aggregate_v<Velocity>,   "Velocity must be aggregate");
static_assert(std::is_aggregate_v<Collider>,   "Collider must be aggregate");
```

> **语法知识 — 聚合类型 (Aggregate Type)**:
>
> 聚合类型的要求（C++20）:
> 1. 无用户声明/继承的构造函数
> 2. 无 `private`/`protected` 非静态数据成员
> 3. 无虚函数
> 4. 无虚/私有/受保护的基类
>
> 聚合类型支持**花括号聚合初始化**：
> ```cpp
> Transform t{1.0f, 2.0f, 0};  // 按成员声明顺序初始化
> ```
>
> **为什么强制聚合？**
> - 保证内存布局与声明顺序一致（利于调试和序列化）
> - 保证没有隐藏的构造逻辑（如成员初始化器之外的副作用）
> - 保证可以安全地 `memcpy` 整块内存

### 可平凡拷贝检查

```cpp
static_assert(std::is_trivially_copyable_v<Transform>, "...");
```

> **语法知识 — `is_trivially_copyable_v`**:
>
> 如果类型满足以下条件则为 `true`:
> - 没有非平凡的拷贝/移动构造函数
> - 没有非平凡的拷贝/移动赋值运算符
> - 没有非平凡的析构函数
>
> "平凡" = 编译器生成的默认版本 = `memcpy` 等价。
>
> **不满足的例子**: `std::string` 内部有指针和堆内存，拷贝构造函数需要分配新内存并复制内容 → 非平凡 → `is_trivially_copyable_v<string>` = false。
>
> **为什么必须可平凡拷贝？** SparseSet 的 swap-and-pop 使用 `std::move`，对平凡可拷贝类型等价于 `memcpy`。如果组件持有堆资源（如 `string`），swap 后数据可能被意外共享 → Bug。

### 标签组件空检查

```cpp
static_assert(std::is_empty_v<IsPlayer>, "IsPlayer must be empty (tag component)");
```

防止有人误给标签组件添加数据成员：
```cpp
struct IsPlayer { int health; };  // ❌ static_assert 失败！tag 不应有数据。
```

---

## 文件级总结

| 设计决策 | 选择 | 理由 |
|---------|------|------|
| 组件 = 纯数据 | 无方法无虚函数 | DOD: sizeof 最小，Cache 友好，可 memcpy |
| 位置用 float | 非 double | 2D 够用，省一半空间 |
| 纹理用 ID 索引 | 非指针 | 2B vs 8B，可序列化 |
| 编译期校验 | static_assert ×12 | 防止破坏组件约束的改动通过编译 |
| 标签用空 struct | 非 bool 标志 | 与 View 组合支持类型级过滤 |
