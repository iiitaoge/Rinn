# SparseSet.hpp — 稀疏集数据结构（深度注释版）

> 文件路径: `src/Core/SparseSet.hpp`  
> 角色: ECS 的**核心数据结构**。每种组件类型拥有一个 SparseSet，实现 O(1) 增删查 + 内存连续的线性遍历。

---

## 文件级设计意图

**问题**: ECS 需要一种数据结构，能按实体 ID 快速查找组件（O(1)），同时也能线性遍历所有组件（Cache 友好）。

**可选方案对比**:

| 方案 | 按ID查找 | 遍历 | 删除 | 内存 | Cache |
|------|----------|------|------|------|-------|
| `std::unordered_map<Entity, T>` | O(1) 但有碰撞 | O(n) 但散布 | O(1) | 每条目 ~40B 开销 | ✗ 哈希桶散布 |
| `std::map<Entity, T>` | O(log n) | O(n) 有序 | O(log n) | 红黑树节点 | ✗ 指针追踪 |
| 裸数组 `T[MAX_ENTITIES]` | O(1) | O(MAX) 含空洞 | O(1) | MAX × sizeof(T) | ⚠️ 空洞浪费预取 |
| **Sparse Set** | **O(1)** | **O(n) 紧密** | **O(1)** | **Sparse + Dense** | **✓ Dense 连续** |

**Sparse Set 胜出的原因**: 它是唯一同时满足"O(1) 随机访问"和"紧密线性遍历"的结构。代价是需要额外的 Sparse 数组（32KB），但这刚好适配 L1 Cache。

**核心 trade-off**: 空间换时间。Sparse 数组不管有多少实体都是固定 32KB——100 个实体也是 32KB，10000 个也是。但 32KB 在现代 CPU Cache 中不算什么。

---

## 数据结构原理（图解）

```
                Sparse 数组 (固定 16384 槽)
                按 entity.index() 直接寻址
                ┌───┬───┬───┬───┬───┬───────────┐
entity.index(): │ 0 │ 1 │ 2 │ 3 │ 4 │ ... 16383 │
                ├───┼───┼───┼───┼───┼───────────┤
         值:    │ 2 │ ∅ │ 0 │ 1 │ ∅ │    ∅      │
                └─┬─┴───┴─┬─┴─┬─┴───┴───────────┘
                  │       │   │
                  │  ┌────┘   └─────┐
                  ▼  ▼              ▼
                Dense 数组 (紧密, 无空洞, 按添加顺序)
                ┌──────────┬──────────┬──────────┐
         索引:  │    0     │    1     │    2     │
                ├──────────┼──────────┼──────────┤
         组件:  │ 组件(e2) │ 组件(e3) │ 组件(e0) │
                └──────────┴──────────┴──────────┘

                dense_to_entity 数组 (与 Dense 完全同步)
                ┌──────────┬──────────┬──────────┐
                │ Entity(2)│ Entity(3)│ Entity(0)│
                └──────────┴──────────┴──────────┘
```

**查找路径**: `entity.index()=2` → `Sparse[2]=0` → `Dense[0]` = 组件(e2)。两次数组下标，共 ~2 个 L1 Cache 命中 ≈ 2ns。

**遍历路径**: 从 `Dense[0]` 到 `Dense[n-1]`，CPU 硬件预取器完美工作。

---

## 依赖与条件编译

```cpp
#pragma once
#include"Types.hpp"
#ifdef _MSC_VER
#include <intrin.h>     // MSVC: _mm_prefetch
#else
#include <immintrin.h>
#endif
```

> **语法知识 — 条件编译**:
>
> `#ifdef` / `#else` / `#endif` 是预处理器指令，在**编译前**决定哪些代码被包含。
>
> - `_MSC_VER`: MSVC 编译器预定义的宏（值=编译器版本号，如 1929）
> - `__GNUC__`: GCC 定义的宏
> - `__clang__`: Clang 定义的宏
>
> **为什么需要条件编译？** `_mm_prefetch` 是 Intel 定义的 intrinsic 函数。MSVC 把它放在 `<intrin.h>`，GCC/Clang 放在 `<immintrin.h>`。不同编译器 include 不同头文件。

