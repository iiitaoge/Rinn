#pragma once
#include "Core/Registry.hpp"
#include "Scripting/ScriptContext.hpp"
#include "ComponentList.hpp"
#include "ComponentTraits.hpp"
#include "Resources/ResourceManager.hpp"
#include <string>
namespace Rinn {

	// 绑定单个组件的所有操作
	template<typename T>
	void bind_component(sol::state& lua, Registry& reg) {
		using Trait = ComponentTrait<T>;
		std::string n = Trait::name;

		// emplace: 从 Lua table 构造组件
		lua["emplace_" + n] = [&reg](Entity e, sol::table t) {
			reg.emplace<T>(e, Trait::from_table(t));
			};

		// get: 返回 Lua table
		lua["get_" + n] = [&reg](Entity e, sol::this_state ts) -> sol::table {
			sol::state_view lua(ts);
			return Trait::to_table(lua, reg.get<T>(e));
			};

		// has: 检查组件
		lua["has_" + n] = [&reg](Entity e) {
			return reg.has<T>(e);
			};

		// remove: 删除组件
		lua["remove_" + n] = [&reg](Entity e) {
			reg.remove<T>(e);
			};
	}
	// 辅助：展开 tuple 绑定所有类型
	template<typename Tuple, std::size_t... Is>
	void bind_all_impl(sol::state& lua, Registry& reg, std::index_sequence<Is...>) {
		(bind_component<std::tuple_element_t<Is, Tuple>>(lua, reg), ...);
	}
	// ========================================
	// 🚀 一行绑定所有组件！
	// ========================================
	inline void bind_all_components(sol::state& lua, Registry& reg) {
		bind_all_impl<AllComponents>(
			lua, reg,
			std::make_index_sequence<std::tuple_size_v<AllComponents>>{}
		);
	}

	// 绑定Registry
	inline void bind_registry(sol::state& lua, Registry& reg) {

		// 1. 绑定 Entity 类型
		lua.new_usertype<Entity>("Entity",
			"index", &Entity::index,
			"generation", &Entity::generation,
			"is_null", &Entity::is_null
		);


		lua["create_entity"] = [&reg]() {
				return reg.create_entity();
			};

		lua["destroy_entity"] = [&reg](Entity e) {
				return reg.destroy_entity(e);
			};

		lua["is_alive"] = [&reg](Entity e) {
				return reg.is_alive(e);
			};

		bind_all_components(lua, reg);
		
	}
	// 绑定资源管理器
	inline void bind_resources(sol::state& lua, ResourceManager& rm) {
		lua["load_texture"] = [&rm](const std::string& path) {
			return rm.load_texture(path);
			};
	}
}
