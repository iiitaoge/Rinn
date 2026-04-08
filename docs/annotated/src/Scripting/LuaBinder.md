# LuaBinder.hpp — C++/Lua 绑定层（深度注释版）

> 文件路径: `src/Scripting/LuaBinder.hpp`  
> 角色: 将 C++ 引擎能力暴露给 Lua 脚本。是"C++ 做机制，Lua 做策略"架构的桥梁。

---

## 文件级设计意图

**问题**: 如何让 Lua 脚本调用 C++ 函数、操作 C++ 对象？

**可选方案**:

| 方案 | 类型安全 | 代码量 | 性能 | 自动化 |
|------|---------|--------|------|--------|
| 手写 Lua C API | ⚠️ 手动 栈操作 | 极多 | 最快 | 无 |
| **Sol2（当前）** | **✓ 编译期检查** | **少** | **接近 C API** | **高** |
| LuaBridge | ✓ | 少 | 稍慢 | 高 |
| tolua++ | ✓ | 代码生成 | 快 | 高 |

**Sol2 如何绑定？** 底层仍是 Lua C API 的栈操作，但 Sol2 用 C++ 模板在编译期自动生成栈压入/弹出代码。你只需写 lambda，Sol2 帮你处理类型转换。

---

## 逐行注释

### 绑定入口函数

```cpp
namespace Rinn {
    inline void bind(sol::state& lua, Registry& reg, ResourceManager& res) {
```

一个函数注册所有绑定。`inline` 因为在头文件中定义。

**设计选择**: 集中式绑定 vs 分散式绑定（每个模块自己注册）。
- **集中式（当前）**: 所有绑定在一个函数中，依赖关系一目了然。
- **分散式**: 每个 System 注册自己的 API。更模块化但需要发现机制。

当前代码量小，集中式够用。

---

### Entity 类型注册

```cpp
lua.new_usertype<Entity>("Entity");
```

> **语法知识 — `new_usertype<T>(name)`**:
>
> 将 C++ 类型 `T` 注册为 Lua 的 userdata 类型。
>
> **底层实现**: Sol2 创建一个 Lua metatable，存储类型信息。当 C++ 返回 `Entity` 给 Lua 时，Sol2 分配一块 `sizeof(Entity)` 的 userdata 内存，拷贝 Entity 到其中，并关联 metatable。
>
> **不透明句柄**: 没有暴露 `id`、`index()`、`generation()` 等字段/方法。Lua 侧只能把 Entity 当作黑盒传递，不能读取或修改内部状态。
>
> 如果要暴露字段:
> ```cpp
> lua.new_usertype<Entity>("Entity",
>     "index", &Entity::index,      // 暴露 index() 方法
>     "id", &Entity::id             // 暴露 id 字段
> );
> ```
>
> **设计选择**: 隐藏内部 → Lua 不能伪造或篡改 Entity → 安全。

---

### set — 统一组件设置

```cpp
lua.set_function("set", [&reg](Entity e, const std::string& name, sol::table data) {
    if (name == "Transform") {
        reg.emplace<Transform>(e,
            data.get<float>("x"),
            data.get<float>("y"));
    }
    else if (name == "Velocity") { ... }
    else if (name == "Sprite") { ... }
    else if (name == "Collider") { ... }
});
```

> **语法知识 — Lambda 捕获详解**:
>
> `[&reg]` 按引用捕获 `reg` 变量。Lambda 内部使用的 `reg` 是外部 `reg` 的引用，不是拷贝。
>
> **生命周期注意**: Lambda 存放在 `lua` 对象中。只要 `lua` 存活，Lambda 就存活，它捕获的引用也要有效。如果 `reg` 先于 `lua` 销毁 → 悬垂引用 → 未定义行为。`main.cpp` 中 `reg` 和 `lua` 同一作用域，destroy 顺序是后构造的先析构 → `lua` 先析构 → 安全。

> **语法知识 — `sol::table`**:
>
> Lua table 在 C++ 侧的代理对象。Lua 的 table 是万能数据结构（兼具数组、哈希表、对象、模块的角色）。
>
> ```cpp
> data.get<float>("x")      // 等价于 Lua: data.x 或 data["x"]
> data["texture_id"]         // 返回 sol::proxy，支持隐式类型转换
> ```
>
> **`get<T>(key)` vs `operator[]`**:
> | 方式 | 行为 | 错误处理 |
> |------|------|---------|
> | `get<float>("x")` | 显式指定返回类型 | 类型错误抛异常 |
> | `data["x"]` | 返回 proxy | 类型转换在使用时发生 |

