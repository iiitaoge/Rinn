# 附录

## 附录 A 核心代码节选

> 以下代码节选用于配合正文阅读，完整源码参见项目仓库 `src/` 与 `assets/shaders/` 目录。

### A.1 Entity 句柄定义（节选自 `src/Core/Types.hpp`）

```cpp
struct Entity {
    uint32_t id = 0;
    static constexpr uint32_t INDEX_MASK       = 0xFFFF;
    static constexpr uint32_t GENERATION_SHIFT = 16;
    static constexpr uint32_t NULL_ID          = 0xFFFFFFFF;

    constexpr Entity() : id(NULL_ID) {}
    constexpr Entity(uint16_t index, uint16_t generation) {
        id = (static_cast<uint32_t>(generation) << GENERATION_SHIFT) | index;
    }
    [[nodiscard]] constexpr uint16_t index() const noexcept {
        return static_cast<uint16_t>(id & INDEX_MASK);
    }
    [[nodiscard]] constexpr uint16_t generation() const noexcept {
        return static_cast<uint16_t>(id >> GENERATION_SHIFT);
    }
    friend auto operator<=>(Entity, Entity) = default;
};
```

### A.2 SparseSet 核心（节选自 `src/Core/SparseSet.hpp`）

```cpp
template<typename T>
class SparseSet : public ISparseSet {
    std::vector<T>      Dense;
    std::vector<Entity> dense_to_entity;
public:
    template<typename... Args>
    requires std::constructible_from<T, Args...>
    [[nodiscard]] T& emplace(Entity entity, Args&&... args) {
        if (Sparse[entity.index()] != NULL_COMPONENT_ENTITY)
            return Dense[Sparse[entity.index()]];
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

    void remove(Entity entity) override {
        if (entity.index() >= MAX_ENTITIES ||
            Sparse[entity.index()] == NULL_COMPONENT_ENTITY) return;
        Entity_index idx_del  = Sparse[entity.index()];
        Entity_index idx_last = static_cast<Entity_index>(Dense.size() - 1);
        if (idx_del == idx_last) {
            Dense.pop_back(); dense_to_entity.pop_back();
            Sparse[entity.index()] = NULL_COMPONENT_ENTITY;
            return;
        }
        Entity entity_last = dense_to_entity[idx_last];
        Dense[idx_del] = std::move(Dense[idx_last]); Dense.pop_back();
        Sparse[entity_last.index()] = idx_del;
        Sparse[entity.index()]      = NULL_COMPONENT_ENTITY;
        dense_to_entity[idx_del] = entity_last; dense_to_entity.pop_back();
    }

    void prefetch(Entity entity) const noexcept {
        _mm_prefetch(reinterpret_cast<const char*>(&Sparse[entity.index()]),
                      _MM_HINT_T0);
    }
};
```

### A.3 View 与 viewIterator（节选自 `src/Core/Registry.hpp`）

```cpp
template<typename... Components>
class View {
    Registry&       reg;
    ISparseSet*     smallest_pool;
    const Entity*   cached_entities;
    size_t          cached_size;
    std::array<ISparseSet*, sizeof...(Components)> other_pools{};
    size_t          other_count = 0;
public:
    View(Registry& r) : reg(r), /* ... */ {
        find_smallest();
        cache_other_pools();
        if (smallest_pool) {
            cached_entities = smallest_pool->entity_data();
            cached_size     = smallest_pool->size();
        }
    }
    auto begin() const { return viewIterator(*this, 0); }
    auto end()   const { return viewIterator(*this, cached_size); }

    struct viewIterator {
        const View& view; size_t index;
        bool is_valid() const {
            Entity c = view.cached_entities[index];
            for (size_t i = 0; i < view.other_count; ++i)
                if (!view.other_pools[i]->has(c)) return false;
            return true;
        }
        viewIterator& operator++() {
            ++index;
            while (index < view.cached_size && !is_valid()) ++index;
            return *this;
        }
        Entity operator*() const { return view.cached_entities[index]; }
    };
};
```

### A.4 HD-2D 片段着色器（`assets/shaders/test.fs` 全文）

```glsl
#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform vec3      facenormal;
uniform sampler2D sprite_normal;
vec3 lightDir = vec3(1, 0, 1);
out vec4 finalColor;
void main() {
    vec4 texel  = texture(texture0,      fragTexCoord);
    vec4 norel  = texture(sprite_normal, fragTexCoord);
    vec3 normal = norel.rgb;
    vec3 N      = normal * 2.0 - 1.0;
    vec3 L      = normalize(lightDir);
    float NdotL = max(dot(N, L), 0.0);
    finalColor  = vec4(texel.rgb * NdotL, texel.a);
}
```

---

## 附录 B 组件清单

**表 B.1 Project Rinn 当前实现的组件清单**

