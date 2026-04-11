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
#include "DebugUI/DebugUI.hpp"

using namespace::Rinn;

int main() {


	RenderSystem::Init(1600, 1400, "Rinn");
	DebugUI::Init();

	Registry reg;
	ResourceManager res;

	sol::state lua;
	Init_lua(lua);
	bind(lua, reg, res);
	auto result = lua.script_file("../../../scripts/main.lua");

	if (!result.valid()) {
		sol::error err = result;
		std::cerr << "加载脚本失败: " << err.what() << std::endl;
	}
	
	while (!RenderSystem::ShouldClose()) {
		RenderSystem::BeginFrame();
		lua["on_update"]();  // 每帧调 Lua

		PhysicSystem::update(reg, RenderSystem::DeltaTime());
		auto hits = CollisionSystem::detect(reg);
		CollisionSystem::resolve(reg, hits);
		RenderSystem::DrawSprites(reg, res);
		
		// 退出世界渲染，进入 UI 的 1:1 屏幕空间！
		RenderSystem::EndCameraMode();
		RenderSystem::DrawTextBubbles(reg);
		RenderSystem::DrawText(std::format("FPS: {}", RenderSystem::FPS()).c_str(), 10, 10, 20, GREEN);

		DebugUI::Draw(reg);
		RenderSystem::EndFrame();
	}

	res.unload_all();
	RenderSystem::Shutdown();
	return 0;
}