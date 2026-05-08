#pragma once
#include "../Core/Registry.hpp"
#include "../Components/Components.hpp"
#include "EventSystem.hpp"
#include <algorithm>
#include <optional>
#include <unordered_map>

// =====================================================================
// AppraisalSystem - M4
// ---------------------------------------------------------------------
// 订阅 EventBus 信息事件 -> 维护 KnowledgeFact entity + 设置 knowers bitset
// "存储世界级 (fact = entity), 查询 per-pair (bitset.test)"
//
// 同时附带情绪 / 预期 delta (信息事件不改 satisfaction)
// 物质 / 社交 / 内省的 handler 留 M5 / M6 加 (同形态 Subscribe)
// =====================================================================

namespace Rinn::AppraisalSystem {

    inline Registry* g_reg = nullptr;

    // fact_type (= EventType) -> fact entity, O(1) 查找索引
    inline std::unordered_map<uint16_t, Entity> fact_index;

    inline std::optional<Entity> get_fact(EventBus::EventType t) {
        auto it = fact_index.find(static_cast<uint16_t>(t));
        if (it == fact_index.end()) return std::nullopt;
        if (!g_reg || !g_reg->is_alive(it->second)) return std::nullopt;
        return it->second;
    }

    inline bool npc_knows(Entity npc, EventBus::EventType t) {
        auto fact = get_fact(t);
        if (!fact.has_value()) return false;
        auto& fc = g_reg->get<KnowledgeFactComponent>(*fact);
        return fc.knowers.test(npc.index());
    }

    inline Entity find_or_create_fact(EventBus::EventType t, Entity subject, uint32_t timestamp) {
        if (auto cached = get_fact(t); cached.has_value()) return *cached;

        Entity fact = g_reg->create_entity();
        KnowledgeFactComponent fc{};
        fc.fact_type = static_cast<uint32_t>(t);
        fc.subject   = subject;
        fc.timestamp = timestamp;
        // bitset 默认全零
        g_reg->emplace<KnowledgeFactComponent>(fact, fc);
        fact_index[static_cast<uint16_t>(t)] = fact;
        return fact;
    }

    // 信息事件的 appraisal 副作用 (情绪 / 预期 delta)
    inline void apply_appraisal_effects(Entity npc, const EventBus::Event& e) {
        using EventType = EventBus::EventType;

        // 预期变化 (NeedComponent.expectation)
        if (auto need_opt = g_reg->try_get<NeedComponent>(npc); need_opt.has_value()) {
            auto& need = need_opt->get();
            if (e.type == EventType::TaxIncreased) {
                // 资源 (idx 0) 预期下降
                need.expectation[0] -= 0.5f * std::max(0.1f, e.payload_f);
            }
        }

        // 情绪变化 (EmotionComponent.intensity), 软上限 1.0
        if (auto emo_opt = g_reg->try_get<EmotionComponent>(npc); emo_opt.has_value()) {
            auto& emo = emo_opt->get();
            auto bump = [&](int idx, float delta) {
                emo.intensity[idx] = std::min(1.0f, emo.intensity[idx] + delta);
            };
            // EmotionComponent: 0 愤怒 / 1 焦虑 / 2 恐慌 / 3 悲伤 / 4 孤独
            switch (e.type) {
                case EventType::TaxIncreased:
                    bump(0, 0.30f);  // 愤怒
                    bump(1, 0.50f);  // 焦虑
                    break;
                case EventType::PriestDied:
                    bump(3, 0.70f);  // 悲伤
                    bump(4, 0.40f);  // 孤独
                    break;
                case EventType::HeardLastWords:
                    bump(3, 0.20f);
                    break;
                default:
                    break;
            }
        }
    }

    // 信息事件统一 handler:
    //  1. actor 必须有 online StoneTablet (没 tablet -> 不传播)
    //  2. find_or_create fact entity
    //  3. 遍历所有 online tablet 的 NPC, 设 knowers bit + 跑 appraisal
    inline void handle_information(const EventBus::Event& e) {
        if (!g_reg) return;

        // --- gate 1: actor 有 online tablet 吗? ---
        bool can_broadcast = false;
        if (!e.actor.is_null() && g_reg->is_alive(e.actor)) {
            auto opt = g_reg->try_get<StoneTabletComponent>(e.actor);
            if (opt.has_value() && opt->get().online) {
                can_broadcast = true;
            }
        }
        if (!can_broadcast) {
            // actor 无 online tablet: 不广播, 静默丢弃 (M5/M6 可以加非广播路径)
            return;
        }

        // --- step 2: find or create fact ---
        uint32_t now = 0;  // 简化: 占位时间戳, M5 可接 TimeControl::game_time
        Entity fact = find_or_create_fact(e.type, e.actor, now);
        auto& fact_data = g_reg->get<KnowledgeFactComponent>(fact);

        // --- step 3: 路由到所有 online tablet 的 NPC ---
        for (Entity npc : g_reg->view<NeedComponent, StoneTabletComponent>()) {
            auto& tablet = g_reg->get<StoneTabletComponent>(npc);
            if (!tablet.online) continue;
            // 射程检查留 M5/M6 加 (broadcast_range vs Transform 距离)

            uint16_t idx = npc.index();
            if (!fact_data.knowers.test(idx)) {
                fact_data.knowers.set(idx);
            }
            apply_appraisal_effects(npc, e);
        }
    }

    inline void Init(Registry& reg) {
        g_reg = &reg;
        fact_index.clear();

        EventBus::Subscribe(EventBus::EventType::TaxIncreased,   handle_information);
        EventBus::Subscribe(EventBus::EventType::PriestDied,     handle_information);
        EventBus::Subscribe(EventBus::EventType::HeardLastWords, handle_information);
        // M5/M6 同形态扩 (物质/社交/内省 handler):
        // EventBus::Subscribe(EventType::TaxCollected, handle_material);
        // EventBus::Subscribe(EventType::Insulted,     handle_social);
    }
}