**设计选择**: 字符串分派（`if name == "Transform"`）。

| 方案 | 优势 | 缺陷 |
|------|------|------|
| **字符串分派（当前）** | 直觉，Lua 侧写法自然 | 拼写错误静默失败，O(n) 比较 |
| 枚举映射 | O(1) 查找 | Lua 侧需写数字或预定义常量 |
| 模板注册 | 类型安全 | Lua 动态类型无法直接映射 |

字符串分派的 O(n) 比较在这里不是瓶颈——`set` 只在实体创建时调用（一次性），不在每帧热循环中。

**缺陷**: 添加新组件类型时必须在此处添加新的 `else if` 分支，容易忘记。改进方案: 用 `unordered_map<string, function>` 做注册表，或使用编译期注册宏。

---

### get_pos / get_vel — 多返回值

```cpp
lua.set_function("get_pos", [&reg](Entity e) -> std::pair<float, float> {
    auto& t = reg.get<Transform>(e);
    return { t.x, t.y };
});
```

> **语法知识 — `-> std::pair<float, float>` 后置返回类型**:
>
> Lambda 的返回类型声明。如果省略，编译器从 `return` 语句推导。显式写出可以避免推导歧义。
>
> **Sol2 的 pair → Lua 多返回值转换**:
> ```
> C++: return std::pair{1.0f, 2.0f}
>  ↓ Sol2 自动转换
> Lua: local x, y = get_pos(entity)  -- x=1.0, y=2.0
> ```
> Sol2 知道如何将 `pair`、`tuple`、多返回值等 C++ 类型转为 Lua 对应形式。

---

### is_key_down — 直接传函数指针

```cpp
lua.set_function("is_key_down", InputSystem::is_key_down);
```

> **语法知识 — 函数指针 vs Lambda**:
>
> `InputSystem::is_key_down` 是一个 `inline` 自由函数（非成员函数），可以直接取地址作为函数指针。Sol2 接受函数指针并自动包装。
>
> **什么时候不能用函数指针？** 成员函数需要 `this` 指针：
> ```cpp
> // ✗ 编译错误: 成员函数不能直接做函数指针
> lua.set_function("foo", &Registry::create_entity);
> // ✓ 用 lambda 包裹并捕获对象
> lua.set_function("foo", [&reg]{ return reg.create_entity(); });
> ```

---

### move — 移动指令

```cpp
lua.set_function("move", [&reg](Entity e, float dx, float dy) {
    auto& v = reg.get<Rinn::Velocity>(e);
    constexpr float speed = 200.0f;
    v.vx = dx * speed;
    v.vy = dy * speed;
});
```

**架构分析**: 
- Lua 只提供方向 (dx, dy ∈ {-1, 0, 1})
- C++ 决定速度大小 (200 像素/秒)
- PhysicSystem 负责将速度累加到位置

这体现了"C++ 做机制（物理参数），Lua 做策略（输入映射）"的分层。

**`constexpr float speed = 200.0f`**: 硬编码。更好的做法是从配置文件或组件数据读取。但对原型阶段足够。

---

### get_collisions — 容器转换

```cpp
lua.set_function("get_collisions", [&reg]() {
    auto hits = CollisionSystem::detect(reg);
    std::vector<std::pair<Entity, Entity>> result;
    result.reserve(hits.size());
    for (auto& h : hits) result.emplace_back(h.a, h.b);
    return sol::as_table(result);
});
```

> **语法知识 — `sol::as_table`**:
>
> 明确告诉 Sol2 将 C++ 容器转为 Lua table。如果不加 `as_table`，Sol2 可能将 `vector` 转为 Lua userdata（不可迭代）。
>
> **转换结果**:
> ```lua
> local collisions = get_collisions()
> -- collisions = { {entity1, entity2}, {entity3, entity4}, ... }
> for _, pair in ipairs(collisions) do
>     print(pair[1], pair[2])
> end
> ```

**`result.reserve(hits.size())`**: 预分配避免 push 过程中多次扩容（每次扩容 = 堆分配 + 元素拷贝）。

---

## 文件级总结

| 设计决策 | 选择 | 理由 |
|---------|------|------|
| 绑定库 | Sol2 | 类型安全 + 自动类型转换 + 接近零开销 |
| Entity 暴露 | 不透明句柄 | 安全：Lua 不能伪造实体 |
| 组件分派 | 字符串比较 | Lua 侧自然，一次性调用不影响性能 |
| 速度参数 | C++ 硬编码 | 物理参数应由"机制层"控制 |
| 碰撞结果 | sol::as_table | 让 Lua 用 ipairs 遍历 |
