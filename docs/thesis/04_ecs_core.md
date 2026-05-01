# 第 4 章 ECS 核心设计与实现

ECS 核心是 Project Rinn 的"语法基底"。引擎的所有子系统、脚本绑定与资源管理都建立在本章所介绍的几个抽象之上：32 位的 Entity 句柄、负责索引复用的 EntityPool、O(1) 增删的 SparseSet、模板化的 ComponentID、统一对外的 Registry，以及多组件交集查询的 View。本章按照"自下而上"的顺序逐一展开，并在每节末尾标注对应的源码位置，便于读者交叉对照。

## 4.1 Entity 句柄设计

游戏世界由大量"实体"组成，每个实体可能持有任意子集的组件。为兼顾标识效率与生命周期安全，Entity 句柄需要回答两个问题：**如何在 O(1) 时间内定位实体？** **如何识别一个旧句柄是否已经失效？**

### 4.1.1 32 位整数的位布局

`src/Core/Types.hpp` 中定义的 `Entity` 是一个 POD（Plain Old Data）结构体，唯一成员是一个 `uint32_t`，位布局如下：

```
位 31                                位 16  位 15                                位 0
┌──────────────────────────────────────┬───────────────────────────────────────┐
│            Generation (16 位)        │              Index (16 位)            │
└──────────────────────────────────────┴───────────────────────────────────────┘
```

低 16 位 Index 用于在数组中定位实体；高 16 位 Generation 用于版本控制。两个掩码常量 `INDEX_MASK = 0xFFFF` 与 `GENERATION_SHIFT = 16` 在 `constexpr` 编译期就被求值，使得 `index()` 与 `generation()` 函数可以被内联为单条 `and` 或 `shr` 指令。`Entity` 同时通过 `friend auto operator<=>(Entity, Entity) = default;` 启用 C++20 默认三路比较，便于作为容器键或在测试中使用。

### 4.1.2 ABA 问题与版本号的作用

ECS 的实体索引会被反复复用：一个实体被销毁后，其索引迟早会被分配给另一个实体。如果系统中仍有持有旧句柄的引用（例如 Lua 脚本缓存了某个对象的 Entity），并且不加判断地按索引访问，就会读取到一个"形似还活着、实则已经是新身份"的实体——这正是经典的 ABA 问题。

解决之道是为每个索引绑定一个单调递增的"版本号"。当索引被释放时版本号加一；只有完整匹配 `(index, generation)` 的句柄才被认定为活跃。本引擎将版本号嵌入实体句柄的高 16 位，使得有效性检查仅需一次内存访问与一次相等比较，开销极小。`is_null()` 函数则用预置常量 `NULL_ID = 0xFFFFFFFF` 表示"无效实体"。

> 代码引用：`src/Core/Types.hpp:21-61`

## 4.2 EntityPool：环形缓冲与版本号自增

实体的创建与销毁需要在 O(1) 时间内完成索引的分配与回收，并在回收时自动维护版本号。`EntityPool` 类承担这一职责，其内部数据结构紧凑且无堆分配。

### 4.2.1 数据布局与编译期约束

```cpp
class EntityPool {
    static constexpr uint16_t CAPACITY = MAX_ENTITIES;
    static constexpr uint16_t MASK = CAPACITY - 1;
    static_assert((CAPACITY & (CAPACITY - 1)) == 0,
                  "CAPACITY must be power of 2 for MASK to work!");

    std::array<Entity_generation, CAPACITY> generations{};
    std::array<Entity_index, CAPACITY> ring_buffer{};

    uint16_t head = 0, tail = 0;
    Entity_index next_idx = 0;
    uint16_t alive_entity_count = 0;
};
```

两个 `std::array` 各占 32 KB，加上几个 `uint16_t` 游标，全部 64 KB 数据连续位于栈上或静态区，对 L1/L2 缓存友好。容量 `CAPACITY = 16384` 是 2 的幂，使得 `tail = (tail + 1) & MASK` 的取模操作可被替换为单条按位与，避免了除法指令的延迟代价。`static_assert` 在编译期对此做了校验，未来若有人调整容量为非 2 的幂，编译会立即失败。

