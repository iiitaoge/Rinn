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