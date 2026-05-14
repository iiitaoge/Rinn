# 附录

## 附录 A 核心代码节选

> 以下代码与项目源码保持逐字同步，完整源码参见项目仓库 `src/` 与 `assets/shaders/` 目录。

### A.1 Entity 句柄定义（`src/Core/Types.hpp` 全文）

```cpp
#pragma once
#include <cstdint>
#include <limits>
#include <bitset> // <--- 必须加这个
#include <algorithm>	// 实现 fill
#include <array>		// 为了内存连续
#include <vector>
#include <cassert>
#include <utility>  // 包含 std::forward
#include <concepts> // 确保构造的时候参数合法，能够造出 T
#include <optional>		// 为了实现 “空返回”
#include <bit>			// 为了实现快速  实体销毁组件

namespace Rinn {
    // 1. 定义实体 ID
// -------------------------------------------------------------------------
    // 实体句柄 (Entity Handle) - 32位值类型
    // -------------------------------------------------------------------------
    // 布局：[ Generation (16 bits) | Index (16 bits) ]
    // -------------------------------------------------------------------------
    struct Entity {
        // 唯一的成员变量：32位无符号整数
        uint32_t id = 0;

        // 掩码常量 (Compile-time constants)
        static constexpr uint32_t INDEX_MASK = 0xFFFF;
        static constexpr uint32_t GENERATION_SHIFT = 16;
        static constexpr uint32_t NULL_ID = 0xFFFFFFFF;

        // 默认构造：创建一个无效实体
        constexpr Entity() : id(NULL_ID) {}

        // 内部构造：由 Registry 使用
        constexpr Entity(uint16_t index, uint16_t generation) {
            id = (static_cast<uint32_t>(generation) << GENERATION_SHIFT) | index;
        }

        // 1. 获取索引 (用于数组寻址) -> O(1) 位运算
        // 如果可能，请在编译期算，运行期算也可以
        [[nodiscard]] constexpr uint16_t index() const noexcept {
            return static_cast<uint16_t>(id & INDEX_MASK);
        }

        // 2. 获取版本 (用于生存检查) -> O(1) 位运算
        [[nodiscard]] constexpr uint16_t generation() const noexcept {
            return static_cast<uint16_t>(id >> GENERATION_SHIFT);
        }

        // 3. 检查是否为 Null
        [[nodiscard]] constexpr bool is_null() const noexcept {
            return id == NULL_ID;
        }

        // 4. 支持 C++20 默认比较 (==, !=)
        // 虽然使用场景不多，常用的是index和generation，但未来可能用上
        // 传值引用 比指针 引用 更快
        friend auto operator<=>(Entity, Entity) = default;

        // 5. 支持作为 Map 的 Key (如果是 std::map)
        // 但我们在 ECS 里通常不用 map，而是用 sparse set
    };

    // 为了支持 std::unordered_map (如果有必要的话，尽管不推荐)
    // 还需要特化 std::hash，但暂时不需要写


    // 2. 定义实体索引
    using Entity_index = std::uint16_t;
    using Entity_generation = std::uint16_t;


    // 2. 无效实体组件号 (Modern C++ 写法，检查Sparse数组中该实体有无对应组件)
    // max() 通常是 0xFFFFFFFF
    constexpr Entity_index NULL_COMPONENT_ENTITY = std::numeric_limits<Entity_index>::max();

    // 3. 组件 ID 类型
    using Component_ID = std::uint8_t; // 64个组件用 uint8 就够了(0-255)，省内存

    // 4. 数量限制（L1 cache）
    constexpr Entity_index MAX_ENTITIES = 16384;
    constexpr Component_ID MAX_COMPONENTS = 64;

    // 5. 签名 (Signature)
    // std::bitset<64> 占用 8 字节，非常紧凑
    using Signature = std::bitset<MAX_COMPONENTS>;
}
```

### A.2 SparseSet 核心（`src/Core/SparseSet.hpp` 全文）

