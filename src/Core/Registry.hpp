#pragma once
#include "Types.hpp"
#include "SparseSet.hpp"
#include "ComponentID.hpp"
#include "iostream"
#include "../Components/Components.hpp"

#ifdef _MSC_VER
#include <intrin.h>   // _mm_prefetch
#else
#include <xmmintrin.h>
#endif

namespace Rinn {

	// 需要确保Registry在堆或者静态区
	class EntityPool {
	private:
		// 1. 物理常量 (编译期计算)
		static constexpr uint16_t CAPACITY = MAX_ENTITIES;
		static constexpr uint16_t MASK = CAPACITY - 1;

		// 验证实体数是否为 2 的幂
		static_assert((CAPACITY& (CAPACITY - 1)) == 0,
			"CAPACITY must be power of 2 for MASK to work!");

		// 2. 核心数据 (全部 Inline，无堆分配)
		// 32KB 的 Ring Buffer + 32KB 的 Generation 数组
		// 这一坨 64KB 的数据紧密排列，对 L1/L2 Cache 极度友好
		std::array<Entity_generation, CAPACITY> generations{};	// 版本数组 (零初始化)
		std::array<Entity_index, CAPACITY> ring_buffer{};		// 存放尸体的环形缓冲区 (零初始化)

		// 3. 游标 (使用 uint16 足够，节省寄存器宽度)	为了复用
		uint16_t head = 0;
		uint16_t tail = 0;

		// 4. 水位线
		Entity_index next_idx = 0;				// 尸体用完了，分配新索引
		uint16_t alive_entity_count = 0;		// 活跃实体数

	public:
		// 构造函数：零开销 (Array 不初始化就是垃圾值，但这正是我们要的)
		// Generation 建议初始化为 0 (可以使用 fill，或者依赖全局静态区的零初始化)
		EntityPool() {
			generations.fill(0);
		}

		[[nodiscard]] bool has_recycled_ids() const noexcept {
			return head != tail;
		}

		// 获取实体
		[[nodiscard]] Entity acquire() noexcept {
			Entity_index idx;

			// 分支预测优化：通常游戏初期主要走 else (开荒)，后期主要走 if (复用)
			if (head != tail) {
				// 1. 复用逻辑
				idx = ring_buffer[head];
				head = (head + 1) & MASK; // 极速位运算
			}
			else {
				// 2. 开荒逻辑
				assert(next_idx < CAPACITY && "Entity pool exhausted!");
				idx = next_idx++;
			}

			++alive_entity_count;

			// 组合 Handle (值返回)
			return Entity(idx, generations[idx]);
		}

		// 该实体索引进入尸体缓冲区 等待复用
		void release(Entity_index idx) noexcept {
			// 安全检查：只有活着的才能死 (防止 Double Free)
			// 这一步非常重要，防止逻辑层 Bug 污染底层池
			// assert(is_valid_index(idx)); 

			// 1. 版本号自增 (核心安全)
			generations[idx]++;

			// 2. 入队
			ring_buffer[tail] = idx;
			tail = (tail + 1) & MASK; // 极速位运算

			--alive_entity_count;
		}

		// 检查 Handle 是否有效 (Gatekeeper)
		[[nodiscard]] bool is_valid(Entity entity) const noexcept {
			// 1. 索引必须在水位线之内 (防止访问未出生的实体)
			// 2. 索引必须小于物理上限 (防止越界)
			// 3. 版本号必须匹配 (防止僵尸)
			return entity.index() < next_idx &&
				generations[entity.index()] == entity.generation();
		}
		// 重置实体池
		void clear() noexcept {
			head = 0;
			tail = 0;
			next_idx = 0;
			alive_entity_count = 0;
			generations.fill(0);  // 重置所有版本号
		}

		[[nodiscard]] size_t size() const noexcept { return alive_entity_count; }

		// 实体上限
		[[nodiscard]] size_t capacity() const noexcept { return CAPACITY; }
	};

	class Registry {
	private:

		template<typename... Components> friend class View;

		EntityPool entity_pool;

		//实体签名，无跳转
		std::array<Signature, MAX_ENTITIES> entity_signatures;  
		// 组件池，无跳转
		std::array<std::unique_ptr<ISparseSet>, MAX_COMPONENTS> Components_Pool;  

		// 获取组件池 (浅尝辄止)
		template<typename T>
		[[nodiscard]] SparseSet<T>& get_pool() {
			Component_ID id = get_component_type_id<T>();
			// 边界检查 (Debug only)
			assert(id < MAX_COMPONENTS && "Component ID out of range!");
			
			// 初始化组件池
			if (Components_Pool[id] == nullptr) {
				Components_Pool[id] = std::make_unique<SparseSet<T>>();		// 延迟初始化，定义的组件类型可能会变
			}

			return *static_cast<SparseSet<T>*>(Components_Pool[id].get());		// 安全解引用
		}
	public:
		Registry(){}