### 4.2.2 acquire：复用与开荒的双路径

`acquire()` 函数返回一个新实体，其内部根据"是否有可复用索引"分为两条路径：

```cpp
[[nodiscard]] Entity acquire() noexcept {
    Entity_index idx;
    if (head != tail) {                       // 1. 复用
        idx = ring_buffer[head];
        head = (head + 1) & MASK;
    } else {                                  // 2. 开荒
        assert(next_idx < CAPACITY && "Entity pool exhausted!");
        idx = next_idx++;
    }
    ++alive_entity_count;
    return Entity(idx, generations[idx]);
}
```

按照"先复用、后开荒"的顺序，可使索引分布尽量紧凑，从而提高后续 SparseSet 的 Sparse 数组的有效命中率。注释中指出"游戏初期主要走 else（开荒）、后期主要走 if（复用）"——这一观察对现代 CPU 的分支预测器是友好的，热路径的稳定预测能进一步降低开销。

### 4.2.3 release：版本号自增与入队

释放索引时，对应位置的 generation 加一，索引入队等待复用：

```cpp
void release(Entity_index idx) noexcept {
    generations[idx]++;
    ring_buffer[tail] = idx;
    tail = (tail + 1) & MASK;
    --alive_entity_count;
}
```

generation 自增是版本号机制的核心：任何此前持有该索引旧版本号的句柄，从此都会在 `is_valid` 检查中被识别为陈旧。`is_valid` 的实现集中检查三件事：索引在水位线之内、索引在物理上限之内、版本号匹配。

> 代码引用：`src/Core/Registry.hpp:16-110`

## 4.3 SparseSet：兼顾 O(1) 与稠密遍历

SparseSet 是 ECS 中最关键的数据结构。第 2 章介绍过其基本原理；本节展开 Project Rinn 的具体实现，包括接口分层、emplace / get / remove 的细节，以及为优化 cache 行为引入的软件预取。

### 4.3.1 接口分层：ISparseSet + SparseSet&lt;T&gt;

`SparseSet` 通过基类 `ISparseSet` 与模板派生 `SparseSet<T>` 形成两层接口：

```cpp
class ISparseSet {
public:
    virtual ~ISparseSet() = default;
    bool has(Entity entity) const noexcept;
    virtual void remove(Entity entity) = 0;
    virtual void clear() = 0;
    virtual size_t size() const noexcept = 0;
    virtual const Entity* entity_data() const noexcept = 0;
protected:
    std::array<Entity_index, MAX_ENTITIES> Sparse;
};

template<typename T>
class SparseSet : public ISparseSet {
    std::vector<T> Dense;
    std::vector<Entity> dense_to_entity;
    // ...
};
```

`ISparseSet` 提供"类型擦除"接口，使得 Registry 可以用 `array<unique_ptr<ISparseSet>, MAX_COMPONENTS>` 一致地管理所有组件池；`SparseSet<T>` 在派生类中保留类型信息，并提供模板化的 emplace / get 等强类型 API。`has` 函数的逻辑足够简单（仅一次数组寻址 + 哨兵比较），因此被设计为非虚的内联实现，避免了在热路径上承担虚函数调用代价。

### 4.3.2 emplace：原地构造与"重复返回旧值"语义

emplace 的核心逻辑如下：

```cpp
template<typename... Args>
requires std::constructible_from<T, Args...>
[[nodiscard]] T& emplace(Entity entity, Args&&... args) {
    if (Sparse[entity.index()] != NULL_COMPONENT_ENTITY) {
        return Dense[Sparse[entity.index()]];   // 重复 emplace 返回旧引用
    }
    if (Dense.size() == Dense.capacity()) {
        size_t new_cap = std::max<size_t>(Dense.capacity() * 2, 8);
        Dense.reserve(new_cap);
        dense_to_entity.reserve(new_cap);
    }
    Dense.emplace_back(std::forward<Args>(args)...);
    dense_to_entity.push_back(entity);
    Sparse[entity.index()] = static_cast<Entity_index>(Dense.size() - 1);
    return Dense.back();
}
```

