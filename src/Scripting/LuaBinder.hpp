#pragma once
#include <sol/sol.hpp>
#include "../Core/Registry.hpp"
#include "../Components/Components.hpp"
#include "../Resources/ResourceManager.hpp"
#include "../Systems/InputSystem.hpp"
#include "../Systems/CollisionSystem.hpp"
#include "../Systems/RenderSystem.hpp"

namespace Rinn {
	inline void bind(sol::state& lua, Registry& reg, ResourceManager& res) {
		// 极简主义：引入最基础的标准库、字符串库和数学库以支持地图截取和计算
		lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::math, sol::lib::table);

		lua.new_usertype<Entity>("Entity");  // 纯黑盒，不暴露字段

		lua.set_function("load_texture", [&res](const std::string& path) -> uint16_t {
			return res.load_texture(path);
		});
		// 绑定 创建实体
		lua.set_function("create_entity", [&reg]() { return reg.create_entity(); });
		// 绑定 挂载组件
		// Lua 侧：set(entity_id, "transform", {x=100, y=200})
		// C++ 侧：一个函数搞定所有组件
		lua.set_function("set", [&reg](Entity e, const std::string& name, sol::table data) {
			if (name == "Transform") {
				reg.emplace<Transform>(e,
					data.get<float>("x"),
					data.get<float>("y"),
					data.get<int>("layer"));
			}
			else if (name == "Velocity") {
				reg.emplace<Rinn::Velocity>(e,
					data.get<float>("x"),
					data.get<float>("y"));
			}
			else if (name == "Sprite") {
				reg.emplace<Rinn::Sprite>(e,
					static_cast<uint16_t>(data["texture_id"]),
					data["width"], data["height"], data["src_x"], data["src_y"], data["src_w"], data["src_h"]);
			}
			else if (name == "Collider") {
				reg.emplace<Rinn::Collider>(e,
					data.get<float>("width"),
					data.get<float>("height"));
			}
			else if (name == "TextBubble") {
				// 自定义 ECS 没有 emplace_or_replace，需要手动先安全移除再挂载
				if (reg.has<Rinn::TextBubble>(e)) {
					reg.remove<Rinn::TextBubble>(e);
				}
				auto& tb = reg.emplace<Rinn::TextBubble>(e);
				
				std::string t = data.get<std::string>("text");
				strncpy(tb.text, t.c_str(), 255);
				tb.display_time = data.get<float>("time"); // 移除 get_or，强制要求必须传入 time
			}
			});

		// 绑定移除组件
		lua.set_function("remove", [&reg](Entity e, const std::string& name) {
			if (name == "TextBubble") {
				reg.remove<TextBubble>(e);
			}
			});

		// lua获取位置
		lua.set_function("get_pos", [&reg](Entity e) -> std::pair<float, float> {
			auto& t = reg.get<Transform>(e);
			return { t.x, t.y };
			});
		// lua 获取速度
		lua.set_function("get_vel", [&reg](Entity e) -> std::pair<float, float> {
			auto& v = reg.get<Velocity>(e);
			return { v.vx, v.vy };
			});
		// 鼠标键盘交互
		lua.set_function("is_key_down", InputSystem::is_key_down);

		// C++唯一 速度 接口
		lua.set_function("move", [&reg](Entity e, float dx, float dy) {
			auto& v = reg.get<Rinn::Velocity>(e);
			constexpr float speed = 200.0f;  // 物理常数，归 C++ 管
			v.vx = dx * speed;
			v.vy = dy * speed;
			});

		// 碰撞检测：返回碰撞对 table 给 Lua
		lua.set_function("get_collisions", [&reg]() {
			auto hits = CollisionSystem::detect(reg);
			std::vector<std::pair<Entity, Entity>> result;
			result.reserve(hits.size());
			for (auto& h : hits) result.emplace_back(h.a, h.b);
			return sol::as_table(result);
			});

		// ========= HD-2D 新增：控制 3D 摄像机焦点 =========
		lua.set_function("set_camera_target", RenderSystem::UpdateCamera);
		// ========= 渲染模式切换 =========
		lua.set_function("set_hd2d_mode", RenderSystem::SetHD2DMode);
	}
}