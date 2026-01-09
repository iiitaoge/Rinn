#pragma once
#include "Types.hpp"
#include "SparseSet.hpp"
#include "ComponentID.hpp"
#include<vector>

namespace Rinn {
	class Registry {
	private:
		// 实体计数
		Entity entity_count = 0;
		//实体签名，
		std::vector<Signature> entity_signatures;
		// 内存更安全，销毁自动释放内存
		std::vector<std::unique_ptr<ISparseSet>> Components_Pool;

		// 获取组件池 (浅尝辄止)
		template<typename T>
		SparseSet<T>& get_pool() {
			Component_ID id = get_component_type_id<T>();
			// 边界检查 (Debug only)
			assert(id < Components_Pool.size() && "Component ID out of range!");
			assert(Components_Pool[id] != nullptr && "Component pool not initialized!");
			return *static_cast<SparseSet<T>*>(Components_Pool[id].get());
		}
	public:

		Entity create_entity() {
			return entity_count++;
		}
		// 是否有实体
		template<typename T>
		bool has(Entity entity) {
			return get_pool<T>().has(entity);
		}
		// 给实体挂起组件
		template<typename T>
		void emplace(Entity entity, T component) {
			return get_pool<T>().add(entity, component);
		}
		// 获取实体的一个具体组件
		template<typename T>
		T& get(Entity entity) {
			return get_pool<T>().get(entity);
		}
		// 移除指定实体指定组件
		template<typename T>
		void remove(Entity entity) {
			return get_pool<T>().remove(entity);
		}
	};
}