几个值得注意的设计点：

1. **完美转发与 concepts**：通过 `std::forward<Args>(args)...` 与 C++20 的 `requires std::constructible_from<T, Args...>` 约束，确保任何能用来直接构造 `T` 的参数都能被 emplace 接受，且不可构造的参数会在编译期立即失败。
2. **统一扩容**：`Dense` 与 `dense_to_entity` 必须同步扩容，否则将出现大小不一致；这里采取"先 reserve 后 push_back"的简化异常安全策略——若 reserve 抛出，则两者都未被修改。
3. **重复 emplace 返回旧值，而非覆盖**：这一选择与 EnTT 的 `emplace`/`emplace_or_replace` 语义一致。其优点是把"是否覆盖"这一语义决定显式交给上层；其代价是 LuaBinder 中需要写出"先 remove 再 emplace"的两步代码。

### 4.3.3 swap-and-pop 与 dense_to_entity 的一致性

`remove` 是 SparseSet 中最容易出错的操作。引擎采用 swap-and-pop 惯用法：将被删除元素与 Dense 队尾元素交换，然后弹出队尾。这要求同时维护 Dense、`dense_to_entity` 与 Sparse 三个数组的一致性。完整逻辑见 `SparseSet::remove`，关键步骤为：

1. 取被删元素与队尾元素在 Dense 中的位置；
2. 若被删的就是队尾，直接 `pop_back` 并清 Sparse；
3. 否则将队尾的 `T` move 到被删位置，更新队尾实体在 Sparse 中的索引，再 pop_back；
4. 同步更新 `dense_to_entity`。

在 `tests/ecs_test.cpp` 中，`Remove_MiddleElement_SwapAndPop`、`Remove_FirstElement_SwapAndPop`、`Remove_LastElement_DirectPop` 等用例对这一过程做了细致校验，确保被交换的实体仍能正确 `get`。

### 4.3.4 软件预取：两阶段 prefetch 设计

考虑一个典型场景：物理系统对所有同时持有 Transform 与 Velocity 的实体顺序遍历并更新。每次访问 `pool.get(entity)` 都涉及两次寻址：先读 Sparse、再用其结果索引 Dense。当数据规模超出 L1 时，这两次寻址容易成为流水线瓶颈。引擎为此提供了两阶段预取接口：

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

第一阶段 `prefetch` 将 Sparse 行拉入 L1；第二阶段 `prefetch_dense` 在已知 Sparse 值后将 Dense 行拉入 L1。在热循环中对 `i + PREFETCH_DIST` 的实体调用预取，可以隐藏后续访问的延迟。是否启用预取属于 System 层的优化决策，SparseSet 仅负责提供能力。

### 4.3.5 raw 指针与场景级 reserve

为支持"按 Dense 顺序线性遍历，绕过 Sparse 间接寻址"的高级用法（例如调试 UI 与未来的序列化），SparseSet 暴露了 `raw_data()`、`raw_entity_data()` 接口。同时提供 `reserve(cap)` 用于在场景加载阶段一次性付清 Dense 的内存分配与 page fault 成本。两者均带有"调用者必须保证遍历期间不增删组件"的契约约束。

> 代码引用：`src/Core/SparseSet.hpp`

## 4.4 ComponentID 的编译期分配

每种组件类型都需要一个唯一的 `Component_ID`，用作 Registry 内部组件池数组的下标与 Signature 中的位偏移。Project Rinn 采用模板特化与函数级静态变量结合的"编译期分配"惯用法：

```cpp
struct ComponentCounter {
    inline static std::atomic<Component_ID> counter{ 0 };
};

template <typename T>
Component_ID get_component_type_id() {
    static Component_ID id =
        ComponentCounter::counter.fetch_add(1, std::memory_order_relaxed);
    return id;
}
```

