# Registry.hpp — 实体池 + 注册表 + View 迭代器（深度注释版）

> 文件路径: `src/Core/Registry.hpp`  
> 角色: ECS 引擎的**中枢**。包含三个核心类：`EntityPool`（环形缓冲区实体分配器）、`Registry`（组件管理的统一接口）、`View`（多组件联合查询迭代器）。

---

## 文件级设计意图

本文件将实体管理、组件存储、组件查询三大职责编排在一起。这是一个"厚 Registry"设计——单一入口管理一切。

| 设计风格 | 代表 | 特点 |
|---------|------|------|
| **厚 Registry（当前）** | EnTT, 本项目 | 一站式 API，简单直观 |
| 薄 Registry + World | flecs, Bevy | Registry 只管实体，System/Query 独立 |

**选择厚 Registry 的理由**: 代码量小，学习曲线低。对当前规模（~10 种组件、~200 实体）完全够用。

---

## 第一部分：EntityPool — 环形缓冲区实体分配器

### 设计意图

**问题**: 实体被销毁后，它的索引（数组位置）如何复用？

**可选方案**:

| 方案 | 分配 | 回收 | 内存 | FIFO 保证 |
|------|------|------|------|----------|
| 自由链表 (free list) | O(1) | O(1) | 在槽位内存中存 next 指针 | ✗ LIFO |
| 位图扫描 | O(n) 最坏 | O(1) | 2KB 位图 | ✗ |
| **环形缓冲区（当前）** | **O(1)** | **O(1)** | **32KB ring buffer** | **✓ FIFO** |
| 优先队列 | O(log n) | O(log n) | 堆 | 可选 |

**选择环形缓冲区的理由**: O(1) 分配/回收 + FIFO 保证。FIFO 意味着最早回收的索引最先被复用——给复用留出最长的"冷却时间"，降低僵尸句柄碰巧有效的概率。

**缺陷**: 额外 32KB（`ring_buffer` 数组）。自由链表可以零额外内存（在已死实体的数据槽中存链表指针），但破坏了数据布局。

---

### 核心数据

```cpp
class EntityPool {
private:
    static constexpr uint16_t CAPACITY = MAX_ENTITIES;     // 16384
    static constexpr uint16_t MASK = CAPACITY - 1;         // 16383 = 0x3FFF
```

> **语法知识 — 2 的幂与位掩码取模**:
>
> 当 `N = 2^k` 时，`x % N` 等价于 `x & (N-1)`。
>
> 证明: `2^k - 1` 的二进制是 k 个 1。`x & (2^k - 1)` 保留 x 的低 k 位，丢弃高位——这正是 `x % 2^k` 的结果。
>
> ```
> 16384 = 0b100000000000000  (第 14 位为 1)
> 16383 = 0b011111111111111  (低 14 位全 1)
> 
> x & 16383:
>   保留 x 的低 14 位 = x % 16384
> ```
>
> **性能差异**: `%` 运算编译为 `DIV` 指令（~20-30 cycles on x86），`&` 编译为 `AND` 指令（1 cycle）。在热循环中差 20-30 倍。

---

```cpp
static_assert((CAPACITY & (CAPACITY - 1)) == 0,
    "CAPACITY must be power of 2 for MASK to work!");
```

> **语法知识 — `static_assert`**:
>
> 编译期断言（C++11）。与运行时 `assert` 的关键区别：
> - `static_assert`: 编译时检查。不满足 → **编译失败**，永远不会产生错误的可执行文件。
> - `assert`: 运行时检查。不满足 → 程序崩溃。Release 模式下被 `NDEBUG` 移除。
>
> **2 的幂判断**: `n & (n-1) == 0` 的原理:
> ```
> n   = 16384 = 0b100000000000000
> n-1 = 16383 = 0b011111111111111
> &   =         0b000000000000000 = 0  ✓ 是 2 的幂
>
> n   = 12345 = 0b11000000111001
> n-1 = 12344 = 0b11000000111000
> &   =         0b11000000111000 ≠ 0  ✗ 不是 2 的幂
> ```

