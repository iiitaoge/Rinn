# Project_Rinn 项目审查报告

> 审查日期：2026-04-26
> 审查范围：`src/`、`scripts/`、`tests/`、`assets/shaders/`、`CMakeLists.txt`
> 审查模式：学习模式 — 指出问题、解释根因、提示思路，不给完整修复代码

每个条目按 **影响等级** 标记：
- 🔴 **CRITICAL** — 隐藏 bug / UB / 工业不可接受
- 🟠 **HIGH** — 明显性能或结构隐患，建议优先修
- 🟡 **MEDIUM** — 应当修但不紧急
- 🔵 **LOW** — 小瑕疵 / 风格问题

---

## 一、性能问题 (Performance)

### P1 🔴 `AudioSystem.hpp` 头文件中的 `static` 全局造成跨 TU 状态分裂
**位置**：`src/Systems/AudioSystem.hpp:9-10`

```cpp
namespace Rinn::AudioSystem {
    static Music current_bgm = { 0 };   // ← 内部链接！
    static bool is_bgm_loaded = false;
    inline void Init() { InitAudioDevice(); }
    inline void PlayBGM(...) { ... }
}
```

`static` 在命名空间作用域 = **internal linkage**。每个 `#include` 这个头的 .cpp 都会得到自己独立的 `current_bgm`。

- `main.cpp` 调用 `AudioSystem::Update()`、`Shutdown()`
- `LuaBinder.hpp`（被 `main.cpp` 包含，但展开的 lambda 在 `bind` 函数体内）调用 `AudioSystem::PlayBGM`

虽然当前 `LuaBinder.hpp` 是 inline header 而 main.cpp 是唯一翻译单元，所以**目前**这个 bug 没有显现。但只要将来再多一个 .cpp 包含 `AudioSystem.hpp`，BGM 就会"播了但听不到"。

**思考方向**：
- `inline` vs `static` 对头文件中的全局变量分别意味着什么？
- 你打算用单例 / 服务定位器 / 显式上下文，哪种最贴合你 ECS 数据驱动的取向？

---

### P2 🟠 `CollisionSystem::detect` 每帧重建 `std::unordered_map` 与 `std::vector`
**位置**：`src/Systems/CollisionSystem.hpp:22, 60-63`

```cpp
inline std::unordered_map<uint64_t, std::vector<Entity>> grid;
// ...
grid.clear();   // bucket 保留，但每个 vector 内部仍发生析构
for (Entity e : reg.view<Transform, Collider>())
    insert_to_grid(...);   // operator[] 触发 rehash + push_back 触发堆扩容
```

每帧成本：
1. `unordered_map::clear` = O(buckets)，触摸所有 bucket。
2. `grid[key]` = 哈希查找 + 可能 rehash。
3. `vector::push_back` = 多次 malloc。
4. 16K 实体下，这是稳定的 ms 级开销。

**思考方向**：
- 把 `unordered_map<uint64_t, vector<Entity>>` 替换为什么结构能复用内存？
- 如果地图尺寸已知，是否可以用 2D 数组（`grid[y * W + x]`）取代哈希？
- `vector<Entity>` 用 `std::vector<vector<Entity>>` 之外，"扁平 bucket + offset" 是经典 ECS 实践，思考一下怎么做。

---

### P3 🟠 `RenderSystem::DrawSprites` 每帧两个 `std::vector` 的堆分配 + lambda 捕获
**位置**：`src/Systems/RenderSystem.hpp:168-207`

```cpp
std::vector<Entity> ground_tiles;   // 每帧分配
std::vector<Entity> sprites;        // 每帧分配
for (Entity e : reg.view<Transform, Sprite>()) { ... push_back ... }

std::sort(sprites.begin(), sprites.end(),
    [&reg](Entity a, Entity b) {
        const auto& ta = reg.get<Transform>(a);  // 4 次 sparse→dense 跳转 / 比较
        const auto& tb = reg.get<Transform>(b);
        const auto& sa = reg.get<Sprite>(a);
        const auto& sb = reg.get<Sprite>(b);
        ...
    });
```

成本：
- 1000 sprites × `O(log N)` 比较 × 4 次 pointer chase ≈ **40K 次 cache miss / 帧**。
- 每帧两次 `vector` 堆分配（即使 reserve 也会析构）。