---

## ISparseSet — 抽象基类（接口）

### 设计意图

```cpp
class ISparseSet {
public:
    virtual ~ISparseSet() = default;
```

**为什么需要基类？**

`Registry` 需要用一个数组存放所有组件池：
```cpp
std::array<std::unique_ptr<ISparseSet>, 64> Components_Pool;
```
但每个池的具体类型不同（`SparseSet<Transform>`, `SparseSet<Velocity>` ...）。C++ 没有"存放不同类型的数组"，唯一的办法是**多态**：通过基类指针持有不同派生类。

**替代方案**:
| 方案 | 优势 | 缺陷 |
|------|------|------|
| **虚函数多态（当前）** | 类型安全，编译器帮助管理 | vtable 间接调用 ~2-5ns/call |
| `void*` 类型擦除 | 无 vtable 开销 | 类型不安全，需要手动转换 |
| `std::any` | 类型安全 | 堆分配 + RTTI 开销 |
| `std::variant` | 栈上存储 | 编译期必须知道所有类型，组件数变化要改 variant |

选择虚函数多态因为：类型安全 + 代码清晰。而 View 通过在构造时缓存裸指针，将虚函数调用限制在"构造一次"而非"每帧每实体"。

> **语法知识 — `virtual` 析构函数**:
>
> 如果通过基类指针 `delete` 派生类对象，没有 `virtual` 析构函数会导致**未定义行为**（通常表现为派生类析构函数不被调用 → 内存泄漏）：
> ```cpp
> ISparseSet* pool = new SparseSet<Transform>();
> delete pool;
> // 没有 virtual ~ISparseSet() → SparseSet<Transform> 的析构函数不会执行！
> // ❌ vector<T> Dense 不会释放 → 内存泄漏
> ```
>
> **`= default`**: 让编译器生成默认实现（什么也不做，因为基类没有需要释放的资源）。

---

### 构造函数

```cpp
ISparseSet() {
    Sparse.fill(NULL_COMPONENT_ENTITY);
}
```

> **语法知识 — `std::array::fill`**:
>
> 将数组所有元素设为同一值。内部实现通常是 `memset` 或展开的循环。
> 
> 对 32KB 的 Sparse 数组来说，`fill(0xFFFF)` 在首次构造时约花费 8μs（现代 CPU）。但只执行一次。

**设计选择**: 用 `0xFFFF` 而非 `0` 作为"空"标记。因为 `0` 是合法的 Dense 索引（第一个组件的位置），必须用一个超出合法范围的值。

---

### 纯虚函数

```cpp
virtual void remove(Entity entity) = 0;
virtual void clear() = 0;
virtual size_t size() const noexcept = 0;
virtual const Entity* entity_data() const noexcept = 0;
```

> **语法知识 — 纯虚函数 `= 0`**:
>
> 纯虚函数没有默认实现，派生类**必须**提供实现，否则也会成为抽象类。
>
> 含有至少一个纯虚函数的类叫**抽象类**，不能被直接实例化：
> ```cpp
> ISparseSet pool;  // ❌ 编译错误：不能实例化抽象类
> ```
>
> `entity_data()` 返回 `const Entity*` 裸指针。**为什么不返回 `span` 或 `vector&`？**
> - `span`/`vector` 返回类型意味着基类必须知道 Dense 的类型——但基类是类型无关的。
> - 裸指针是最通用的接口，View 只需要指针 + 大小即可遍历。

---

### Sparse 数组

```cpp
protected:
    std::array<Entity_index, MAX_ENTITIES> Sparse;
```

