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
#include "Systems/LineSystem.hpp"
#include "Systems/EntityNameSystem.hpp"
#include "UI/ComponentUI.hpp"
#include "UI/LightUI.hpp"
#include "UI/AIDebugUI.hpp"
#include "UI/DebugPanelUI.hpp"

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

	EntityNameSystem::Init(reg);

	// AppraisalSystem 注册全部信息 + 完成事件 handler (M4 + M6 F1/F3/F4)
	AppraisalSystem::Init(reg);

	// LineSystem (M6 v2): 接收 hook 调用, 用 raylib TextBubble 渲染中文
	// name getter 返回中文显示名 (DisplayName), 用于 {actor} 占位
	LineSystem::Init(reg, [](Entity e) -> std::string {
		return EntityNameSystem::DisplayName(e);
	});

	// 把 LineSystem 钩到 DecisionSystem (动作选定时立即说话)
	DecisionSystem::g_on_action_chosen = LineSystem::on_action_chosen;
	// 把 LineSystem 钩到 AppraisalSystem (witness 反应足够强时说话)
	AppraisalSystem::g_on_witness_react = LineSystem::on_witness_react;

	// EventBus -> EventLog: 所有 event 进 ring buffer 给 ImGui 显示
	EventBus::SubscribeAll([](const EventBus::Event& e) {
		EventLog::PushFmt(EventLog::Level::Info, "EventBus",
			"%s actor=%s target=%s f=%.2f i=%d",
			EventBus::TypeName(e.type),
			EntityNameSystem::NameOrId(e.actor).c_str(),
			EntityNameSystem::NameOrId(e.target).c_str(),
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

		// NPC AI pipeline (M2 -> M6)
		EmotionDecaySystem::Update(reg, dt);
		DecisionSystem::Update(reg, dt);
		ActionExecutionSystem::Update(reg, dt);
		LineSystem::Tick(dt);

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
		DebugPanelUI::Draw(reg);
		rlImGuiEnd();
		RenderSystem::EndFrame();
	}

	res.unload_all();
	AudioSystem::Shutdown();
	RenderSystem::Shutdown();
	return 0;
}
