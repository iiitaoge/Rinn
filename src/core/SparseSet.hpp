#pragma once
#include"Types.hpp"
#include<vector>
#include<cassert>

namespace Rinn{
	class ISparseSet {
	public:
		virtual ~ISparseSet() = default;

		// 构造函数：限制Sparse大小为100000，初始化为 NULL_ENTITY
		ISparseSet(size_t max_entities = 100000) {
			Sparse.resize(max_entities, NULL_ENTITY);
		}

		// 检查该实体是否有对应组件
		bool has(Entity entity) const {
			return entity < Sparse.size() && Sparse[entity] != NULL_ENTITY;
		}
		virtual void remove(Entity entity) = 0;
		virtual void clear() = 0;
		virtual size_t size() const = 0;

	protected:
		std::vector<Entity_index> Sparse;
	};

	// 具体组件类实现
	template<typename T>
	class SparseSet : public ISparseSet {
	private:
		std::vector<T> Dense;
		std::vector<Entity_index> dense_to_entity;	// 组件对应实体，用于dense反向定位sparse
	public:
		using ISparseSet::ISparseSet;
		// 给实体挂组件
		void add(Entity entity, T component) {
			if (entity >= Sparse.size()) {
				// 如果 entity ID 太大，静默扩容
				Sparse.resize(entity + 1, NULL_ENTITY);
			}
			if (Sparse[entity] != NULL_ENTITY)
				return;
			// 原子操作：更新 Dense 和 dense_to_entity
			Dense.push_back(component);
			dense_to_entity.push_back(entity);
			Sparse[entity] = (Entity_index)(Dense.size() - 1);
		}

		T& get(Entity entity) {
			assert(has(entity) && "Entity does not have this component!");
			return Dense[Sparse[entity]];
		}

		// dense_to_entity和Dense必须保持一致性：一致写，一致删
		void remove(Entity entity) {
			if (entity >= Sparse.size() || Sparse[entity] == NULL_ENTITY) {
				return; // 或者 assert(false);
			}
			Entity_index index_deleted = Sparse[entity];		// 被删除实体在Dense中的索引
			Entity_index index_last = Dense.size() - 1;		// 队尾索引
			Entity entity_last = dense_to_entity[index_last];	//队尾实体号

			// 维护稠密数组 Dense
			Dense[index_deleted] = Dense[index_last];
			Dense.pop_back();

			// 维护稀疏数组 Sparse
			Sparse[entity_last] = index_deleted;
			Sparse[entity] = NULL_ENTITY;

			// 维护dense_to_entity
			dense_to_entity[index_deleted] = entity_last;
			dense_to_entity.pop_back();
		}

		// 重置 
		void clear() override {
			Dense.clear();
			dense_to_entity.clear();
			std::fill(Sparse.begin(), Sparse.end(), NULL_ENTITY);
		}

		// 组件数
		size_t size() const override {
			return Dense.size();
		}

		// 为System准备的迭代器 
		auto begin() { return Dense.begin(); }
		auto end() { return Dense.end(); }
		auto begin() const { return Dense.begin(); }
		auto end() const { return Dense.end(); }
	};
}