**内存分析**:
```
std::array<uint16_t, 16384> Sparse:
  大小 = 16384 × 2 bytes = 32,768 bytes = 32 KB
  
  L1 Data Cache (典型 Intel CPU): 32-48 KB
  → Sparse 数组恰好装入 L1
  → 每次 Sparse[idx] 访问 ≈ 4 cycles = ~1.3ns (@3GHz)
```

> **语法知识 — `protected` 访问控制**:
>
> C++ 三种访问级别:
> | 级别 | 类内 | 派生类 | 外部 |
> |------|------|--------|------|
> | `public` | ✓ | ✓ | ✓ |
> | `protected` | ✓ | ✓ | ✗ |
> | `private` | ✓ | ✗ | ✗ |
>
> `Sparse` 放在 `protected` 是因为 `SparseSet<T>` 需要直接访问它（频繁操作），但外部代码不应碰它。

**为什么 Sparse 放在基类而非派生类？** 因为所有 `SparseSet<T>` 的 Sparse 数组完全一样（`array<uint16_t, 16384>`），与组件类型 T 无关。放在基类避免重复定义。

---

## SparseSet\<T\> — 具体组件池

```cpp
template<typename T>
class SparseSet : public ISparseSet {
private:
    std::vector<T> Dense;
    std::vector<Entity> dense_to_entity;
```

> **语法知识 — 类模板与继承**:
>
> `class SparseSet : public ISparseSet` — 每个实例化（如 `SparseSet<Transform>`）都继承 `ISparseSet`，可以通过 `ISparseSet*` 指针持有。
>
> **`public` 继承**: 基类的 `public` 成员在派生类中仍是 `public`。如果用 `private` 继承，基类所有成员都变成 `private`（破坏多态的 is-a 关系）。

**为什么 Dense 用 `vector` 而非 `array`？**
- `array` 需要编译期知道大小 → 必须预设 MAX，浪费未用空间
- `vector` 按需增长 → 只分配实际需要的内存
- `vector` 的底层也是连续内存，与 `array` 一样 Cache 友好

**`dense_to_entity` 存在的必要性**:

删除操作 (swap-and-pop) 需要知道"被移动到空位的末尾元素对应哪个实体"，才能更新那个实体在 Sparse 中的指针。如果没有这个反向映射，删除就需要遍历整个 Sparse 数组找到末尾实体的索引 → O(n)。有了它 → O(1)。

---

### 类型别名与继承构造

```cpp
using iterator = typename std::vector<T>::iterator;
using const_iterator = typename std::vector<T>::const_iterator;
using value_type = T;

using ISparseSet::ISparseSet;
```

> **语法知识 — `typename` 消歧义**:
>
> 在依赖模板参数的上下文中，编译器无法确定 `std::vector<T>::iterator` 是一个**类型**还是一个**静态成员变量**。`typename` 明确告诉编译器"这是一个类型"。
>
> **必须加 `typename` 的场景**: 嵌套名称的左边是依赖模板参数的类型。
> ```cpp
> typename std::vector<T>::iterator   // T 是模板参数 → 必须加 typename
> std::vector<int>::iterator         // int 不是模板参数 → 不需要
> ```

> **语法知识 — `using ISparseSet::ISparseSet;`**:
>
> C++11 继承构造函数。将基类的所有构造函数"搬到"派生类中。等价于写:
> ```cpp
> SparseSet() : ISparseSet() {}
> ```
> 但更简洁，尤其当基类有多个构造函数时。

---

### emplace — 原地构造组件（深入分析）

```cpp
template<typename... Args>
requires std::constructible_from<T, Args...>
[[nodiscard]] T& emplace(Entity entity, Args&&... args) {
```

