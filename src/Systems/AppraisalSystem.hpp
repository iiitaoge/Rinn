#pragma once
#include "../Core/Registry.hpp"
#include "../Components/Components.hpp"
#include "EventSystem.hpp"
#include <algorithm>
#include <initializer_list>
#include <optional>
#include <unordered_map>

// =====================================================================
// AppraisalSystem - M4 + M6
// ---------------------------------------------------------------------
//   M4: 信息事件 -> KnowledgeFact + bitset
//   M6 F1: 主观解读层 (NPC weight 作为 lens 缩放 emotion delta)
//   M6 F3: Relation affinity 更新 (witness 反应同步改 affinity)
//   M6 F4: 14 类 action 完成事件 -> witness 反应矩阵
// =====================================================================

namespace Rinn::AppraisalSystem {

    inline Registry* g_reg = nullptr;
    inline std::unordered_map<uint16_t, Entity> fact_index;

    // ─── M6 v2: witness 反应钩子 (LineSystem 监听) ─────────
    using WitnessHook = void(*)(Entity witness, EventBus::EventType event_type, Entity actor);
    inline WitnessHook g_on_witness_react = nullptr;
    inline float WITNESS_LINE_THRESHOLD = 0.15f;  // emotion delta 大于这个才触发台词

    // ─── 工具: fact 查询 (M4) ─────────────────────────────────
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
        g_reg->emplace<KnowledgeFactComponent>(fact, fc);
        fact_index[static_cast<uint16_t>(t)] = fact;
        return fact;
    }

    // ─── F1: 主观解读因子 ─────────────────────────────────────
    //   lens = clamp(weight / 3.0, 0.3, 2.0)
    //   只乘到 emotion delta (expectation 是客观事实, 不缩放)
    inline float interpretation_factor(const NeedComponent& need, int primary_need_idx) {
        constexpr float REFERENCE_WEIGHT = 3.0f;
        if (primary_need_idx < 0 || primary_need_idx >= NeedComponent::N) return 1.0f;
        float raw = need.weights[primary_need_idx] / REFERENCE_WEIGHT;
        return std::clamp(raw, 0.3f, 2.0f);
    }

    // 每个 event 类型的"主导 need"——决定用哪个 weight 做 lens
    inline int primary_need_for_event(EventBus::EventType t) {
        using ET = EventBus::EventType;
        switch (t) {
            case ET::TaxIncreased:     return 0;   // 资源
            case ET::PriestDied:       return 4;   // 信仰 (Q2 决策)
            case ET::HeardLastWords:   return 2;   // 亲情
            case ET::BrokeDown:        return 3;   // 安全
            case ET::TaxRefused:       return 0;   // 资源
            case ET::ConfrontedLeader: return 1;   // 社交
            case ET::Hoarded:          return 0;   // 资源 (引发囤粮恐慌)
            case ET::AttendedFuneral:  return 2;   // 亲情
            case ET::VisitedFriend:    return 1;   // 社交
            case ET::WentToTavern:     return 1;   // 社交
            case ET::ConfidedGrief:    return 2;   // 亲情
            case ET::Prayed:           return 4;   // 信仰
            case ET::SankIntoGrief:    return 2;   // 亲情
            default:                   return 5;   // 其他: 好奇心 (低相关)
        }
    }

    // ─── F3: Relation affinity 操作 ─────────────────────────
    inline int affinity_from_to(Entity from, Entity to) {
        if (from.is_null() || to.is_null()) return 0;
        for (Entity edge : g_reg->view<RelationComponent>()) {
            auto& r = g_reg->get<RelationComponent>(edge);
            if (r.from.id == from.id && r.to.id == to.id) return r.affinity;
        }
        return 0;  // 无关系 = 中立
    }

    inline void update_affinity(Entity from, Entity to, int delta) {
        if (from.is_null() || to.is_null() || from.id == to.id) return;
        for (Entity edge : g_reg->view<RelationComponent>()) {
            auto& r = g_reg->get<RelationComponent>(edge);
            if (r.from.id == from.id && r.to.id == to.id) {
                int v = static_cast<int>(r.affinity) + delta;
                r.affinity = static_cast<int8_t>(std::clamp(v, -100, 100));
                return;
            }
        }
        // 没找到边 -> 不创建 (M6 简化, 稀疏图)
    }

    // ─── F4: 通用工具 - 旁观者反应 ─────────────────────────
    // C 方案: agency-aware appraisal — 一份事件对 actor / target / witness 走不同路径.
    //   actor (e.actor)    : 在 apply_witness_reaction 里被显式 skip (已有逻辑)
    //   target (e.target)  : 用 target_multiplier 替代 base_delta, 表示"被针对者"的反应模式
    //   witness (其他)     : 走原来的 base_delta * lens / affinity 路径
    // 当 target_multiplier = 1.0 时退化为旧行为, 兼容现有 reaction 表.
    struct WitnessReaction {
        int   emotion_idx;      // 0 怒 / 1 焦 / 2 恐 / 3 悲 / 4 孤
        float base_delta;
        bool  scale_by_affinity; // 是否按 affinity 缩放 (敌对放大)
        bool  apply_subjective_lens; // 是否过 weight 主观解读
        float target_multiplier = 1.0f; // 当 npc == e.target 时, base_delta 乘这个 (0=完全免疫, >1=被针对加剧)
    };

    inline void bump_emotion(EmotionComponent& emo, int idx, float delta) {
        if (idx < 0 || idx >= EmotionComponent::E) return;
        emo.intensity[idx] = std::clamp(emo.intensity[idx] + delta, 0.0f, 1.0f);
    }

    inline void force_redecide(Entity npc) {
        if (auto opt = g_reg->try_get<DecisionComponent>(npc); opt.has_value()) {
            opt->get().next_decision_tick = 0;
        }
    }

    // 通用 witness 反应函数 - 所有 action 完成事件复用
    // agency-aware: actor 跳过, target 走 target_multiplier, 其余 NPC 走原 lens/affinity 路径.
    inline void apply_witness_reaction(const EventBus::Event& e,
                                       std::initializer_list<WitnessReaction> reactions) {
        if (!g_reg) return;
        if (e.actor.is_null()) return;

        int primary = primary_need_for_event(e.type);

        for (Entity npc : g_reg->view<NeedComponent, EmotionComponent, StoneTabletComponent>()) {
            if (npc.id == e.actor.id) continue;
            auto& tablet = g_reg->get<StoneTabletComponent>(npc);
            if (!tablet.online) continue;

            auto& need = g_reg->get<NeedComponent>(npc);
            auto& emo  = g_reg->get<EmotionComponent>(npc);

            bool is_target = !e.target.is_null() && npc.id == e.target.id;

            float max_abs_delta = 0.0f;
            for (auto& r : reactions) {
                float delta;
                if (is_target) {
                    // target perspective: 用 target_multiplier 替代 base_delta,
                    // 不再叠加 affinity / lens (target 反应是"被针对"的特定模式, 不走通用主观解读)
                    delta = r.base_delta * r.target_multiplier;
                } else {
                    delta = r.base_delta;
                    if (r.scale_by_affinity) {
                        int aff = affinity_from_to(npc, e.actor);
                        delta *= (1.0f - aff / 200.0f);
                    }
                    if (r.apply_subjective_lens) {
                        delta *= interpretation_factor(need, primary);
                    }
                }
                max_abs_delta = std::max(max_abs_delta, std::abs(delta));
                bump_emotion(emo, r.emotion_idx, delta);
            }
            force_redecide(npc);

            // M6 v2: 反应足够强 -> 触发 witness 台词
            if (max_abs_delta > WITNESS_LINE_THRESHOLD && g_on_witness_react) {
                g_on_witness_react(npc, e.type, e.actor);
            }
        }
    }

    // ─── F1+M4: 信息事件 handler (Tax/Priest/HeardLast) ─────
    // C 方案核心: actor (e.actor==npc) 走 mirror perspective(-1),
    // 表示"我自己的决定不会反过来击穿我"——expectation 不下降, anger/anxiety 不上调.
    // 例: 村长加税, 自己 expectation +=, anger -=, anxiety -=.
    // 返回 max abs emotion delta (用于决定是否触发 witness 台词)
    inline float apply_appraisal_effects(Entity npc, const EventBus::Event& e) {
        using EventType = EventBus::EventType;
        auto need_opt = g_reg->try_get<NeedComponent>(npc);
        if (!need_opt.has_value()) return 0.0f;
        auto& need = need_opt->get();

        const float perspective =
            (!e.actor.is_null() && npc.id == e.actor.id) ? -1.0f : 1.0f;

        if (e.type == EventType::TaxIncreased) {
            need.expectation[0] -= 0.5f * std::max(0.1f, e.payload_f) * perspective;
        }

        int primary = primary_need_for_event(e.type);
        float lens = interpretation_factor(need, primary);

        float max_abs = 0.0f;
        if (auto emo_opt = g_reg->try_get<EmotionComponent>(npc); emo_opt.has_value()) {
            auto& emo = emo_opt->get();
            auto bump_track = [&](int idx, float delta) {
                float d = delta * perspective;
                max_abs = std::max(max_abs, std::abs(d));
                bump_emotion(emo, idx, d);
            };
            switch (e.type) {
                case EventType::TaxIncreased:
                    bump_track(0, 0.30f * lens);
                    bump_track(1, 0.50f * lens);
                    break;
                case EventType::PriestDied:
                    bump_track(3, 0.70f * lens);
                    bump_track(4, 0.40f * lens);
                    break;
                case EventType::HeardLastWords:
                    bump_track(3, 0.20f * lens);
                    break;
                default:
                    break;
            }
        }
        return max_abs;
    }

    inline void handle_information(const EventBus::Event& e) {
        if (!g_reg) return;
        bool can_broadcast = false;
        if (!e.actor.is_null() && g_reg->is_alive(e.actor)) {
            auto opt = g_reg->try_get<StoneTabletComponent>(e.actor);
            if (opt.has_value() && opt->get().online) can_broadcast = true;
        }
        if (!can_broadcast) return;

        Entity fact = find_or_create_fact(e.type, e.actor, 0);
        auto& fact_data = g_reg->get<KnowledgeFactComponent>(fact);

        for (Entity npc : g_reg->view<NeedComponent, StoneTabletComponent>()) {
            auto& tablet = g_reg->get<StoneTabletComponent>(npc);
            if (!tablet.online) continue;

            uint16_t idx = npc.index();
            if (!fact_data.knowers.test(idx)) {
                fact_data.knowers.set(idx);
            }
            float delta = apply_appraisal_effects(npc, e);
            force_redecide(npc);

            // M6 v2: 反应足够强 -> 触发 witness 台词 (信息事件路径)
            if (delta > WITNESS_LINE_THRESHOLD && g_on_witness_react) {
                g_on_witness_react(npc, e.type, e.actor);
            }
        }
    }

    // ─── F4: 14 个 action 完成事件 -> 旁观反应矩阵 ──────────

    // 愤怒系: 破坏与对抗 -> 旁观恐慌+愤怒+ affinity 下降
    inline void handle_broke_down(const EventBus::Event& e) {
        apply_witness_reaction(e, {
            {2 /*panic*/,  0.40f, true,  false},  // affinity-scaled
            {0 /*anger*/,  0.20f, false, true},   // 主观 lens (高安全 weight 的人更怒)
        });
        // affinity: 旁观者对 actor -5
        for (Entity npc : g_reg->view<NeedComponent, StoneTabletComponent>()) {
            if (npc.id == e.actor.id) continue;
            auto& tablet = g_reg->get<StoneTabletComponent>(npc);
            if (!tablet.online) continue;
            update_affinity(npc, e.actor, -5);
        }
    }

    inline void handle_tax_refused(const EventBus::Event& e) {
        // target = leader (被拒税者). 村长不会因为被拒税而 panic, 而是怒上加怒.
        apply_witness_reaction(e, {
            {2 /*panic*/,  0.30f, true,  false, /*target_mult*/ 0.0f},
            {0 /*anger*/,  0.15f, false, true,  /*target_mult*/ 3.0f},
        });
        for (Entity npc : g_reg->view<NeedComponent, StoneTabletComponent>()) {
            if (npc.id == e.actor.id) continue;
            update_affinity(npc, e.actor, -3);
        }
    }

    inline void handle_confronted_leader(const EventBus::Event& e) {
        // target = leader (被对抗者). 比普通 witness 焦虑加剧.
        apply_witness_reaction(e, {
            {1 /*anxiety*/, 0.20f, false, true, /*target_mult*/ 2.5f},
        });
    }

    // 焦虑系: hoard 引发资源恐慌
    inline void handle_hoarded(const EventBus::Event& e) {
        apply_witness_reaction(e, {
            {1 /*anxiety*/, 0.15f, false, true},  // 资源恐慌涟漪
        });
    }

    inline void handle_hid_money(const EventBus::Event& e) {
        // 私密动作, 微弱社交涟漪
        apply_witness_reaction(e, {
            {1 /*anxiety*/, 0.05f, false, false},
        });
    }

    inline void handle_prepaid_miners(const EventBus::Event& e) {
        // 经济信号, 旁观者轻微焦虑
        apply_witness_reaction(e, {
            {1 /*anxiety*/, 0.10f, false, false},
        });
    }

    // 恐慌系: 多为私密动作, 弱涟漪
    inline void handle_closed_doors(const EventBus::Event& e) {
        apply_witness_reaction(e, {
            {4 /*loneliness*/, 0.05f, false, false},  // 邻居闭门 -> 孤独+轻
        });
    }

    inline void handle_feigned_illness(const EventBus::Event& e) {
        // 几乎无可见涟漪
        (void)e;
    }

    inline void handle_flattered(const EventBus::Event& e) {
        // target = leader (被讨好者). 享受讨好 → anxiety 大降.
        apply_witness_reaction(e, {
            {1 /*anxiety*/, -0.05f, false, false, /*target_mult*/ 3.0f},
        });
    }

    // 孤独系: 接触型 -> 集体松弛
    inline void handle_visited_friend(const EventBus::Event& e) {
        // actor 自己受益, 旁观者也轻微感觉社区温暖
        apply_witness_reaction(e, {
            {4 /*loneliness*/, -0.10f, false, false},
        });
        // actor 自己孤独大幅下降 (额外加在 self)
        if (g_reg && !e.actor.is_null() && g_reg->is_alive(e.actor)) {
            if (auto opt = g_reg->try_get<EmotionComponent>(e.actor); opt.has_value()) {
                bump_emotion(opt->get(), 4, -0.20f);
            }
        }
    }

    inline void handle_went_to_tavern(const EventBus::Event& e) {
        // 集体场所 -> 在场所有人孤独下降
        apply_witness_reaction(e, {
            {4 /*loneliness*/, -0.15f, false, false},
        });
    }

    inline void handle_attended_funeral(const EventBus::Event& e) {
        // 集体哀悼 -> 旁观者悲伤+, 孤独- (聚集)
        apply_witness_reaction(e, {
            {3 /*sadness*/,    0.20f, false, true},
            {4 /*loneliness*/, -0.10f, false, false},
        });
    }

    // 悲伤系
    inline void handle_confided_grief(const EventBus::Event& e) {
        // actor 悲伤减轻 (self), 旁观者轻微共情
        if (g_reg && !e.actor.is_null() && g_reg->is_alive(e.actor)) {
            if (auto opt = g_reg->try_get<EmotionComponent>(e.actor); opt.has_value()) {
                bump_emotion(opt->get(), 3, -0.10f);
            }
        }
        apply_witness_reaction(e, {
            {3 /*sadness*/, 0.05f, false, false},  // 共情
        });
    }

    inline void handle_prayed(const EventBus::Event& e) {
        // 平静效应 (难得的负 delta)
        apply_witness_reaction(e, {
            {3 /*sadness*/,    -0.05f, false, false},
            {4 /*loneliness*/, -0.05f, false, false},
        });
    }

    inline void handle_sank_into_grief(const EventBus::Event& e) {
        // 自残式悲伤, 旁观者也被感染
        apply_witness_reaction(e, {
            {3 /*sadness*/, 0.05f, false, false},
        });
    }

    // ─── Init: 全部订阅 ─────────────────────────────────────
    inline void Init(Registry& reg) {
        g_reg = &reg;
        fact_index.clear();

        using ET = EventBus::EventType;

        // 信息事件
        EventBus::Subscribe(ET::TaxIncreased,     handle_information);
        EventBus::Subscribe(ET::PriestDied,       handle_information);
        EventBus::Subscribe(ET::HeardLastWords,   handle_information);

        // 愤怒系完成
        EventBus::Subscribe(ET::BrokeDown,        handle_broke_down);
        EventBus::Subscribe(ET::TaxRefused,       handle_tax_refused);
        EventBus::Subscribe(ET::ConfrontedLeader, handle_confronted_leader);

        // 焦虑系完成
        EventBus::Subscribe(ET::Hoarded,          handle_hoarded);
        EventBus::Subscribe(ET::HidMoney,         handle_hid_money);
        EventBus::Subscribe(ET::PrepaidMiners,    handle_prepaid_miners);

        // 恐慌系完成
        EventBus::Subscribe(ET::ClosedDoors,      handle_closed_doors);
        EventBus::Subscribe(ET::FeignedIllness,   handle_feigned_illness);
        EventBus::Subscribe(ET::Flattered,        handle_flattered);

        // 孤独系完成
        EventBus::Subscribe(ET::VisitedFriend,    handle_visited_friend);
        EventBus::Subscribe(ET::WentToTavern,     handle_went_to_tavern);
        EventBus::Subscribe(ET::AttendedFuneral,  handle_attended_funeral);

        // 悲伤系完成
        EventBus::Subscribe(ET::ConfidedGrief,    handle_confided_grief);
        EventBus::Subscribe(ET::Prayed,           handle_prayed);
        EventBus::Subscribe(ET::SankIntoGrief,    handle_sank_into_grief);
    }
}