---

```cpp
std::array<Entity_generation, CAPACITY> generations{};   // 版本号数组
std::array<Entity_index, CAPACITY> ring_buffer{};        // 回收索引的环形队列

uint16_t head = 0;           // 出队端（复用时取）
uint16_t tail = 0;           // 入队端（回收时放）
Entity_index next_idx = 0;   // 水位线: 下一个未使用过的索引
uint16_t alive_entity_count = 0;
```

**环形缓冲区状态图**:
```
初始状态 (空队列):
  ring_buffer: [_, _, _, _, _, ...]
  head=0, tail=0  ← head==tail 表示队列为空

销毁 Entity(idx=5) 后:
  ring_buffer: [5, _, _, _, _, ...]
  head=0, tail=1

销毁 Entity(idx=2) 后:
  ring_buffer: [5, 2, _, _, _, ...]
  head=0, tail=2

复用一个 (取出 idx=5):
  ring_buffer: [_, 2, _, _, _, ...]
  head=1, tail=2

环绕场景 (tail 回到数组开头):
  ring_buffer: [7, 2, 3, 4, 5, 6]
  head=1, tail=0  ← tail < head，队列跨越数组边界
```

**`{}`初始化**: `std::array<T, N>{}` 值初始化所有元素为 0（对整数类型）。`generations` 全 0 表示所有索引待开荒时使用。

---

### acquire — 获取实体

```cpp
[[nodiscard]] Entity acquire() noexcept {
    Entity_index idx;

    if (head != tail) {
        idx = ring_buffer[head];
        head = (head + 1) & MASK;
    }
    else {
        assert(next_idx < CAPACITY && "Entity pool exhausted!");
        idx = next_idx++;
    }

    ++alive_entity_count;
    return Entity(idx, generations[idx]);
}
```

**分支分析**:
- **游戏初期**: `head == tail`（空队列），走 `else` 分支——分配全新索引。`next_idx` 从 0 开始递增。
- **游戏中后期**: 有实体被销毁后，`head != tail`，走 `if` 分支——从队列复用。

> **`(head + 1) & MASK`**: 环形递增。等价于 `(head + 1) % CAPACITY` 但快 20 倍（见上面的位掩码分析）。
>
> 当 `head = 16383`（最大值）:
> ```
> (16383 + 1) & 16383 = 16384 & 16383 = 0  ← 回绕到开头
> ```

**`Entity(idx, generations[idx])`**: 组合 Handle。注意复用时 `generations[idx]` 已在上次 `release()` 中被递增，所以新 Handle 的版本号比旧的更高。

---

### release — 回收实体

```cpp
void release(Entity_index idx) noexcept {
    generations[idx]++;
    ring_buffer[tail] = idx;
    tail = (tail + 1) & MASK;
    --alive_entity_count;
}
```

**三步操作**:
1. **版本号递增**: 所有持有旧版本的 Handle 自动失效。这是防止"使用已销毁实体"的核心安全机制。
2. **入队**: 索引进入环形缓冲区等待复用。
3. **计数减一**。

**缺陷**: 没有双重释放保护。如果同一 `idx` 被 `release` 两次（逻辑 Bug），`generations` 会递增两次，`ring_buffer` 中出现两份相同索引 → 下次 `acquire` 会分配出两个具有相同 index 但不同 generation 的 Handle → 数据覆盖。应该加 assert 检查。

---

### is_valid — 句柄有效性检查

```cpp
[[nodiscard]] bool is_valid(Entity entity) const noexcept {
    return entity.index() < next_idx &&
        generations[entity.index()] == entity.generation();
}
```

**两重防线**:

| 检查 | 防止什么 |
|------|---------|
| `index < next_idx` | 访问从未分配过的索引（开荒水位线之后） |
| `generation 匹配` | 使用已销毁实体的旧句柄（僵尸句柄） |

**案例**:
```
Entity e1 = pool.acquire();  // index=0, gen=0
pool.release(e1.index());    // generations[0] → 1
Entity e2 = pool.acquire();  // index=0, gen=1

pool.is_valid(e1) → gen 0 ≠ gen 1 → false  ✓ 旧句柄被正确识别为无效
pool.is_valid(e2) → gen 1 == gen 1 → true   ✓ 新句柄有效
```

---

## 第二部分：Registry — 注册表

### 核心数据结构

```cpp
class Registry {
private:
    template<typename... Components> friend class View;

    EntityPool entity_pool;
    std::array<Signature, MAX_ENTITIES> entity_signatures;
    std::array<std::unique_ptr<ISparseSet>, MAX_COMPONENTS> Components_Pool;
```

**内存分析**:
| 成员 | 大小 | 说明 |
|------|------|------|
| `entity_pool` | ~96KB | generations(32KB) + ring_buffer(32KB) + 其他 |
| `entity_signatures` | 128KB | 16384 × bitset<64>(8B) |
| `Components_Pool` | 512B | 64 × unique_ptr(8B) |
| **总计** | **~225KB** | 不含组件池本身的 Dense 数据 |

> **语法知识 — `friend` 声明**:
>
> ```cpp
> template<typename... Components> friend class View;
> ```
> 允许 `View<任意组件组合>` 访问 `Registry` 的所有 `private` 成员。
>
> **为什么需要 friend？** `View` 需要直接调用 `get_pool<T>()` 获取组件池，但这是 `private` 方法（不应被外部随意调用）。`friend` 是一种精确控制的访问权限授予。
>
> **替代方案**: 把 `get_pool` 改成 `public`。更简单但暴露了内部实现。friend 是"最小权限"原则的体现。

> **语法知识 — `std::unique_ptr`**:
>
> 独占所有权智能指针。特点：
> - 不可拷贝（`unique_ptr<T> a = b;` 编译错误）
> - 可移动（`unique_ptr<T> a = std::move(b);` 转移所有权）
> - 析构时自动 `delete` 持有的对象
> - `.get()` 返回裸指针但不转移所有权
>
> ```cpp
> // 创建
> auto p = std::make_unique<SparseSet<T>>();  // 在堆上构造，返回 unique_ptr
>
> // 使用
> p->get(entity);     // 通过 -> 访问
> (*p).get(entity);   // 通过 * 解引用
>
> // 销毁
> // p 的析构函数自动调用 delete，无需手动管理
> ```

---

### get_pool — 延迟初始化的组件池获取

```cpp
template<typename T>
[[nodiscard]] SparseSet<T>& get_pool() {
    Component_ID id = get_component_type_id<T>();
    assert(id < MAX_COMPONENTS && "Component ID out of range!");

    if (Components_Pool[id] == nullptr) {
        Components_Pool[id] = std::make_unique<SparseSet<T>>();
    }

    return *static_cast<SparseSet<T>*>(Components_Pool[id].get());
}
```

**延迟初始化 (Lazy Initialization)**: 组件池只在首次使用时创建。

| 策略 | 时机 | 优势 | 缺陷 |
|------|------|------|------|
| **延迟初始化（当前）** | 首次 emplace/get 时 | 不浪费内存在未使用的组件类型上 | 首次使用有一次堆分配 |
| 预分配 | Registry 构造时 | 运行时零分配 | 需要提前知道所有组件类型 |
| 手动注册 | 用户显式调用 `register<T>()` | 最灵活 | 忘记注册 → 运行时崩溃 |