> **语法知识 — 可变参数模板 (Variadic Template)**:
>
> `typename... Args` 声明一个**参数包**（parameter pack），代表零个或多个类型：
> ```cpp
> emplace(entity)                         // Args = {}（空包）
> emplace(entity, 1.0f, 2.0f)            // Args = {float, float}
> emplace(entity, Transform{1, 2, 0})    // Args = {Transform}
> ```
>
> `Args&&... args` 展开为对应的参数列表。`&&` 在模板中是**万能引用** (Universal Reference)（也叫转发引用 Forwarding Reference）——它既能绑定左值也能绑定右值。
>
> **万能引用 vs 右值引用**:
> ```cpp
> void foo(int&& x);            // 右值引用: 只接受右值
> template<typename T>
> void bar(T&& x);              // 万能引用: 接受左值和右值
> ```
> 区别在于是否涉及模板类型推导。有推导 → 万能引用。无推导 → 右值引用。

> **语法知识 — `requires` 约束 (C++20 Concepts)**:
>
> ```cpp
> requires std::constructible_from<T, Args...>
> ```
> 约束这个模板只在 `T` 可以用 `Args...` 构造时才合法。
>
> **没有约束时的错误信息** (C++17 以前):
> ```
> error: no matching function for call to 'construct_at'
>   in instantiation of function template specialization 'allocator_traits<...>::construct<...>'
>     in instantiation of member function 'vector<Transform>::emplace_back<int, string>'
>       ... (20行嵌套模板错误)
> ```
>
> **有约束时的错误信息** (C++20):
> ```
> error: constraints not satisfied
>   note: 'Transform' is not constructible from '{int, string}'
> ```
> 一目了然。

---

#### 幂等 emplace 设计

```cpp
assert(entity.index() < MAX_ENTITIES && "Entity out of range!");

if (Sparse[entity.index()] != NULL_COMPONENT_ENTITY) {
    return Dense[Sparse[entity.index()]];
}
```

**设计意图**: 如果组件已存在，返回现有引用而非覆盖。

| 策略 | 行为 | 优势 | 缺陷 |
|------|------|------|------|
| **幂等（当前）** | 重复 emplace 返回已有 | 安全，不会意外丢失数据 | 调用者可能以为覆盖了实际没有 |
| 覆盖 | 重复 emplace 替换数据 | 语义直觉 | 可能丢失正在使用的引用 |
| 断言失败 | 重复 emplace 崩溃 | 强制调用者先检查 | 不灵活 |

当前选择"幂等"是因为 Lua 脚本可能多次设置同一组件，崩溃不友好，覆盖可能破坏 C++ 侧持有的引用。

---

#### 异常安全扩容

```cpp
if (Dense.size() == Dense.capacity()) {
    size_t new_cap = std::max<size_t>(Dense.capacity() * 2, 8);
    Dense.reserve(new_cap);
    dense_to_entity.reserve(new_cap);
}
```

> **语法知识 — `vector` 的 size vs capacity**:
>
> ```
> vector 内存布局:
> [已使用|已使用|已使用|_______|_______|_______|]
>  ←────── size ──────→
>  ←──────────── capacity ──────────────────────→
> ```
> - `size()`: 已存储的元素数量
> - `capacity()`: 已分配的内存能容纳的最大元素数量
> - `reserve(n)`: 预分配内存使 `capacity >= n`，不改变 `size`
>
> 当 `size() == capacity()` 时，下一次 `push_back/emplace_back` 会触发扩容：
> 1. 分配新内存（通常 2 倍大小）
> 2. 移动所有元素到新内存
> 3. 释放旧内存
>
> 这是 O(n) 操作，且可能使所有指向旧内存的指针/引用失效！

**为什么手动管理扩容而非依赖 vector 自动扩容？**

`Dense` 和 `dense_to_entity` 必须保持**同步**。如果先 `Dense.push_back()` 成功但 `dense_to_entity.push_back()` 抛异常（内存不足），两个 vector 长度不一致 → 数据损坏。

先 `reserve` 确保两个 vector 都有足够空间后，后续的 `emplace_back` / `push_back` 保证不分配内存 → 不会抛异常 → 两者始终同步。