```cpp
#pragma once
#include"Types.hpp"
#ifdef _MSC_VER
#include <intrin.h>     // MSVC: _mm_prefetch
#else
#include <immintrin.h>
#endif

namespace Rinn {
	class ISparseSet {
	public:
		virtual ~ISparseSet() = default;

		ISparseSet() {
			Sparse.fill(NULL_COMPONENT_ENTITY);
		}

		// 检查该实体是否有对应组件
		bool has(Entity entity) const noexcept {
			assert(!entity.is_null() && "Entity invalid");
			return entity.index() < MAX_ENTITIES && Sparse[entity.index()] != NULL_COMPONENT_ENTITY;
		}
		virtual void remove(Entity entity) = 0;
		virtual void clear() = 0;
		virtual size_t size() const noexcept = 0;
		
		// ⭐ 新增：暴露底层实体数组指针，View 构造时缓存，消除遍历中的虚函数调用
		virtual const Entity* entity_data() const noexcept = 0;

	protected:
		std::array<Entity_index, MAX_ENTITIES> Sparse;
	};

	// 具体组件类实现
	template<typename T>
	class SparseSet : public ISparseSet {
	private:
		std::vector<T> Dense;
		std::vector<Entity> dense_to_entity;	// 组件对应实体（完整 handle），用于 dense 反向定位 sparse 及 View 遍历
	public:

		// 兼容性：暴露迭代器类型，允许 std::sort 等算法工作
		using iterator = typename std::vector<T>::iterator;
		using const_iterator = typename std::vector<T>::const_iterator;
		using value_type = T;

		using ISparseSet::ISparseSet;
		// 给实体挂组件--原地构造
		template<typename... Args>
		// 核心约束在这里：
		requires std::constructible_from<T, Args...>
		[[nodiscard]] T& emplace(Entity entity, Args&&... args) {

			assert(entity.index() < MAX_ENTITIES && "Entity out of range!");

			// 如果组件已存在，返回现有引用（不替换）
			// 组件更新应通过 get() 获取引用后直接修改
			if (Sparse[entity.index()] != NULL_COMPONENT_ENTITY) {
				return Dense[Sparse[entity.index()]];
			}
			

			// 安全：异常安全 
			// 先确保 capacity 足够，防止 push_back 过程中抛出异常导致数据不一致
			// 注意：这是一种简化的异常安全处理，更严格的需要 try-catch 回滚
			// 一起扩容，一起成功
			if (Dense.size() == Dense.capacity()) {
				size_t new_cap = std::max<size_t>(Dense.capacity() * 2, 8);
				Dense.reserve(new_cap);
				dense_to_entity.reserve(new_cap);
			}


			// 原地构造
			Dense.emplace_back(std::forward<Args>(args)...);	
			

			//  同步稀疏集映射
			dense_to_entity.push_back(entity);
			Sparse[entity.index()] = static_cast<Entity_index>(Dense.size() - 1);

			return Dense.back();
		}

		[[nodiscard]] T& get(Entity entity) {
			assert(has(entity) && "Entity does not have this component!");
			return Dense[Sparse[entity.index()]];
		}
		// 只读get
		[[nodiscard]] const T& get(Entity entity) const {
			assert(has(entity) && "Entity does not have this component!");
			return Dense[Sparse[entity.index()]];
		}

		// dense_to_entity和Dense必须保持一致性：一致写，一致删
		void remove(Entity entity) override{
			if (entity.index() >= MAX_ENTITIES || Sparse[entity.index()] == NULL_COMPONENT_ENTITY) {
				return; // 或者 assert(false);
			}
			Entity_index index_deleted = Sparse[entity.index()];		// 被删除实体在Dense中的索引
			Entity_index index_last = static_cast<Entity_index>(Dense.size() - 1);		// 队尾索引


			// 如果删除的就是最后一个，直接 pop
			if (index_deleted == index_last) {
				Dense.pop_back();
				dense_to_entity.pop_back();
				Sparse[entity.index()] = NULL_COMPONENT_ENTITY;
				return;
			}



			Entity entity_last = dense_to_entity[index_last];	// 队尾实体

			// 维护稠密数组 Dense
			Dense[index_deleted] = std::move(Dense[index_last]);
			Dense.pop_back();

			// 维护稀疏数组 Sparse
			Sparse[entity_last.index()] = index_deleted;
			Sparse[entity.index()] = NULL_COMPONENT_ENTITY;

			// 维护dense_to_entity
			dense_to_entity[index_deleted] = entity_last;
			dense_to_entity.pop_back();
		}

		// 重置 
		void clear() override {
			for (Entity e : dense_to_entity) {		// 从 O(Capacity) 降维到了 O(Size)
				Sparse[e.index()] = NULL_COMPONENT_ENTITY;
			}
			Dense.clear();
			dense_to_entity.clear();
		}

		// 组件数
		[[nodiscard]] size_t size() const noexcept override {
			return Dense.size();
		}


		// ⭐ 新增：返回实体数组指针，供 View 缓存使用
		[[nodiscard]] const Entity* entity_data() const noexcept override {
			return dense_to_entity.data();
		}

		// ⭐ 场景级预分配：一次性付清 Page Fault / TLB Miss 成本
		void reserve(size_t cap) {
			Dense.reserve(cap);
			dense_to_entity.reserve(cap);
		}

		// ⭐ 直接暴露 Dense 数组指针，System 可绕过 Sparse 间接寻址
		// 使用场景：当 System 需要线性遍历全部组件（不需要按 Entity 查找）
		// 安全性：调用者必须保证不增删组件（遍历期间 Dense 不 resize）

		[[nodiscard]] T* raw_data() noexcept { return Dense.data(); }	// 头指针
		[[nodiscard]] const T* raw_data() const noexcept { return Dense.data(); }

		[[nodiscard]] Entity* raw_entity_data() noexcept { return dense_to_entity.data(); }
		[[nodiscard]] const Entity* raw_entity_data() const noexcept { return dense_to_entity.data(); }

		// 软件预取：提前将某实体的 Sparse→Dense 路径加载到 L1
		// 在热循环中对 i+PREFETCH_DIST 的实体调用，隐藏 L2 延迟
		void prefetch(Entity entity) const noexcept {
			auto idx = entity.index();
			// 阶段1: 预取 Sparse 条目 (2 bytes, 但会加载整条 Cache Line)
			_mm_prefetch(reinterpret_cast<const char*>(&Sparse[idx]), _MM_HINT_T0);
		}

		// 两阶段预取: 当 Sparse 值已知时，预取 Dense 条目
		void prefetch_dense(Entity entity) const noexcept {
			auto idx = entity.index();
			auto dense_idx = Sparse[idx];
			if (dense_idx != NULL_COMPONENT_ENTITY) {
				_mm_prefetch(reinterpret_cast<const char*>(&Dense[dense_idx]), _MM_HINT_T0);
			}
		}

		// 为System准备的迭代器 
		//  兼容性：完整的迭代器支持 Dense支持
		iterator begin() noexcept { return Dense.begin(); }
		iterator end() noexcept { return Dense.end(); }
		const_iterator begin() const noexcept { return Dense.begin(); }
		const_iterator end() const noexcept { return Dense.end(); }
		const_iterator cbegin() const noexcept { return Dense.cbegin(); }
		const_iterator cend() const noexcept { return Dense.cend(); }
	};
}
```