其原理是：函数模板的每一种实例化（即不同的 `T`）都会拥有独立的局部 `static` 变量；该变量的初始化只发生一次，并通过原子计数器获得全局唯一的递增 ID。`std::memory_order_relaxed` 在这里是合理的选择——分配过程不存在依赖排序的需求，仅需保证原子性。

`Component_ID` 的类型为 `uint8_t`，配合 `MAX_COMPONENTS = 64` 的上限，使得 Signature 可以被压缩为 `std::bitset<64>`（8 字节），便于在签名比较与位运算时贴合一字宽度。

> 代码引用：`src/Core/ComponentID.hpp`

## 4.5 Registry：统一接口与签名

Registry 是 ECS 对外的核心入口。它整合 EntityPool、SparseSet 与 Signature，为业务代码提供一致的 emplace / get / has / remove / view / destroy 接口。

### 4.5.1 双层映射：Signature + Components_Pool

Registry 内部维护两张表：

```cpp
std::array<Signature, MAX_ENTITIES> entity_signatures;
std::array<std::unique_ptr<ISparseSet>, MAX_COMPONENTS> Components_Pool;
```

`entity_signatures[i]` 是第 i 个实体当前持有组件类型的位图，提供"该实体是否持有 X 组件"的 O(1) 查询；`Components_Pool[id]` 则按组件 ID 索引到对应的 SparseSet 实例，提供"该组件类型的所有实例"的 O(1) 查询。两张表共同覆盖了"实体维度"与"组件维度"的双向访问需求。

### 4.5.2 emplace / has / get / remove

Registry 的对外 API 在内部转发给底层结构：

- `emplace<T>(e, args...)` 调用 `get_pool<T>().emplace(e, args...)` 并设置签名位；
- `has<T>(e)` 直接检查签名位，避开池查询；
- `get<T>(e)` 在 debug 模式下通过 `assert(has<T>(e))` 校验前置条件，然后调用池的 get；
- `remove<T>(e)` 同时清除签名位与池中数据。

`try_get<T>` 提供安全路径，返回 `std::optional<std::reference_wrapper<T>>`，用于业务代码中可能不存在组件的场景。这一双路径设计区分了"我知道它在"与"我不确定"两种意图，使得 System 内部的高频访问可以走零开销的 `get`，而脚本端的偶发查询可以走带空判断的 `try_get`。

### 4.5.3 destroy_entity 与位扫描

销毁实体时，需要遍历它持有的所有组件并依次从对应池中移除。直接对 `MAX_COMPONENTS = 64` 做线性扫描虽然可行，但若实体只持有少数几个组件，浪费的循环次数较多。Registry 利用了 `std::countr_zero` 的硬件位扫描指令对此进行加速：

```cpp
unsigned long long n = sig.to_ullong();
while (n > 0) {
    int count = std::countr_zero(n);              // 找最低有效位
    Component_ID index = static_cast<Component_ID>(count);
    if (Components_Pool[index] != nullptr) {
        Components_Pool[index]->remove(entity);   // 通过基类指针虚分发
    }
    n &= (n - 1);                                 // 清除最低有效位
}
```

`std::countr_zero` 在 x86 上对应 `BSF/TZCNT` 指令，单周期完成；`n &= (n - 1)` 是经典的"清除最低位 1"位技巧。这两条结合起来，让循环次数严格等于实体实际持有的组件数。当实体几乎持有所有组件时，则退化到对 64 个池的直接遍历，对应代码的 `if (sig.all())` 分支。

### 4.5.4 延迟创建组件池与可观察的副作用

`get_pool<T>()` 在第一次被调用时为对应组件类型创建 SparseSet 实例：