**思考方向**：
- `std::sort` 比较函数应当只读"排序键"，怎么把 `(layer, y_bottom)` 预先打包成 `uint64_t`，先做"投影排序"？
- 两个 `vector` 是否应该提升为 System 内的成员，跨帧复用？

---

### P4 🟠 `Facenormal()` 每帧两次 `GetShaderLocation` 字符串查找
**位置**：`src/Systems/RenderSystem.hpp:90-94, 134-136`

```cpp
inline void Facenormal() {
    int loc = GetShaderLocation(test_shader, "facenormal");  // 字符串查表！
    SetShaderValue(...);
}
// DrawSprite3D 里又一次：
int loc = GetShaderLocation(test_shader, "sprite_normal");
SetShaderValueTexture(...);
```

shader uniform 的 location 是 GL state，**只需在加载 shader 时查一次**。

**思考方向**：
- shader 加载后，把 location 缓存到哪里？属于 `RenderSystem` 还是某个 `Material` 结构体？

---

### P5 🟠 `view::operator++` 每步对每个非主池调用 `has()`，但 `View` 没记录"主池是哪个组件"
**位置**：`src/Core/Registry.hpp:331-340`

```cpp
bool is_valid() const {
    Entity candidate = view.cached_entities[index];
    for (size_t i = 0; i < view.other_count; ++i) {
        if (!view.other_pools[i]->has(candidate)) return false;  // 每步 1 次 sparse 查
    }
    return true;
}
```

`has()` 不是虚函数，OK。但循环中每个候选实体都做 `other_count` 次 sparse 检查；同时 System 通常紧接一句 `reg.get<Sprite>(e)` 又走一次同路径。两次 sparse[idx] 没有共享。

**思考方向**：
- 能否让 iterator 在 `is_valid` 通过后顺手把每个组件的 dense 指针缓存出来？这样 `*it` 直接返回 tuple<T&, S&...> 而非 Entity。这是 EnTT `view::each` 的核心思路。
- `View::other_pools` 已经是 `ISparseSet*`，无法 `.get<T>()` — 设计上是不是需要 `TypedView`？

---

### P6 🟡 `LuaBinder::set` 字符串分发开销
**位置**：`src/Scripting/LuaBinder.hpp:31-75`

```cpp
lua.set_function("set", [&reg](Entity e, const std::string& name, sol::table data) {
    if (name == "Transform") { ... }
    else if (name == "Velocity") { ... }
    else if (name == "Sprite") { ... }
    ...
});
```

每个 Lua → C++ 的组件挂载都要：
1. `std::string` 构造（Lua 的 const char* 拷贝）。
2. 4-7 次字符串比较。
3. 多次 `data.get<T>("key")` — 每次都是字符串哈希 + table lookup。

scene 加载时（一次）影响小；每帧 `set(npc.id, "TextBubble", ...)` 这种就放大到帧预算。

**思考方向**：
- Sol2 支持 `lua.new_usertype<Transform>("Transform", ...)`，能把组件直接以类型化方式暴露给 Lua。这种方案下，Lua 写 `e:add(Transform.new{x=...})` 的开销是多少？
- 或者用 enum / int component_id 代替字符串，编译期映射到模板特化。

---

### P7 🟡 `chinese_font` 一次性加载全部 CJK 统一汉字
**位置**：`src/Systems/RenderSystem.hpp:36-43`

```cpp
for (int i = 0x4E00; i <= 0x9FFF; i++) codepoints.push_back(i);   // 20,991 字
```

启动时 `LoadFontEx` 会渲染 ~21K 个 32px glyph 到 atlas，可能是数十 MB 的 GPU 内存。绝大多数游戏只用其中几百字。

**思考方向**：
- 只加载实际需要的字（`dialogue_data.lua` 的全部正文 + UI 文本）。如何在运行时收集集合？
- 或者用 SDF 字体 + 动态 glyph 上传。

---

### P8 🟡 `View` 的"惰性创建组件池"会让 `view<NotYetUsed>()` 产生副作用
**位置**：`src/Core/Registry.hpp:131-137, 367-389`

```cpp
template<typename T>
[[nodiscard]] SparseSet<T>& get_pool() {
    if (Components_Pool[id] == nullptr) {
        Components_Pool[id] = std::make_unique<SparseSet<T>>();  // 写入！
    }
    ...
}
```

