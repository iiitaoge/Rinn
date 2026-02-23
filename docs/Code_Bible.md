# Project Rinn 代码圣经 (Code Bible)

> **文档说明**：本文档对 Project Rinn 引擎的每一行核心代码、每一个架构决策、每一个 C++ 语法特性进行了无比详细的解构。旨在为开发者提供上帝视角的代码理解。

---

## 目录 (Table of Contents)

1.  **核心层 (Core Layer)** - 引擎的灵魂
    *   [`Types.hpp`](#1-coretypeshpp-基础类型定义)
    *   [`ComponentID.hpp`](#2-corecomponentidhpp-类型元编程魔法)
    *   [`SparseSet.hpp`](#3-coresparsesethpp-稀疏集数据结构)
    *   [`Registry.hpp`](#4-coreregistryhpp-ecs-注册表)
    *   [`World.hpp`](#5-coreworldhpp-世界容器)
2.  **组件层 (Components Layer)** - 数据定义
    *   [`Components.hpp`](#6-componentscomponentshpp-组件定义)
3.  **系统层 (Systems Layer)** - 业务逻辑
    *   [`PhysicsSystem.hpp`](#7-systemsphysicssystemhpp-物理系统)
    *   [`CollisionSystem.hpp`](#8-systemscollisionsystemhpp-碰撞系统)
    *   [`RenderSystem.hpp`](#9-systemsrendersystemhpp-渲染系统)
    *   [`InputSystem.hpp`](#10-systemsinputsystemhpp-输入系统)
4.  **资源层 (Resource Layer)** - 数据仓库
    *   [`ResourceManager.hpp`](#11-resourcesresourcemanagerhpp-资源管理器)
    *   [`TileMap.hpp`](#12-resourcestilemaphpp-瓦片地图)
    *   [`PrefabManager.hpp`](#13-resourcesprefabmanagerhpp-预制体工厂)
5.  **脚本层 (Scripting Layer)** - 胶水层
    *   [`ScriptContext.hpp`](#14-scriptingscriptcontexthpp-lua-上下文)
    *   [`LuaBinder.hpp`](#15-scriptingluabinderhpp-lua-绑定)
6.  **入口 (Entry Point)**
    *   [`main.cpp`](#16-maincpp-程序入口)

---

## 1. `Core/Types.hpp` - 基础类型定义

**文件职责**：定义整个引擎通用的基础数据类型、常量和宏。

### 1.1 `Entity` 结构体 (实体句柄)

这是 ECS 中最基础的“身份ID”。

```cpp
struct Entity {
    uint32_t id = 0; // 唯一数据成员：32位整数
    // ...
};
```

*   **内存布局**：
    *   低 16 位 (`index`): 数组索引，用于在 O(1) 时间内定位数据。
    *   高 16 位 (`generation`): 版本号，用于解决“悬垂指针”问题（即 ID 复用导致的逻辑错误）。
*   **语法点**：
    *   `constexpr`: 声明为编译期常量函数，允许编译器极度优化（如直接替换为立即数）。
    *   `[[nodiscard]]`: C++17 特性，如果调用者忽略了返回值（例如只调用 `e.index()` 但不使用结果），编译器会报警。
    *   `operator<=>`: C++20 "Spaceship Operator" (太空飞船运算符)。编译器自动生成 `<, <=, ==, !=, >=, >` 所有比较函数。

### 1.2 `Signature` (组件签名)

```cpp
using Signature = std::bitset<MAX_COMPONENTS>;
```

*   **数据结构**：`std::bitset<64>`。本质上是一个 `uint64_t`。
*   **用途**：
    *   每一位代表一种组件类型。
    *   第 0 位是 Transform，第 1 位是 Sprite...
    *   通过位运算（AND, EQUAL）极速判断实体是否拥有特定的一组组件。

---

## 2. `Core/ComponentID.hpp` - 类型元编程魔法

**文件职责**：为每一个 Component 类型（如 `Transform`）在运行时分配一个唯一的整数 ID。

### 2.1 `get_component_type_id<T>()`

```cpp
template <typename T>
Component_ID get_component_type_id() {
    static Component_ID id = ComponentCounter::counter.fetch_add(1, ...);
    return id;
}
```

*   **核心机制**：利用 C++ 模板实例化的特性。
    *   对于 `get_component_type_id<Transform>()`，编译器生成一份函数代码，里面有一个**独立**的 `static id` 变量。
    *   对于 `get_component_type_id<Velocity>()`，编译器生成另一份代码，拥有**另一个独立**的 `static id`。
*   **原子操作**：`std::atomic` 保证即使在多线程初始化时，ID 也不会重复（虽然本引擎目前是单线程）。
*   **结果**：
    1.  第一次调用 `get<A>`，返回 0。
    2.  第一次调用 `get<B>`，返回 1。
    3.  再次调用 `get<A>`，因为 `static` 变量已存在，直接返回 0。

---

## 3. `Core/SparseSet.hpp` - 稀疏集数据结构

**文件职责**：DOD 的核心容器。实现“数据紧凑存储”同时支持“O(1) 随机访问”。

### 3.1 `ISparseSet` (接口类)

```cpp
class ISparseSet {
    // ...
    virtual const Entity* entity_data() const noexcept = 0;
};
```

*   **设计模式**：类型擦除 (Type Erasure)。
    *   `Registry` 需要存储一个异构的容器列表（存 `SparseSet<int>`, `SparseSet<float>` ...）。
    *   C++ 容器只能存同种类型，所以它们必须继承自同一个基类 `ISparseSet`。
*   **虚函数优化**：
    *   `entity_data()` 这种纯虚函数被设计为**只在迭代开始前调用一次**，获取到底层指针后，后续循环不再虚调用。

### 3.2 `SparseSet<T>` (模板实现类)

这是 DOD 铁律 1 (Layout) 的守护者。

#### 数据成员

```cpp
std::vector<T> Dense;                  // 真正存组件数据 (紧凑, Cache Friendly)
std::vector<Entity> dense_to_entity;   // 反向索引：Dense[i] 属于哪个 Entity
std::array<Entity_index, MAX> Sparse;  // 稀疏索引：EntityID -> DenseIndex
```

#### `emplace` (原地构造)

```cpp
template<typename... Args>
requires std::constructible_from<T, Args...>
T& emplace(Entity entity, Args&&... args) { ... }
```

*   **语法点**：
    *   `requires` (C++20 Concepts): 编译期约束。确保传入的参数 `args` 真的能构造出类型 `T`。如果不能，编译期直接报错，而不是报长篇大论的模板错误。
    *   `std::forward`: 完美转发。将参数均封不动（保留左值/右值属性）地传给 `T` 的构造函数。
*   **算法逻辑**：
    1.  在 `Dense` 尾部 `push_back` 新组件。
    2.  记录 `Sparse[entityID] = Dense.last_index`。
    3.  这样：通过 EntityID 查 Sparse 表拿到 Index，再通过 Index 访问 Dense 拿到数据。

#### `remove` (O(1) 删除)

**核心 trick**：为了保持 Dense 数组连续，删除元素时，不使用 `vector::erase`（那是 O(N) 的），而是：
1.  把 Dense 数组**最后一个元素**搬运到被删除元素的坑位覆盖它。
2.  更新最后一个元素在 Sparse 表中的索引。
3.  `pop_back` 删掉末尾。
这是经典的 **Swap-and-Pop** 技术。

---

## 4. `Core/Registry.hpp` - ECS 注册表

**文件职责**：ECS 的“数据库管理系统”。管理 Entity 生命周期，协调 Component 存储。

### 4.1 `EntityPool` (实体池)

```cpp
std::array<Entity_index, CAPACITY> ring_buffer;
std::array<Entity_generation, CAPACITY> generations;
```

*   **设计**：
    *   **Ring Buffer (环形缓冲区)**：用于复用死掉的实体 ID。`head` 和 `tail` 两个指针追逐。
    *   **Generation 数组**：解决复用冲突。每当实体死亡，`generations[id]++`。新的实体句柄必须携带这个新版本号才有效。

### 4.2 `View` (视图迭代器)

这是引擎性能的关键。

```cpp
template<typename... Components>
class View { ... };
```

*   **工作原理**：
    1.  **Find Smallest**: 在构造时，找出需要的组件中，数量最少的那个组件池（比如 `view<Transform, Player>`，`Player` 只有 1 个，`Transform` 有 10000 个，迭代器会选择遍历 `Player` 池）。
    2.  **Short-Circuit Evaluation**: 遍历最小池的 Entity，检查它是否拥有其他所有组件。
*   **迭代器魔法 (`viewIterator`)**：
    *   它是一个前向迭代器。
    *   `operator++`: 并不只是简单的 `index++`。它是一个 `while` 循环：只要当前指向的实体不符合所有组件要求，就一直 ++，直到找到一个合法的，或者遍历结束。
    *   **Cached Pointers**: 迭代器内部直接持有 `Entity*` 裸指针，把虚函数调用完全挡在循环外面。

---

## 5. `Core/World.hpp` - 世界容器

**文件职责**：纯数据聚合体。符合 DOD 铁律 3 (System 是工人，World 是仓库)。

```cpp
struct World {
    Registry registry;
    TileMap tilemap;
    Context ctx;
    ResourceManager resources;
    // ...
};
```

*   **特征**：
    *   **Struct**: 默认 public，表明它只是数据的集合。
    *   **无方法**: 没有任何业务逻辑方法（如 `Update`）。它的数据由 Systems 来操作。
    *   **分层存储**: 显式区分了 `Registry` (动态实体) 和 `TileMap` (静态环境)。

---

## 6. `components/Components.hpp` - 组件定义

**文件职责**：定义所有游戏数据组件。

*   **POD (Plain Old Data)**：
    *   所有 struct 都没有构造函数、析构函数、虚函数。
    *   例如 `Transform { float x, y; int layer; }`。
    *   这种数据结构在内存中是可以直接 `memcpy` 的，且极度紧凑。

---

## 7. `Systems/PhysicsSystem.hpp` - 物理系统

**文件职责**：实现运动积分逻辑。

```cpp
inline void Update(Registry& r, float dt) {
    for (Entity e : r.view<Transform, Velocity>()) {
        // ...
    }
}
```

*   **DOD 特性**:
    *   `namespace` 而非 `class`：表明它是一组函数，而不是对象。
    *   函数式编程风格：输入 `Registry` 数据，原位修改，不持有任何状态。

---

## 8. `Systems/CollisionSystem.hpp` - 碰撞系统

**文件职责**：处理 AABB 碰撞检测与响应。

*   **Pipeline 设计**：分为两个阶段函数：
    1.  `SavePositions`: 发生在物理更新**前**。备份当前帧的合法位置。
    2.  `Resolve`: 发生在物理更新**后**。检查新位置是否非法（撞墙），如果非法，回滚到旧位置或滑动。
*   **TileMap 碰撞**：
    *   将浮点坐标 `(x, y)` 转换为网格坐标 `(gx, gy)`。
    *   查询 `TileMap` 数组，如果是 `WALL`，则发生碰撞。
    *   O(1) 复杂度。

---

## 9. `Systems/RenderSystem.hpp` - 渲染系统

**文件职责**：封装 Raylib 绘图 API。

*   **State Management**: `BeginFrame` / `EndFrame` 管理渲染状态机。
*   **TileMap 渲染**: 双重循环遍历可见区域的 tile 并绘制。
*   **Sprite 渲染**:
    *   遍历 `view<Transform, Sprite>`。
    *   使用 `ResourceManager` 根据 `texture_id` 获取 `Texture2D` 指针。
    *   调用 `DrawTexturePro` 进行绘制。

---

## 10. `Systems/InputSystem.hpp` - 输入系统

**文件职责**：提供输入查询。

*   **Zero Overhead**:
    *   所有函数都是 `inline`。
    *   本质上是直接调用 Raylib 的 `IsKeyDown`。
    *   没有任何中间层状态存储，完全透传。

---

## 11. `Resources/ResourceManager.hpp` - 资源管理器

**文件职责**：加载和缓存纹理。

*   **缓存机制**:
    *   `unordered_map<string, uint16_t> path_to_id`: 避免重复加载同一张图片。
    *   `vector<Texture2D> textures`: 实际存储。外部组件只持有 `uint16_t` 索引。
*   **RAII**:
    *   析构函数 `~ResourceManager()` 中调用 `UnloadTexture`，防止显存泄漏。

---

## 12. `Resources/TileMap.hpp` - 瓦片地图

**文件职责**：一维数组存储二维网格数据。

*   **扁平化数组**:
    *   使用 `std::vector<uint8_t>` 而不是 `vector<vector<uint8_t>>`。
    *   访问公式：`index = y * width + x`。
    *   优势：内存绝对连续，极大减少 Cache Miss。

---

## 13. `Resources/PrefabManager.hpp` - 预制体工厂

**文件职责**：定义“如何创建一个特定类型的实体”。

```cpp
using PrefabSpawner = std::function<Entity(Registry&, ...)>;
```

*   **Lambda 闭包**:
    *   预制体本质上是一个存储起来的 Lambda 函数。
    *   例如 "player" 对应的 Lambda 会执行：创建一个实体 -> 挂载 Transform -> 挂载 Sprite -> 返回实体。

---

## 14-16. 脚本层与入口

### `main.cpp`
*   **显式流水线**: 每一行代码代表一个处理阶段 (Input -> Script -> Physics -> Collision -> Render)。
*   **Lua 生命周期**: 初始化 Lua -> 绑定 API -> 加载脚本 -> 每一帧调用 `on_update`。

### `LuaBinder.hpp`
*   **Sol2 库**: 使用 Sol2 库作为 C++ 和 Lua 的桥梁。
*   **Intent Pattern (意图模式)**:
    *   Lua 函数 `move(e, ...)` 并不直接移动实体，而是修改 `Velocity` 组件。
    *   真正的移动由下一阶段的 `PhysicsSystem` 统一执行。这就是“Lua 设策略，C++ 做机制”。

---

**文档结束**