**`std::max<size_t>(capacity * 2, 8)`**: 初始容量为 8（避免 0×2=0 的死循环），之后倍增。倍增策略保证 n 次插入的总扩容成本是 O(n)（均摊 O(1)）。

---

#### 原地构造

```cpp
Dense.emplace_back(std::forward<Args>(args)...);
```

> **语法知识 — `emplace_back` vs `push_back`**:
>
> ```cpp
> // push_back: 先构造临时对象，再拷贝/移动到 vector 尾部
> Transform t{1.0f, 2.0f};
> Dense.push_back(t);           // 拷贝构造
> Dense.push_back(Transform{1.0f, 2.0f});  // 移动构造
>
> // emplace_back: 直接在 vector 尾部的内存上构造，零拷贝零移动
> Dense.emplace_back(1.0f, 2.0f);  // 原地构造
> ```
>
> 对 POD 类型（如 Transform）差别不大。对持有堆资源的类型（如 `std::string`），`emplace_back` 可以省去一次移动。

> **语法知识 — `std::forward` 完美转发**:
>
> `forward` 保持参数的"值类别"（左值/右值）原样传递：
> ```cpp
> void wrapper(auto&& arg) {
>     target(std::forward<decltype(arg)>(arg));
>     //    ↑ 左值传入 → 转发为左值
>     //    ↑ 右值传入 → 转发为右值
> }
> ```
>
> **如果不用 forward 会怎样？** 所有参数都变成左值（具名变量都是左值），丧失移动语义：
> ```cpp
> Dense.emplace_back(args...);  // ❌ args 是具名变量 → 左值 → 拷贝
> Dense.emplace_back(std::forward<Args>(args)...);  // ✓ 保持原属性
> ```

---

#### 同步映射

```cpp
dense_to_entity.push_back(entity);
Sparse[entity.index()] = static_cast<Entity_index>(Dense.size() - 1);
return Dense.back();
```

三步建立双向映射:
```
步骤1: dense_to_entity[末尾] = entity     // Dense→Entity 方向
步骤2: Sparse[entity.index()] = 末尾索引  // Entity→Dense 方向
步骤3: return Dense.back()                 // 返回新组件引用
```

---

### get — 两种版本

```cpp
[[nodiscard]] T& get(Entity entity) {
    assert(has(entity) && "Entity does not have this component!");
    return Dense[Sparse[entity.index()]];
}

[[nodiscard]] const T& get(Entity entity) const {
    assert(has(entity) && "Entity does not have this component!");
    return Dense[Sparse[entity.index()]];
}
```

> **语法知识 — const 重载**:
>
> 两个函数签名完全相同，只差 `const` 限定符。编译器根据调用对象的 const 性自动选择：
> ```cpp
> SparseSet<T> pool;
> pool.get(e);          // 调用非 const 版本, 返回 T&
>
> const SparseSet<T>& cpool = pool;
> cpool.get(e);         // 调用 const 版本, 返回 const T&
> ```
>
> **为什么需要两个？** 如果只有 `const` 版本，System 就不能修改组件数据。如果只有非 `const` 版本，`const` 引用的 View 就无法读取组件。

**性能分析**: `Dense[Sparse[entity.index()]]` = 两次数组下标寻址:
1. `Sparse[idx]`: L1 命中 → ~1ns
2. `Dense[sparse_val]`: 如果 Dense 在 L1/L2 → ~1-4ns

总共 ~2-5ns/次访问，极其高效。

---

### remove — Swap-and-Pop 删除（深入图解）

```cpp
void remove(Entity entity) override {
    if (entity.index() >= MAX_ENTITIES || Sparse[entity.index()] == NULL_COMPONENT_ENTITY) {
        return;
    }
    Entity_index index_deleted = Sparse[entity.index()];
    Entity_index index_last = static_cast<Entity_index>(Dense.size() - 1);
```

**防御性编程**: 不用 assert，而是安静返回。设计选择——`destroy_entity` 可能对空的池调用 remove，崩溃不合适。