`View::find_smallest()` 调用 `reg.get_pool<Components>()` —— 即使读取也会创建。`View<NeverEmplacedComponent>()` 会分配 32KB 的 sparse 数组。

**思考方向**：
- "构造一个空 View 不应改变世界"，怎么实现？
- 可以引入 `try_get_pool` 返回 `SparseSet<T>*`（nullable），View 内特判 size = 0。

---

### P9 🟡 `Boids.lua` 的 `get_neighbors` 是 O(N²) 全实体遍历
**位置**：`scripts/Boids.lua:2-15`

虽然当前 `main.lua` 没用到，但留作对照。如果接入，N=200 时帧成本约 40K 次 Lua-C++ 跳转。

**思考方向**：
- 你已经写了空间哈希 (`CollisionSystem`)，能不能把它通用化成 `SpatialHash` 服务，给 Boids 复用？

---

## 二、架构缺陷 (Architecture)

### A1 🔴 命名空间级 `inline` / `static` 全局组合形成隐式单例
**位置**：`RenderSystem.hpp:13-15, 86-88`、`AudioSystem.hpp:9-10`、`CollisionSystem.hpp:22`

```cpp
namespace RenderSystem {
    inline Font chinese_font = {};
    inline Camera3D camera = { 0 };
    inline Shader test_shader = { 0 };
    Vector3 vertical_normal = ...;   // 没有 inline！
    Vector3 tile_normal = ...;
    Vector3 facenormal = ...;
}
```

问题：
1. `vertical_normal/tile_normal/facenormal` **没有 `inline`** —— 一旦头文件被两个 .cpp 包含就会 **链接错误**。当前侥幸只一个 .cpp 用到。
2. 整个 `RenderSystem` 是命名空间 + 全局变量，无法持有第二个相机、第二个窗口。
3. `CollisionSystem::grid` 同问题，无法多线程 / 多场景。

**思考方向**：
- ECS 项目一般用 `Resource` / `Context` 概念存储全局状态（Bevy / EnTT 都有）。你打算把 `Camera`、`Shader`、`Font` 视作 ECS 资源（单例组件）吗？
- 一个简单的过渡：把 `RenderSystem` 改写为 `class RenderContext { Camera camera; Shader shader; ... };`，然后 System 函数都接收 `RenderContext&`。

---

### A2 🟠 `ComponentCounter::counter` 是进程级共享，多 Registry / 测试套件会泄漏 ID
**位置**：`src/Core/ComponentID.hpp:8-21`

```cpp
struct ComponentCounter {
    inline static std::atomic<Component_ID> counter{ 0 };
};
template <typename T>
Component_ID get_component_type_id() {
    static Component_ID id = ComponentCounter::counter.fetch_add(1, ...);
    return id;
}
```

- 跨多个 `Registry` 实例，所有组件类型共享同一个 ID 空间 → 测试中创建多个 Registry 还能共用同一组组件 ID 是巧合。
- 不可重置；`Registry::clear` 不会重置 ID 计数器。
- 64 个组件上限，库代码 / 引擎代码 / 游戏代码混用时容易耗光。

**思考方向**：
- EnTT 用 `entt::type_seq<T>::value()` 也是进程级，但每个 Registry 自己维护一张"ID → pool"映射表。问题就转移到"如何在 64 个槽位里只分配实际用到的"。
- 如果想要 per-Registry ID，用 `std::type_index` 做 key，运行时分配。代价是查 pool 多一次哈希。权衡？

---

### A3 🟠 `bind()` 巨型 string-switch 是**非可扩展**的 Lua 接口
**位置**：`src/Scripting/LuaBinder.hpp:31-88`

每加一个组件，都要在 `set` 和 `remove` 里各加一个 `else if`。同时：
- 这两个分支必须保持同步。
- 字段名硬编码，重命名 C++ 的 `Sprite::src_x` 不会让 Lua 编译失败，只会运行时静默错误。

**思考方向**：
- 思考 EnTT 的 meta system 或者 sol2 的 `new_usertype` —— 能否一次性"反射"出全部字段？
- 数据驱动注册：用 `.lua` 文件声明组件 schema，C++ 启动时读取并生成绑定。比手写 `else if` 长远但代价低。

---

### A4 🟠 `SparseSet::emplace` 重复时**返回旧值**而非替换 — 静默偏离用户意图
**位置**：`src/Core/SparseSet.hpp:58-60`