		// ⭐ System 直接访问类型化组件池（用于 raw_data() 线性遍历）
		// 使用场景：PhysicsSystem 等需要批量处理全部组件的 System
		// 约束：遍历期间不得增删该类型的组件
		template<typename T>
		[[nodiscard]] SparseSet<T>& pool() { return get_pool<T>(); }

		// 提供一个辅助函数，返回 View 对象
		template<typename... Components>
		View<Components...> view() {
			return View<Components...>(*this);
		}


		// 新增：检查实体是否存活
		[[nodiscard]] bool is_alive(Entity entity) const noexcept {
			return entity_pool.is_valid(entity);
		}

		// 创建实体
		[[nodiscard]] Entity create_entity() noexcept {
			return entity_pool.acquire();

		}
		// 是否有对应组件
		template<typename T>
		[[nodiscard]] bool has(Entity entity) const {
			assert(is_alive(entity) && "Entity is dead or stale!");
			Component_ID id = get_component_type_id<T>();
			return entity_signatures[entity.index()][id];

		}
		// 完美转发
		// 给实体挂起组件(优化为原地构造)
		template<typename T, typename... Args>
		[[nodiscard]] T& emplace(Entity entity, Args&&... args) {  // ✅ 原地构造
			assert(is_alive(entity));
			Component_ID id = get_component_type_id<T>();
			entity_signatures[entity.index()].set(id);
			return get_pool<T>().emplace(entity, std::forward<Args>(args)...);
		}


		// 获取该实体的指定组件
		// 方案A：双版本设计（推荐）
		// 快速路径：用于 System 遍历（保证存在）
		template<typename T>
		[[nodiscard]] T& get(Entity entity) {
			assert(is_alive(entity) && "Entity is dead or stale!");
			assert(has<T>(entity) && "Entity does not have component! Use try_get() for safe access.");
			return get_pool<T>().get(entity);
		}

		// 安全路径：用于用户代码（可能不存在）
		template<typename T>
		[[nodiscard]] std::optional<std::reference_wrapper<T>> try_get(Entity entity) noexcept {
			if (!is_alive(entity)) return std::nullopt;

			Component_ID id = get_component_type_id<T>();
			if (!entity_signatures[entity.index()][id]) return std::nullopt;

			return std::ref(get_pool<T>().get(entity));
		}

		// 移除指定实体指定组件
		template<typename T>
		void remove(Entity entity) {
			assert(is_alive(entity) && "Entity is dead or stale!");
			Component_ID id = get_component_type_id<T>();
			entity_signatures[entity.index()].reset(id);		// 签名层面移除
			get_pool<T>().remove(entity);						// 组件池层面移除
		}

		// 销毁实体
		void destroy_entity(Entity entity) {
			assert(is_alive(entity) && "Entity is dead or stale!");

			Signature& sig = entity_signatures[entity.index()];

			// 方案A：使用 to_ullong() + 溢出检查
			if constexpr (MAX_COMPONENTS <= 64) {
				// 检查是否所有位都设置（防止溢出）
				if (sig.all()) {
					// 特殊处理：直接遍历
					for (Component_ID id = 0; id < MAX_COMPONENTS; ++id) {
						if (Components_Pool[id] != nullptr) {
							Components_Pool[id]->remove(entity);
						}
					}
				}
				else {
					// 使用硬件加速
					unsigned long long n = sig.to_ullong();
					while (n > 0) {
						int count = std::countr_zero(n);
						Component_ID index = static_cast<Component_ID>(count);
						assert(index < MAX_COMPONENTS && "Component ID out of range!");
						if (Components_Pool[index] != nullptr) {
							Components_Pool[index]->remove(entity);
						}
						n &= (n - 1);
					}
				}
			}

			sig.reset();
			entity_pool.release(entity.index());
		}

		// 返回活跃实体 (未实现实体重用)
		[[nodiscard]] size_t size() const {
			return entity_pool.size();  // 只返回活跃实体
		}

		// 保留 Registry 结构，清空组件
		void clear() {
			// 1. 清空所有组件池（调用每个 SparseSet::clear()）
			for (auto& pool : Components_Pool) {
				if (pool != nullptr) {
					pool->clear();  // 虚函数调用，清空该类型的所有组件
				}
			}

			// 2. 重置所有签名
			std::fill(entity_signatures.begin(), entity_signatures.end(), Signature{});

			// 3. 重置实体池
			entity_pool.clear();

			// 注意：Components_Pool 结构保留（64个指针，日后可继续使用）
		}
	};

	
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
}
