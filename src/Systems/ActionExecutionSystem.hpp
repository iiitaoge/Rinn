#pragma once
#include "../Core/Registry.hpp"
#include "../Components/Components.hpp"
#include "EventSystem.hpp"
#include "DecisionSystem.hpp"
#include "EntityNameSystem.hpp"
#include "../UI/AIDebugUI.hpp"
#include <algorithm>

// =====================================================================
// ActionExecutionSystem - M5
// ---------------------------------------------------------------------
// 推进 DecisionComponent.action_progress.
// 完成时:
//   1. 给主要 need 加 satisfaction (满足度补偿)
//   2. publish complete_event (如果非 None) - 这就是涌现叙事的下一跳
//   3. 触发立即重决策
// =====================================================================

namespace Rinn::ActionExecutionSystem {

    inline void Update(Registry& reg, float dt) {
        for (Entity e : reg.view<NeedComponent, DecisionComponent>()) {
            auto& dec = reg.get<DecisionComponent>(e);
            if (dec.current_action_id >= DecisionSystem::action_catalog.size()) continue;

            const auto& act = DecisionSystem::action_catalog[dec.current_action_id];
            if (dec.action_progress >= 1.0f) continue;

            float duration = std::max(0.1f, act.duration);
            dec.action_progress += dt / duration;

            if (dec.action_progress >= 1.0f) {
                dec.action_progress = 1.0f;

                // 1. satisfaction 补偿
                if (act.gain_need_idx >= 0 && act.gain_need_idx < NeedComponent::N) {
                    auto& need = reg.get<NeedComponent>(e);
                    need.satisfaction[act.gain_need_idx] =
                        std::min(10.0f, need.satisfaction[act.gain_need_idx] + act.gain);
                }

                // 2. EventLog: 让每个 action 完成都可见 (M5 demo 验证用)
                EventLog::PushFmt(EventLog::Level::Info, "Action",
                    "%s completed: %s (+%.2f to need[%d])",
                    EntityNameSystem::NameOrId(e).c_str(),
                    act.name, act.gain, act.gain_need_idx);

                // 3. publish 完成事件 (产生下一跳, 仅当 catalog 里指定了 complete_event)
                if (act.complete_event != EventBus::EventType::None) {
                    EventBus::Publish(EventBus::Event{
                        act.complete_event, e, Entity{}, 0.0f, 0
                    });
                }

                // 4. 触发立即重决策 (next tick 重新评估 utility)
                dec.next_decision_tick = 0;
            }
        }
    }
}