```cpp
template<typename T>
[[nodiscard]] SparseSet<T>& get_pool() {
    Component_ID id = get_component_type_id<T>();
    if (Components_Pool[id] == nullptr) {
        Components_Pool[id] = std::make_unique<SparseSet<T>>();
    }
    return *static_cast<SparseSet<T>*>(Components_Pool[id].get());
}
```

这一"延迟初始化"的策略的优点是：未被使用的组件类型不会占用 Sparse 数组的 32 KB 内存。其代价是 `get_pool` 具有可观察的副作用——即便仅做读操作（如 View 构造），也会写入 `Components_Pool`。这一权衡在第 9 章的不足之处中亦有讨论。

> 代码引用：`src/Core/Registry.hpp:112-271`

## 4.6 View：变长模板与最小池驱动

View 提供 ECS 中最常用的查询模式：枚举所有同时持有指定多个组件类型的实体。

### 4.6.1 最小池驱动的几何直觉

若 `view<A, B, C>` 要枚举同时持有 A、B、C 三种组件的实体，最朴素的做法是对全部实体的 Signature 做"按位与"判断，但这浪费了已有的池结构。一个更优的策略是：**找出三种组件中实例数最少的池作为驱动池，仅遍历该池中的实体；对每个候选实体，再逐一检查其余两种组件是否也存在。**

这种"最小池驱动"的算法在最坏情况下不会比朴素遍历更差，但在组件分布不均（实际游戏场景的常态）下能显著减少候选数量，是 EnTT 等成熟 ECS 库的标准做法。

### 4.6.2 构造期缓存：消除热循环中的虚函数

View 的 C++ 实现核心如下：

```cpp
template<typename... Components>
class View {
    Registry& reg;
    ISparseSet* smallest_pool;
    const Entity* cached_entities;          // 直接指向 dense_to_entity.data()
    size_t cached_size;

    static constexpr size_t POOL_COUNT = sizeof...(Components);
    std::array<ISparseSet*, POOL_COUNT> other_pools{};
    size_t other_count = 0;
    // ...
};
```

构造时，`find_smallest()` 遍历 `Components...` 找到 size 最小的池作为驱动池；`cache_other_pools()` 把剩余池的指针缓存到 `other_pools`；`cached_entities` 与 `cached_size` 则直接指向最小池的 dense 实体数组与其大小。三组缓存使得后续遍历过程中**不再触碰 Registry**，所有访问都走裸指针，彻底消除虚函数调用与 sparse 数组扫描。

### 4.6.3 viewIterator 的惰性求值

迭代器的 `is_valid()` 通过对每个非主池调用 `has` 来判断当前候选实体是否完整匹配；`operator++` 则做盲走 + 惰性跳过：

```cpp
viewIterator& operator++() {
    index++;
    while (index < view.cached_size && !is_valid()) {
        index++;
    }
    return *this;
}
```

构造时若 `index = 0` 的实体不合法，会在 viewIterator 构造函数中立即触发一次 `++`，使迭代器始终指向合法实体或终止位置。`begin()` 返回 `viewIterator(*this, 0)`、`end()` 返回 `viewIterator(*this, cached_size)`，因此区间 for 循环 `for (Entity e : reg.view<A, B>())` 直接可用。

### 4.6.4 工作集分析

最小池驱动策略的另一个收益体现在内存工作集上。若使用 `entity_signatures` 全表扫描，工作集为 `MAX_ENTITIES × sizeof(Signature) = 16384 × 8 = 128 KB`；而最小池驱动方案下，仅需访问驱动池的 dense 实体数组与若干个 Sparse 数组，对 `view<Transform, Velocity>` 这一二组件查询而言，工作集约为 32 KB（一个非主池的 Sparse 数组）+ 若干 KB 的 dense 实体数据，远低于 128 KB。这种"少触碰内存"的策略对热循环的 cache 命中率极为友好。

> 代码引用：`src/Core/Registry.hpp:274-391`

---

至此，ECS 核心的全部组件已经完整呈现。下一章将进入子系统层，介绍这些核心抽象如何被各个 System 复用以完成具体的游戏功能。