> **语法知识 — `static_cast` 向下转型**:
>
> ```cpp
> return *static_cast<SparseSet<T>*>(Components_Pool[id].get());
> ```
>
> 这里做了"向下转型"（downcasting）：从基类指针 `ISparseSet*` 转为派生类指针 `SparseSet<T>*`。
>
> **安全性**: `static_cast` 不做运行时检查。如果 `Components_Pool[id]` 实际上不是 `SparseSet<T>` 类型 → 未定义行为。但由于 `get_component_type_id<T>()` 保证类型 T 的 ID 唯一且一致，这里的转型是安全的。
>
> **替代方案 `dynamic_cast`**: 会做运行时类型检查（通过 RTTI），失败返回 `nullptr`。更安全但有性能开销（~100ns/call），且要求编译器开启 RTTI。

---

### emplace / get / try_get / remove

```cpp
template<typename T, typename... Args>
[[nodiscard]] T& emplace(Entity entity, Args&&... args) {
    assert(is_alive(entity));
    Component_ID id = get_component_type_id<T>();
    entity_signatures[entity.index()].set(id);
    return get_pool<T>().emplace(entity, std::forward<Args>(args)...);
}
```

**两层操作**: 签名层（`bitset::set`）+ 存储层（`SparseSet::emplace`）。必须保持同步。

---

```cpp
template<typename T>
[[nodiscard]] std::optional<std::reference_wrapper<T>> try_get(Entity entity) noexcept {
    if (!is_alive(entity)) return std::nullopt;
    Component_ID id = get_component_type_id<T>();
    if (!entity_signatures[entity.index()][id]) return std::nullopt;
    return std::ref(get_pool<T>().get(entity));
}
```

> **语法知识 — `std::optional<std::reference_wrapper<T>>`**:
>
> **问题**: `std::optional<T&>` 在 C++17 中不合法——`optional` 不能直接持有引用。
>
> **解决**: `std::reference_wrapper<T>` 是引用的包装器。它是一个可拷贝的对象，内部存储指针，但表现得像引用：
> ```cpp
> int x = 42;
> std::reference_wrapper<int> ref = std::ref(x);
> ref.get() = 100;  // 等效于 x = 100
> ```
>
> **使用模式**:
> ```cpp
> auto result = reg.try_get<Transform>(entity);
> if (result) {
>     result->get().x = 100;  // 有值：通过 reference_wrapper 访问
> }
> // 无值：result == std::nullopt
> ```

---

### destroy_entity — 硬件加速位遍历

```cpp
void destroy_entity(Entity entity) {
    Signature& sig = entity_signatures[entity.index()];

    if constexpr (MAX_COMPONENTS <= 64) {
        if (sig.all()) {
            for (Component_ID id = 0; id < MAX_COMPONENTS; ++id) {
                if (Components_Pool[id] != nullptr)
                    Components_Pool[id]->remove(entity);
            }
        }
        else {
            unsigned long long n = sig.to_ullong();
            while (n > 0) {
                int count = std::countr_zero(n);
                Component_ID index = static_cast<Component_ID>(count);
                if (Components_Pool[index] != nullptr)
                    Components_Pool[index]->remove(entity);
                n &= (n - 1);
            }
        }
    }

    sig.reset();
    entity_pool.release(entity.index());
}
```

> **语法知识 — `if constexpr` (C++17)**:
>
> 编译期条件分支。不满足条件的分支在编译时**完全被移除**（连语法检查都跳过）。与运行时 `if` 的区别：
> ```cpp
> if constexpr (sizeof(T) > 8) { /* 只有 T 大于 8 时才编译这段 */ }
> if (sizeof(T) > 8) { /* 即使不走这里也要编译通过 */ }
> ```

> **语法知识 — `std::countr_zero` (C++20)**:
>
> 统计整数末尾有多少个连续的 0 位。等于"最低位的 1 在第几位"。
>
> 对应 x86 硬件指令:
> - `TZCNT`（Trailing Zero Count）: Intel Haswell+ 原生支持，单周期
> - `BSF`（Bit Scan Forward）: 老指令，功能相同，大多平台也是单周期
>
> ```
> countr_zero(0b101000) = 3  → 最低位的 1 在第 3 位
> countr_zero(0b000001) = 0  → 最低位的 1 在第 0 位
> countr_zero(0b000000) = 64 → 无 1（对 uint64_t）
> ```