---

```cpp
    // 快速路径：删最后一个
    if (index_deleted == index_last) {
        Dense.pop_back();
        dense_to_entity.pop_back();
        Sparse[entity.index()] = NULL_COMPONENT_ENTITY;
        return;
    }
```

**优化**: 删除末尾不需要 swap，直接 pop。分支预测器通常预测"不走这里"（删最后一个的概率低），不影响性能。

---

```cpp
    Entity entity_last = dense_to_entity[index_last];

    Dense[index_deleted] = std::move(Dense[index_last]);
    Dense.pop_back();

    Sparse[entity_last.index()] = index_deleted;
    Sparse[entity.index()] = NULL_COMPONENT_ENTITY;

    dense_to_entity[index_deleted] = entity_last;
    dense_to_entity.pop_back();
```

**Swap-and-Pop 图解**:

```
删除前 (要删 Entity(2), 它在 Dense[0]):
  Sparse:  [0]=2  [2]=0  [3]=1
  Dense:   [组件C, 组件A, 组件B]  ← index_deleted=0, index_last=2
  d2e:     [e2,    e3,    e0   ]

步骤1: Dense[0] = move(Dense[2])     → Dense: [组件B, 组件A, 组件B(废弃)]
步骤2: Dense.pop_back()              → Dense: [组件B, 组件A]
步骤3: Sparse[e0.index()=0] = 0      → e0 现在指向 Dense[0]
步骤4: Sparse[e2.index()=2] = ∅      → e2 清空
步骤5: d2e[0] = e0                   → d2e: [e0, e3]
步骤6: d2e.pop_back()                → d2e: [e0, e3]

删除后:
  Sparse:  [0]=0  [2]=∅  [3]=1
  Dense:   [组件B, 组件A]
  d2e:     [e0,    e3   ]
  ✓ 依然紧密、一致
```

> **语法知识 — `std::move`**:
>
> `std::move(x)` 并不"移动"x。它只是把 x 强制转换为右值引用 `T&&`，告诉接收方"你可以窃取 x 的资源"。
>
> 对 POD 类型（如 `Transform`），移动等于拷贝，无额外收益。对拥有堆资源的类型（如 `std::string`），移动只需转移指针，O(1) 替代 O(n) 的拷贝。
>
> 移动后的对象处于"有效但未指定"的状态——之后只能析构或重新赋值，不应读取。

**优势**: O(1) 删除 + Dense 保持紧密。
**缺陷**: 删除会改变 Dense 中元素的顺序（末尾元素被移到空位）。如果有人依赖遍历顺序，这是个隐患。但 ECS 系统通常不依赖遍历顺序。

---

### clear — 优化的批量清空

```cpp
void clear() override {
    for (Entity e : dense_to_entity) {
        Sparse[e.index()] = NULL_COMPONENT_ENTITY;
    }
    Dense.clear();
    dense_to_entity.clear();
}
```

**设计选择**: 只清空"有组件的实体"在 Sparse 中的条目，而非 `Sparse.fill()`。

| 方案 | 复杂度 | 说明 |
|------|--------|------|
| `Sparse.fill(∅)` | O(MAX_ENTITIES) = O(16384) | 总是扫描全部 32KB |
| **遍历 dense_to_entity** | **O(实际组件数)** | 200 个组件只扫 200 次 |

当组件数远小于 MAX_ENTITIES 时（常见场景），这个优化带来数量级的提升。

---

### 预取函数

```cpp
void prefetch(Entity entity) const noexcept {
    auto idx = entity.index();
    _mm_prefetch(reinterpret_cast<const char*>(&Sparse[idx]), _MM_HINT_T0);
}

void prefetch_dense(Entity entity) const noexcept {
    auto idx = entity.index();
    auto dense_idx = Sparse[idx];
    if (dense_idx != NULL_COMPONENT_ENTITY) {
        _mm_prefetch(reinterpret_cast<const char*>(&Dense[dense_idx]), _MM_HINT_T0);
    }
}
```