```cpp
if (Sparse[entity.index()] != NULL_COMPONENT_ENTITY) {
    return Dense[Sparse[entity.index()]];   // 静默返回旧的
}
```

`bind()` 处理 `TextBubble` 时不得不写：

```cpp
if (reg.has<Rinn::TextBubble>(e)) reg.remove<Rinn::TextBubble>(e);
auto& tb = reg.emplace<Rinn::TextBubble>(e);
```

这违反"一行 emplace 应当替换"的直觉。

**思考方向**：
- 你想要 `emplace` (新建，已存在则 assert)、`replace` (覆盖)、`emplace_or_replace` 这三档。哪种命名最不容易踩坑？
- EnTT 选择：`emplace` 严格新建，`emplace_or_replace` 显式语义。

---

### A5 🟠 `Registry::get` 缺少 `const` 重载 — `const Registry&` 用不了
**位置**：`src/Core/Registry.hpp:186-202`

```cpp
template<typename T> [[nodiscard]] T& get(Entity entity) { ... }       // 只有非 const
template<typename T> [[nodiscard]] auto try_get(Entity entity) noexcept { ... }
```

`get_pool<T>()` 也没有 const 版本。结果：DebugUI / 序列化 / 任何只读 System 都无法接 `const Registry&`，破坏 const 正确性。

**思考方向**：
- 加 const 版本时，`get_pool` 的"延迟创建"语义如何处理？(const 接口看到不存在的池怎么办 → 返回 `const SparseSet*` 可空)

---

### A6 🟡 `ScriptContext.hpp` 名不副实
7 行内只调一个 `lua.open_libraries`，且与 `bind` 中的 `open_libraries` **重复且不一致**：

```cpp
// ScriptContext.hpp
lua.open_libraries(sol::lib::base, sol::lib::math);

// LuaBinder.hpp::bind
lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::math, sol::lib::table);
```

两次 open 是允许的，但责任划分不清。

**思考方向**：
- "ScriptContext" 的含义是什么？是不是应该承担 Lua state 生命周期 / 错误处理 / 沙盒？现在它只是一个空壳。

---

### A7 🟡 `Sprite::texture_id` 是 `size_t`，`ResourceManager::load_texture` 返回 `uint16_t`
**位置**：`Components.hpp:17` vs `ResourceManager.hpp:12`

类型不一致，靠隐式转换硬接。如果改了一边没改另一边，编译期不会报错但语义错位（比如 ID 放进了高位）。

**思考方向**：
- 用 `using TextureHandle = uint16_t;` 锁住类型契约。

---

### A8 🟡 资源路径硬编码 `"../../../assets/..."`
**位置**：`main.cpp:29`、`scripts/main.lua:2`、`map_loader.lua:1`、`RenderSystem.hpp:42, 48`

工作目录变了就崩。MSVC IDE 的默认 cwd 是 `build/Debug`，CLI 的是项目根，发布版的是 exe 同目录。三套路径不可能同时正确。

**思考方向**：
- 用 `std::filesystem` 在启动时做"找根目录"。或者在打包时把 assets 复制到 exe 旁边，路径恒为 `assets/...`。
- CMakeLists 应当 `add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD COMMAND copy assets ...)`。

---

### A9 🟡 Lua 错误恢复 = 没有
**位置**：`main.cpp:30-34`

```cpp
auto result = lua.script_file("../../../scripts/main.lua");
if (!result.valid()) {
    sol::error err = result;
    std::cerr << "加载脚本失败: " << err.what() << std::endl;
}
// 然后继续往下跑！
while (!RenderSystem::ShouldClose()) {
    lua["on_update"]();   // ← Lua state 已损坏，但还是调
```

`on_update` 失败时也没有 try-catch。`sol::protected_function` 是 sol2 的标准答案。

**思考方向**：
- "策略可崩，机制不能崩"是脚本系统的底线。如果 Lua 错了，C++ 应该怎么"软降级"——继续渲染但暂停 update？显示错误覆盖层？

---

### A10 🟡 `PhysicSystem` 名字写错（应为 `PhysicsSystem`）
**位置**：`src/Systems/PhysicSystem.hpp`

英语 `Physics` 是不可数名词加 -s 形式，单数也是 `physics`。`PhysicSystem` 不是合法英文。改名小但显眼。

---

