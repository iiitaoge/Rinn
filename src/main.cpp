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
#include "Systems/AudioSystem.hpp"
#include "Systems/EmotionDecaySystem.hpp"
#include "Systems/EventSystem.hpp"
#include "Systems/AppraisalSystem.hpp"
#include "Systems/DecisionSystem.hpp"
#include "Systems/ActionExecutionSystem.hpp"
#include "UI/ComponentUI.hpp"
#include "UI/LightUI.hpp"
#include "UI/AIDebugUI.hpp"

using namespace::Rinn;

int main() {


	RenderSystem::Init(1600, 1400, "Rinn");
	AudioSystem::Init();
	ComponentUI::Init();

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

	// AppraisalSystem 注册 4 个信息事件 handler (M4)
	AppraisalSystem::Init(reg);

	// EventBus -> EventLog: 所有 event 进 ring buffer 给 ImGui 显示
	EventBus::SubscribeAll([](const EventBus::Event& e) {
		EventLog::PushFmt(EventLog::Level::Info, "EventBus",
			"%s actor=%s target=%s f=%.2f i=%d",
			EventBus::TypeName(e.type),
			DemoVillage::NameOrId(e.actor).c_str(),
			DemoVillage::NameOrId(e.target).c_str(),
			e.payload_f, e.payload_i);
	});

	while (!RenderSystem::ShouldClose()) {
		RenderSystem::BeginFrame();
		lua["on_update"]();  // 每帧调 Lua

		// 全局可调 dt：暂停/单步/倍速 都汇集在 TimeControl
		float dt = TimeControl::Tick(RenderSystem::DeltaTime());

		PhysicSystem::update(reg, dt);
		auto hits = CollisionSystem::detect(reg);
		CollisionSystem::resolve(reg, hits);

		// NPC AI pipeline (M2 -> M5)
		EmotionDecaySystem::Update(reg, dt);
		DecisionSystem::Update(reg, dt);
		ActionExecutionSystem::Update(reg, dt);

		// 所有 system publish 完之后, drain 一次 (FIFO 同帧到空)
		EventBus::Drain();

		// DrawGrid(40, 1.0f);  // 调试：XZ 平面参考网格（40格×1米）
		BeginShaderMode(RenderSystem::test_shader);
		RenderSystem::DrawSprites(reg, res);
		EndShaderMode();
		
		// 退出世界渲染，进入 UI 的 1:1 屏幕空间！
		RenderSystem::EndCameraMode();
		RenderSystem::DrawTextBubbles(reg);
		
		AudioSystem::Update();
		
		RenderSystem::DrawText(std::format("FPS: {}", RenderSystem::FPS()).c_str(), 10, 10, 20, GREEN);

		rlImGuiBegin();
		ComponentUI::DrawEntityPanel(reg);
		LightUI::DrawLightPanel();
		AIDebugUI::Draw(reg);
		rlImGuiEnd();
		RenderSystem::EndFrame();
	}

	res.unload_all();
	AudioSystem::Shutdown();
	RenderSystem::Shutdown();
	return 0;
}