> **语法知识 — `_mm_prefetch` 与 `reinterpret_cast`**:
>
> **`_mm_prefetch(addr, hint)`**: 编译器 intrinsic，对应 x86 `PREFETCHT0` 指令。告诉 CPU "提前把 `addr` 所在的 Cache Line 加载到 L1"。
>
> 预取提示级别:
> | 提示 | 目标缓存 | 用途 |
> |------|----------|------|
> | `_MM_HINT_T0` | L1 + L2 + L3 | 即将使用的数据 |
> | `_MM_HINT_T1` | L2 + L3 | 稍后使用 |
> | `_MM_HINT_T2` | L3 | 更晚使用 |
> | `_MM_HINT_NTA` | 非临时，不污染缓存 | 只用一次的数据 |
>
> **`reinterpret_cast<const char*>`**: `_mm_prefetch` 的参数类型是 `const char*`，但我们要预取的是 `Entity_index*` 或 `T*`。`reinterpret_cast` 在不改变底层位模式的情况下重新解释指针类型。这是合法且安全的（只要不通过转换后的指针做类型不兼容的读写）。

**两阶段预取的原理**:

SparseSet 的一次 `get` 有两步间接寻址：Sparse → Dense。如果在循环中**提前几步**预取，可以隐藏内存延迟：
```
for i = 0 to n:
    prefetch(entities[i + 4])          // 第一阶段：提前 4 步预取 Sparse
    prefetch_dense(entities[i + 2])    // 第二阶段：提前 2 步预取 Dense
    process(entities[i])               // 处理当前实体（此时数据已在 L1）
```

**优势**: 可以将 Cache Miss 延迟（~50ns）完全隐藏在计算中。  
**缺陷**: 过度预取会污染缓存（占用 Cache Line 但实际没用到），降低其他数据的命中率。参数 `PREFETCH_DIST` 需要针对具体硬件调优。目前代码**没有使用**这些预取函数——它们是预留的优化接口。

---

### 迭代器

```cpp
iterator begin() noexcept { return Dense.begin(); }
iterator end() noexcept { return Dense.end(); }
const_iterator begin() const noexcept { return Dense.begin(); }
const_iterator end() const noexcept { return Dense.end(); }
const_iterator cbegin() const noexcept { return Dense.cbegin(); }
const_iterator cend() const noexcept { return Dense.cend(); }
```

> **语法知识 — Range-for 循环的底层原理**:
>
> ```cpp
> for (auto& x : container) { ... }
> ```
> 编译器将其展开为:
> ```cpp
> auto __begin = container.begin();
> auto __end   = container.end();
> for (; __begin != __end; ++__begin) {
>     auto& x = *__begin;
>     ...
> }
> ```
> 只要类型提供 `begin()` 和 `end()`，就可以用 range-for 循环。

**注意**: 这些迭代器遍历的是 `Dense` 数组中的**组件数据**（`T` 类型），不是实体。如果需要知道"这个组件属于哪个实体"，需要用 `raw_entity_data()` 获取并行的实体数组。

---

## 文件级总结

| 设计决策 | 选择 | 替代方案 | 选择理由 |
|---------|------|---------|---------|
| 数据结构 | Sparse+Dense 双数组 | unordered_map, 裸数组 | 唯一兼顾 O(1) 随机和紧密遍历 |
| 多态方式 | 虚函数(ISparseSet) | void*, variant, any | 类型安全 + 清晰 |
| 删除算法 | swap-and-pop | 标记删除, 链表 | O(1) + 保持紧密 |
| emplace 语义 | 幂等（不覆盖） | 覆盖, 断言失败 | Lua 脚本友好 |
| Sparse 初始化 | fill(0xFFFF) | fill(0) | 0 是合法索引，不能做哨兵 |
| 预取 | 提供但未使用 | 不提供 | 预留优化空间 |