### A11 🔵 空目录 `src/Game/` 与 `Scripting/ScriptContext.hpp` 残留
死结构会随时间产生"是不是我没看全？"的认知噪音。

---

## 三、代码规范不一致 (Code Style)

### S1 🟠 命名风格混乱
| 实体 | 风格 |
|------|------|
| `Entity`, `Sprite`, `Registry`, `EntityPool`, `View` | PascalCase ✓ |
| `viewIterator` | **camelCase** ✗（应 PascalCase） |
| `Components_Pool`, `entity_pool` | **混用 PascalCase 和 snake_case** |
| `create_entity`, `is_alive`, `get_pool` | snake_case |
| `Init_lua` | **混合**（应 `init_lua`） |
| `bind` | snake/小写 |
| `RenderSystem::Init`, `BeginFrame`, `DrawText` | PascalCase（Raylib 风格） |
| `RenderSystem::vertical_normal`, `tile_normal` | snake_case |

**建议**：选一种（snake_case 或 PascalCase）贯穿全项目。Google C++ Style 是 PascalCase 类 + snake_case 函数；STL 是全 snake_case。无对错，但不能同一文件内混用。

---

### S2 🟠 `RenderSystem.hpp` 中 3 个 `Vector3` 全局缺 `inline`
**位置**：`src/Systems/RenderSystem.hpp:86-88`

```cpp
Vector3 vertical_normal = { 0.0f, 0.0f, 1.0f };  // 没 inline，多 TU 包含时链接错误
Vector3 tile_normal = { 0.0f, 1.0f, 0.0f };
Vector3 facenormal = { 0 };
```

参考 P1，与 `inline Camera3D camera` 并列摆放但风格不一致 —— 极易在重构时被忽视。

---

### S3 🟡 `#include "iostream"` (用引号包含标准库)
**位置**：`src/Core/Registry.hpp:5`

引号优先在用户路径里查 → 标准库要用尖括号 `<iostream>`。MSVC/Clang 都不报错只是因为后备到了系统路径，但风格上是错的。同时 Registry.hpp 用到 `iostream` 仅为打印 —— 是不是应当移除？

---

### S4 🟡 `std::is_aggregate_v<Sprite>` 的 `Sprite` 实际上**不是聚合类型**
**位置**：`src/Components/Components.hpp:67`

```cpp
struct Sprite {
    size_t texture_id;
    size_t normal_id = 0;     // ← 默认成员初始化在 C++17 之后仍是聚合
    float width, height;
    ...
};
static_assert(std::is_aggregate_v<Sprite>, ...);  // ✓ 通过
```

OK，C++14 之后默认成员初始化也属聚合。但 `Sprite` 字段顺序在 `bind()` 里用聚合初始化时（间接通过 emplace 的 forward）必须严格匹配。任何字段重排会**静默错位**。

**思考方向**：
- 用 designated initializer `{.texture_id = ..., .width = ...}` 可以避免按顺序传参的脆弱性。

---

### S5 🟡 `[[nodiscard]]` 滥用
**位置**：`Registry.hpp:160, 175`

```cpp
[[nodiscard]] Entity create_entity() noexcept;
[[nodiscard]] T& emplace(...);   // ← emplace 返回引用，调用方常常忽略
```

`emplace<Component>(e, ...)` 在大量 setup 代码里都不需要返回值。`bind()` 里全是 `reg.emplace<...>(e, ...)` 没有 `auto&` 接收 → 触发 `[[nodiscard]]` 警告（除非 MSVC 没启用 W4 对此告警）。tests 里 `(void)reg.emplace<...>` 是被迫的 boilerplate。

**思考方向**：
- `[[nodiscard]]` 适合"忽略返回值就是 bug"的场景（如 `try_get` / `acquire`）。`emplace` 通常不属于。

---

### S6 🟡 `strncpy` + 手动 size 容易踩雷
**位置**：`src/Scripting/LuaBinder.hpp:72`

```cpp
strncpy(tb.text, t.c_str(), 255);
```

虽然 `tb.text[256]` 零初始化，所以 255 字节后必有 `\0`。但：
1. 如果某天有人把 buffer 改小，越界。
2. MSVC 会警告 `strncpy unsafe`，需要 `_CRT_SECURE_NO_WARNINGS`。

**思考方向**：用 `snprintf(tb.text, sizeof(tb.text), "%s", t.c_str())` 一行搞定且总是 null-terminate。