> **语法知识 — `n &= (n - 1)` 清除最低位的 1**:
>
> 经典位运算技巧，Brian Kernighan 算法的核心操作：
> ```
> n     = 0b101000
> n - 1 = 0b100111  (从最低位的 1 开始，翻转它及以下所有位)
> n & (n-1)= 0b100000  (最低位的 1 被清除)
> ```

**执行流程**:
```
sig = 0b00101010  (实体有组件 1, 3, 5)

循环 1: countr_zero(0b00101010) = 1 → remove 组件1
         n &= (n-1) → 0b00101000

循环 2: countr_zero(0b00101000) = 3 → remove 组件3
         n &= (n-1) → 0b00100000

循环 3: countr_zero(0b00100000) = 5 → remove 组件5
         n &= (n-1) → 0b00000000

n == 0 → 退出循环（只循环了 3 次，而非 64 次）
```

**为什么 `sig.all()` 需要特殊处理？** `bitset<64>::to_ullong()` 在所有位都为 1 时返回 `0xFFFFFFFFFFFFFFFF`。这本身没问题，但一些编译器在 `bitset.all()` 的情况下对 `to_ullong()` 的行为有微妙的差异。为安全起见，对全满的签名走朴素遍历。

---

## 第三部分：View — 多组件联合查询

### 设计意图

**问题**: System 需要遍历"同时拥有 Transform 和 Velocity 的所有实体"。如何高效实现？

**可选方案**:

| 方案 | 复杂度 | Cache | 代码 |
|------|--------|-------|------|
| 遍历全部实体 + 检查签名 | O(MAX_ENTITIES) | ✗ 访问 128KB 签名数组 | 简单 |
| **最小池驱动 + 检查其他池（当前）** | **O(最小池 size)** | **✓ 只碰必要的 Sparse** | 中等 |
| Archetype 表 | O(匹配实体数) | ✓ 按组件组合分组 | 复杂 |

**选择最小池驱动的理由**: 平衡性能和复杂度。对 `View<Transform, Speed>` 且 Speed 只有 5 个实体时，只遍历 5 次——而非 16384 个签名。

**核心优化思想**:
- 构造时缓存裸指针 → 遍历时零虚函数调用
- 不碰 128KB 的 `entity_signatures` → 工作集从 ~192KB 降至 ~64KB
- 检查其他池的 `has()` 加载的 Cache Line 会被后续 `get<T>()` 复用 → 免费预取

---

### View 数据成员

```cpp
template<typename... Components>
class View {
private:
    Registry& reg;
    ISparseSet* smallest_pool;
    const Entity* cached_entities;
    size_t cached_size;

    static constexpr size_t POOL_COUNT = sizeof...(Components);
    std::array<ISparseSet*, POOL_COUNT> other_pools{};
    size_t other_count = 0;
```

> **语法知识 — `sizeof...(Components)`**:
>
> 参数包的元素数量。对 `View<A, B, C>` 返回 3。这是编译期常量，可以用于 `std::array` 的大小。

---

### find_smallest — 折叠表达式

```cpp
void find_smallest() {
    size_t min_size = SIZE_MAX;
    ([&] {
        auto& pool = reg.get_pool<Components>();
        if (pool.size() < min_size) {
            min_size = pool.size();
            smallest_pool = &pool;
        }
    }(), ...);
}
```