| 组件 | 字段 | 类型约束 | 用途 |
|------|------|----------|------|
| `Transform` | `x, y, layer` | aggregate, trivially copyable | 世界位置与渲染层 |
| `Sprite` | `texture_id, normal_id, width, height, src_x..src_h, is_ground` | 同上 | 精灵渲染 |
| `Velocity` | `vx, vy` | 同上 | 物理积分 |
| `Collider` | `width, height, offset_x, offset_y, layer, mask` | 同上 | 碰撞检测 |
| `TextBubble` | `text[256], display_time` | 同上 | 对话气泡 |
| `IsPlayer` | — | empty | 玩家标签 |
| `IsEnemy` | — | empty | 敌人标签 |
| `IsDead` | — | empty | 死亡标签 |
| `IsStatic` | — | empty | 静态标签 |

> 编译期校验：所有数据组件 `static_assert(std::is_aggregate_v<T> && std::is_trivially_copyable_v<T>)`；所有标签组件 `static_assert(std::is_empty_v<T>)`。

---

## 附录 C Lua 绑定 API 速查

**表 C.1 `LuaBinder::bind` 暴露给 Lua 的全部函数**

| Lua 函数签名 | 返回类型 | 用途 |
|-------------|---------|------|
| `create_entity()` | Entity | 新建实体 |
| `set(e, name, table)` | — | 挂载组件，按 name 字符串分发 |
| `remove(e, name)` | — | 移除组件 |
| `get_pos(e)` | (x, y) | 读取 Transform |
| `get_vel(e)` | (vx, vy) | 读取 Velocity |
| `move(e, dx, dy)` | — | 设置 Velocity（含速度常数 200） |
| `is_key_down(key)` | bool | 键盘查询（int 键码） |
| `get_collisions()` | table[Entity, Entity] | 当前帧碰撞对 |
| `set_camera_target(x, y)` | — | 相机焦点跟随 |
| `load_texture(path)` | uint16 | 加载并返回纹理 ID |
| `play_bgm(path)` | — | 播放背景音乐 |

**`set` 当前支持的组件名**：`"Transform"`、`"Velocity"`、`"Sprite"`、`"Collider"`、`"TextBubble"`。

**`remove` 当前支持的组件名**：`"TextBubble"`、`"Collider"`、`"Sprite"`。

---

## 附录 D 性能测试原始数据

> 本附录留作填写实测数据。建议读者在目标硬件上按下列命令分别编译运行三个基准，将得到的输出粘贴于此。

### D.1 Cache Benchmark

编译与运行：

```bash
cl /O2 /EHsc /std:c++20 tests\cache_benchmark.cpp /Fe:cache_bench.exe
.\cache_bench.exe > cache_bench_output.txt
```

实测输出：

```
（在此粘贴 cache_bench.exe 的实际输出）
```

### D.2 False Sharing Benchmark

```bash
cl /O2 /EHsc /std:c++20 tests\false_sharing_bench.cpp /Fe:fs_bench.exe
.\fs_bench.exe > fs_bench_output.txt
```

实测输出：

```
（在此粘贴 fs_bench.exe 的实际输出）
```

### D.3 ECS 单元测试

```bash
cmake -DBUILD_TESTS=ON -B build
cmake --build build --target ecs_tests
ctest --test-dir build --output-on-failure
```

实测输出：

```
（在此粘贴 ctest 的实际输出，应显示 [  PASSED  ] X tests）
```

---

## 附录 E 缩略语表

| 缩写 | 全称 | 中文 |
|------|------|------|
| ECS | Entity-Component-System | 实体-组件-系统 |
| OOP | Object-Oriented Programming | 面向对象编程 |
| DOD | Data-Oriented Design | 面向数据设计 |
| AoS | Array of Structures | 结构体数组 |
| SoA | Structure of Arrays | 数组结构体 |
| AABB | Axis-Aligned Bounding Box | 轴对齐包围盒 |
| MTV | Minimum Translation Vector | 最小穿透向量 |
| MVP | Model-View-Projection | 模型-视图-投影矩阵 |
| API | Application Programming Interface | 应用程序接口 |
| GUI | Graphical User Interface | 图形用户界面 |
| CJK | Chinese, Japanese, Korean | 中日韩 |
| GPU | Graphics Processing Unit | 图形处理单元 |
| CPU | Central Processing Unit | 中央处理器 |
| TU | Translation Unit | 翻译单元 |
| RAII | Resource Acquisition Is Initialization | 资源获取即初始化 |
| TTL | Time To Live | 生存时间 |
| FPS | Frames Per Second | 每秒帧数 |
| HD-2D | High-Definition 2D | 高清 2D |
| GOAP | Goal-Oriented Action Planning | 目标导向行动规划 |
| LLM | Large Language Model | 大语言模型 |
| GID | Global Identifier | 全局标识符（Tiled 中使用） |
| UV | U-V coordinate | 纹理坐标 |
| BGM | Background Music | 背景音乐 |
| UB | Undefined Behavior | 未定义行为 |