---

### S7 🔵 中英文注释混用、Markdown 装饰符号嵌入
代码中的 `// ⭐ 新增：...`、`// 🔥 关键点：...`、`// ====` 风格不一。emoji 注释在 IDE / git diff 里偶尔显示异常，且 `permissive-` 严格模式下编码问题更敏感。

---

### S8 🔵 `MeasureTextEx(...).x` 隐式 float→int
**位置**：`src/Systems/RenderSystem.hpp:120`

```cpp
int text_width = MeasureTextEx(chinese_font, tb.text, 24, 2).x;  // float → int
```

W4 会告警。

---

### S9 🔵 头文件顺序不规范
`bind()` 里大量 `#include "../Systems/X.hpp"`，但同一项目内有的用 `"Core/Registry.hpp"`，有的用 `"../Components/Components.hpp"`。CMakeLists 已经把 `src/` 加入 include path，应该统一用 `"Core/..."` 之类绝对相对路径。

---

### S10 🔵 `.fs` shader 中 `lightDir` 写死且非 `uniform`
**位置**：`assets/shaders/test.fs:11`

```cpp
vec3 lightDir = vec3(1, 0, 1);  // 硬编码，不能从 C++ 调
```

应当声明成 `uniform vec3 lightDir`，让游戏侧/编辑器可调。

---

## 四、其他观察

### 优点（继续保持）
1. ECS 核心 (`SparseSet` / `Registry`) 单元测试覆盖**扎实**，含边界、复用、版本号、空 view、size determinism。
2. `Entity` handle 的位布局 + 版本号是工业标准做法，写法正确。
3. `EntityPool` 用 ring buffer FIFO 复用 + 2 的幂掩码，设计干净。
4. `View` 选择最小池作为驱动池，是 EnTT 同款优化思路。
5. `static_assert(is_aggregate / is_trivially_copyable)` 给组件设置编译期护栏，意识到位。
6. `cache_benchmark.cpp` 自己测 cache miss 数据，工程素养在线。

### `progress.md` / 文档清空
当前 git status 显示 `docs/` 几乎全部被删除。如果是有意重构，建议保留：
- 至少一份"当前架构 1 页纸图"（供陌生人 30 秒入门）。
- 一份 ADR 列表（记录"为什么 emplace 不替换"等不可逆决策）。

---

## 五、修复优先级建议

| 优先级 | 问题 | 估时 |
|--------|------|------|
| **P0** | A1（`Vector3` 缺 inline）+ P1（AudioSystem static） | 30 min — 防止隐藏 link/UB |
| **P0** | A8（资源路径） | 1 h — 影响发布 |
| **P1** | P4（cache shader location） | 30 min |
| **P1** | A9（Lua protected_function） | 1 h — 健壮性 |
| **P1** | P2（碰撞网格内存复用） | 2 h |
| **P2** | A4（emplace 语义）+ A5（const 重载） | 1 h |
| **P2** | A3（Lua 绑定可扩展性） | 4 h+ — 大重构 |
| **P3** | S1（命名规范统一） | 1 h（机械 rename） |

---

## 六、给作者的几个反思问题

1. 当 `RenderSystem` 不再是命名空间而是类时，谁持有这个类？`Registry` 吗？还是 `Engine` 这一层？这条线决定了 ECS 之上的"世界对象"长什么样。
2. `bind()` 里的字符串 switch 是工程债务。在你"做减法"的设计哲学里，怎样的接口最接近"零 boilerplate 添加新组件"？
3. `View::find_smallest` 副作用创建池 —— 你 OK 这种隐式行为吗？工业 ECS 通常**显式注册**所有组件类型。代价是失去模板"任意 T 都能用"的优雅。
4. 当前每帧都同时跑 C++ 物理 + C++ 碰撞 + Lua on_update，三者的执行顺序是写死在 `main.cpp`。你设想的 "System schedule" 长什么样？
5. 测试覆盖了 ECS 但没有覆盖 `CollisionSystem` / `RenderSystem` / `LuaBinder`。后两者依赖 raylib 难以单测，但 `CollisionSystem` 是纯逻辑，可以单测。下一步要写吗？

---

报告完毕。建议从 **P0** 两条开始修，它们是潜伏的链接 / UB 风险，每帧调用都在掷骰子。
