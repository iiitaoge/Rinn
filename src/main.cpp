#include <iostream>
#include <sol/sol.hpp>
#include "Core/Registry.hpp"
#include "Components/Components.hpp"
#include "Scripting/LuaBinder.hpp"
#include "Scripting/ScriptContext.hpp"
#include "Systems/RenderSystem.hpp"
#include "Systems/InputSystem.hpp"
#include "Systems/PhysicSystem.hpp"
#include "Systems/CollisionSystem.hpp"

using namespace::Rinn;

int main() {


	Registry reg;
	sol::state lua;
	Init_lua(lua);
	bind(lua, reg);
	auto result = lua.script_file("../../../scripts/main.lua");	// 路径跟exe位置有关


	if (!result.valid()) {
		sol::error err = result;
		std::cerr << "加载脚本失败: " << err.what() << std::endl;
	}

	RenderSystem::Init(1600, 1400, "Rinn");
	
	while (!RenderSystem::ShouldClose()) {
		RenderSystem::BeginFrame();
		RenderSystem::DrawText(std::format("FPS: {}", RenderSystem::FPS()).c_str(), 10, 10, 20, GREEN);

		lua["on_update"]();  // 每帧调 Lua

		PhysicSystem::update(reg, RenderSystem::DeltaTime());
		auto hits = CollisionSystem::detect(reg);
		for (auto& h : hits) {
			std::cout << std::format("Collision: {} <-> {}\n", h.a.index(), h.b.index());
		}
		CollisionSystem::resolve(reg, hits);
		for (Entity e : reg.view<Rinn::Transform>()) {
			Rinn::Transform& t = reg.get<Rinn::Transform>(e);
			RenderSystem::DrawRectFilled(t.x, t.y, 20, 20, BLUE);
			DrawText(std::format("{}", e.index()).c_str(), (int)t.x + 18, (int)t.y + 15, 20, RED);
		}

		RenderSystem::EndFrame();
	}
	return 0;
}