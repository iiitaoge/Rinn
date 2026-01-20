#pragma once
#include "Core/Registry.hpp"
#include "Scripting/ScriptContext.hpp"
                                                            

namespace Rinn {
	// 绑定Registry
	void bind_registry(sol::state& lua, Registry& reg) {

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

		lua["emplace_Transform"] = [&reg](Entity e, float x, float y) {
			reg.emplace<Transform>(e, x, y);
			};
		lua["emplace_Velocity"] = [&reg](Entity e, float vx, float vy) {
			reg.emplace<Velocity>(e, vx, vy);
			};

		
	}
}