### A.3 View 与 viewIterator（节选自 `src/Core/Registry.hpp`，与原文一致）

```cpp
	template<typename... Components>
	class View {
	private:
		Registry& reg;					 // 获取组件池
		ISparseSet* smallest_pool;		 // 指针，非拥有（仅用于 find_smallest）
		
		// ⭐ 缓存：消除遍历中的虚函数调用
		const Entity* cached_entities;  // 直接指向 dense_to_entity.data()
		size_t cached_size;
		
		// ⭐ 替代 128KB entity_signatures: 仅缓存非最小池指针
		// View<Transform, Velocity> 只需检查 1 个额外池的 Sparse (32KB)
		// 而非扫描 128KB 的签名数组 → 工作集从 192KB 降至 64KB
		static constexpr size_t POOL_COUNT = sizeof...(Components);
		std::array<ISparseSet*, POOL_COUNT> other_pools{};
		size_t other_count = 0;

	public:
		View(Registry& r) : reg(r), smallest_pool(nullptr), cached_entities(nullptr), cached_size(0) {
			find_smallest();  // 构造函数体内调用
			cache_other_pools();	// 缓存非最小池指针
			
			// ⭐ 只在构造时调用一次虚函数，之后遍历全部走裸指针
			if (smallest_pool) {
				cached_entities = smallest_pool->entity_data();
				cached_size = smallest_pool->size();
			}
		}

		// 开始：从索引 0 开始找，Iterator 构造函数会自动跳过不合法的
		auto begin() const {
			return viewIterator(*this, 0);
		}

		// 结束：索引等于 size 就是结束
		auto end() const {
			return viewIterator(*this, cached_size);  // ⭐ 使用缓存 size
		}

		struct viewIterator {

			// 获取view的引用
			const View& view;

			// 当前在最小池里面的索引
			size_t index;

			// 构造函数
			viewIterator(const View& v, size_t i) : view(v), index(i) {
				// 🔥 关键点：一出生就要检查自己脚下的位置是否合法
				// 如果 index 0 的实体不符合要求，必须马上跳到下一个
				if (index < view.cached_size && !is_valid()) {
					++(*this); // 触发查找逻辑
				}
			}

			// 判断实体是否合法
			// ⭐ 核心优化: 直接查各池的 Sparse 数组 (各 32KB)
			//    不再触碰 128KB 的 entity_signatures
			//    且 has() 加载的 Cache Line 会被后续 get<T>() 复用 → 免费预取
			bool is_valid() const {
				Entity candidate = view.cached_entities[index];
				for (size_t i = 0; i < view.other_count; ++i) {
					if (!view.other_pools[i]->has(candidate)) return false;
				}
				return true;
			}

			// 核心：前进一步
			viewIterator& operator++() {
				// 1. 先盲目走一步
				index++;

				// 2. 只要没到底，且当前实体不合格，就继续走
				// 这就是 "Lazy Evaluation" (惰性求值)
				while (index < view.cached_size && !is_valid()) {
					index++;
				}
				return *this;
			}

			// 比较：只要索引不一样，就不相等
			bool operator!=(const viewIterator& other) const {
				return index != other.index;
			}

			Entity operator*() const {
				return view.cached_entities[index];  // ⭐ 直接数组访问，无虚函数！
			}

		};

	private:
		// 查找最小池
		void find_smallest() {
			size_t min_size = SIZE_MAX;
			([&] {
				auto& pool = reg.get_pool<Components>();
				if (pool.size() < min_size) {
					min_size = pool.size();
					smallest_pool = &pool;  // 存地址
				}
				}(), ...);
		}

		// 缓存非最小池指针 (替代 build_signature)
		// 构造时执行一次, 遍历时零开销
		void cache_other_pools() {
			other_count = 0;
			([&] {
				ISparseSet* pool = &reg.get_pool<Components>();
				if (pool != smallest_pool) {
					other_pools[other_count++] = pool;
				}
			}(), ...);
		}

	};
```

### A.4 HD-2D 片段着色器（`assets/shaders/shader.fs` 全文）

```glsl
#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;

uniform sampler2D sprite_normal;


uniform vec3 tangent;
uniform vec3 bitangent;
uniform vec3 facenormal;

uniform vec3 lightDir;  // 太阳在哪
uniform vec3 light_color;   // 太阳是什么颜色 白色：RGB 全满
uniform vec3 ambient_color; // 阴影颜色



out vec4 finalColor;

void main() {
    vec4 texel = texture(texture0, fragTexCoord);
    vec4 norel = texture(sprite_normal, fragTexCoord);

    mat3 TBN = mat3(tangent, bitangent, facenormal);
    vec3 normal = norel.rgb * 2.0 - 1.0;

    vec3 N = TBN * normal;
    N = normalize(N);
    vec3 L = normalize(lightDir);
    float NdotL = max(dot(N, L), 0.0);
    vec3 lighting = mix(ambient_color, light_color, NdotL);
    finalColor = vec4(texel.rgb * lighting, texel.a);
    //finalColor = vec4(N * 0.5 + 0.5, 1.0);
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

**表 E.1 缩略语表**

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