> **语法知识 — C++17 折叠表达式**:
>
> `(expr, ...)` 对参数包中的每个类型展开表达式。
>
> 对 `View<A, B, C>`:
> ```cpp
> // 展开为:
> ([&]{ /* 检查池 A */ }()),
> ([&]{ /* 检查池 B */ }()),
> ([&]{ /* 检查池 C */ }())
> ```
>
> **四种折叠形式**:
> | 语法 | 名称 | 展开 |
> |------|------|------|
> | `(E, ...)` | 一元右折叠 | `E1, (E2, (E3))` |
> | `(..., E)` | 一元左折叠 | `((E1), E2), E3` |
> | `(E, ..., init)` | 二元右折叠 | `E1, (E2, (E3, init))` |
> | `(init, ..., E)` | 二元左折叠 | `((init, E1), E2), E3` |
>
> 这里用的是一元右折叠 + 逗号运算符。逗号运算符 `(a, b)` 依次执行 a 和 b，返回 b 的值。

> **语法知识 — Lambda 捕获 `[&]`**:
>
> `[&]` 按引用捕获所有外部变量。Lambda 内部可以读写 `min_size`、`smallest_pool` 等。
>
> **捕获模式**:
> | 语法 | 含义 |
> |------|------|
> | `[&]` | 按引用捕获全部 |
> | `[=]` | 按值拷贝全部 |
> | `[&x, y]` | x 按引用，y 按值 |
> | `[this]` | 捕获 this 指针 |
> | `[&, x]` | 全部按引用，但 x 按值 |

---

### viewIterator — 迭代器

```cpp
struct viewIterator {
    const View& view;
    size_t index;

    viewIterator(const View& v, size_t i) : view(v), index(i) {
        if (index < view.cached_size && !is_valid()) {
            ++(*this);
        }
    }
```

**构造时跳过无效位置**: 如果 `begin()` 指向的第一个实体不满足所有组件要求，自动前进到第一个有效位置。

---

```cpp
    bool is_valid() const {
        Entity candidate = view.cached_entities[index];
        for (size_t i = 0; i < view.other_count; ++i) {
            if (!view.other_pools[i]->has(candidate)) return false;
        }
        return true;
    }
```

**过滤逻辑**: 从最小池取出候选实体，检查它是否也存在于其他所有池中。

**关键性能特征**: `has()` 调用加载了该池 Sparse 数组对应位置的 Cache Line。紧接着的 `get<T>()` 访问同一 Sparse 位置 → Cache 命中。这相当于**免费的预取**。

---

```cpp
    viewIterator& operator++() {
        index++;
        while (index < view.cached_size && !is_valid()) {
            index++;
        }
        return *this;
    }
```

> **语法知识 — 前置 vs 后置 `++`**:
>
> ```cpp
> viewIterator& operator++();      // 前置: ++it，返回自身引用
> viewIterator operator++(int);    // 后置: it++，返回旧值的拷贝
> ```
> 前置更高效（无需拷贝旧值）。range-for 循环只使用前置 `++`。

---

```cpp
    Entity operator*() const {
        return view.cached_entities[index];
    }
```

解引用返回实体。注意这是**值返回**而非引用——Entity 只有 4 字节，值返回放寄存器比引用更快。

---

## 文件级总结

| 设计决策 | 选择 | 替代方案 | 选择理由 |
|---------|------|---------|---------|
| 索引回收 | 环形缓冲区 FIFO | 自由链表 LIFO, 位图 | FIFO 最大化僵尸句柄检测窗口 |
| 实体池容量 | 2 的幂 (16384) | 任意值 | 位掩码取模 (1 cycle vs 20 cycles) |
| View 驱动策略 | 最小池 | 遍历签名, Archetype | 平衡性能与实现复杂度 |
| View 缓存 | 裸指针 + 大小 | span, 保留虚函数 | 零虚函数调用，一次缓存永久使用 |
| 组件销毁 | 硬件位迭代 | 逐位检查 64 位 | 只循环"已设置位数"次 |
| 组件获取 | get() 快路径 + try_get() 安全路径 | 只提供一种 | 系统内部用 get，用户侧用 try_get |
