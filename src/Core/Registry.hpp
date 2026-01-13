#pragma once
#include "Types.hpp"
#include "SparseSet.hpp"
#include "ComponentID.hpp"
#include <vector>
#include <optional>		// 为了实现 “空返回”
#include <bit>			// 为了实现快速  实体销毁组件
namespace Rinn {
	class Registry {
	private:
		// 活跃实体计数
		Entity_index alive_entity_count = 0;
		//实体签名，
		std::vector<Signature> entity_signatures;
		// 内存更安全，销毁自动释放内存
		std::vector<std::unique_ptr<ISparseSet>> Components_Pool;

		// 获取组件池 (浅尝辄止)
		template<typename T>
		SparseSet<T>& get_pool() {
			Component_ID id = get_component_type_id<T>();
			// 边界检查 (Debug only)
			assert(id < MAX_COMPONENTS && "Component ID out of range!");
			
			// 初始化组件池
			if (Components_Pool[id] == nullptr) {
				Components_Pool[id] = std::make_unique<SparseSet<T>>(MAX_ENTITIES);		//显示加载，便于维护
			}

			return *static_cast<SparseSet<T>*>(Components_Pool[id].get());		// 安全解引用
		}
	public:
		Registry()
			: entity_signatures(MAX_ENTITIES)      //100000 签名在堆上，安全
			, Components_Pool(MAX_COMPONENTS)     // 64个指针在对象中，数据在堆上
		{
		}
		// 创建实体
		Entity create_entity(Entity_index index, Entity_generation gen) {
			assert(alive_entity_count < MAX_ENTITIES && "Entity out of range!");
			Entity(index, gen);

		}
		// 是否有实体
		template<typename T>
		bool has(Entity entity) const {
			assert(entity < alive_entity_count && entity < MAX_ENTITIES && "Entity out of count!");		//实体数量不可超过最大值
			Component_ID id = get_component_type_id<T>();
			return entity_signatures[entity][id];

		}
		// 给实体挂起组件
		template<typename T>
		void emplace(Entity entity, T component) {
			assert(entity < alive_entity_count && entity < MAX_ENTITIES && "Entity out of count");
			Component_ID id = get_component_type_id<T>();	//获取组件id（在签名中的位数）
			assert(id < MAX_COMPONENTS && "Component ID out of MAX");
			entity_signatures[entity].set(id);		//签名层面挂起
			get_pool<T>().emplace(entity, component);	//SparseSet层面挂起
		}
		// 方案A：双版本设计（推荐）
		// 快速路径：用于 System 遍历（保证存在）
		template<typename T>
		T& get(Entity entity) {
			assert(has<T>(entity) && "Entity does not have component! Use try_get() for safe access.");
			return get_pool<T>().get(entity);
		}

		// 安全路径：用于用户代码（可能不存在）
		template<typename T>
		std::optional<std::reference_wrapper<T>> try_get(Entity entity) {

			// 先检查 entity 有效性（Release 模式也需要）
			if (entity >= alive_entity_count || entity >= MAX_ENTITIES) {
				return std::nullopt;
			}
			if (has<T>(entity)) {
				return std::ref(get_pool<T>().get(entity));
			}
			return std::nullopt;
		}
		// 移除指定实体指定组件
		template<typename T>
		void remove(Entity entity) {
			assert(entity < alive_entity_count && entity < MAX_ENTITIES && "Entity out of count");
			Component_ID id = get_component_type_id<T>();
			entity_signatures[entity].reset(id);		// 移除后 置0
			get_pool<T>().remove(entity);
		}

		// 销毁实体
		void destroy_entity(Entity entity) {
			assert(entity.index() < alive_entity_count && entity.index() < MAX_ENTITIES && "Entity out of range!");

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
			--alive_entity_count;
		}

		// 返回活跃实体 (未实现实体重用)
		size_t size() const {
			return alive_entity_count;  // 只返回活跃实体
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

			// 3. 重置实体计数
			alive_entity_count = 0;

			// 注意：Components_Pool 结构保留（64个指针，可能为nullptr）
		}
	};
}


