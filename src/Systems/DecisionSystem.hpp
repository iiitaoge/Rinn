#pragma once
#include "../Core/Registry.hpp"
#include "../Components/Components.hpp"
#include "EventSystem.hpp"
#include <algorithm>
#include <array>
#include <string>
#include <unordered_map>
#include <vector>

// =====================================================================
// DecisionSystem - M5
// ---------------------------------------------------------------------
// Utility AI:  utility(a) = gain(a) * salience(need) * emotion_modulator(a)
//   salience(need) = weight * max(0, expectation - satisfaction)
//   emotion_modulator(a) = 1 + sum( emotion.intensity[i] * a.modulators[i] )
//
// 每 N tick 重决策一次 (DecisionComponent.next_decision_tick 控制).
// 中断: AppraisalSystem 在情绪 delta 后置 next_decision_tick=0 触发立即重决.
// =====================================================================

namespace Rinn::DecisionSystem {

    // ActionDef = action catalog 一项. 这块是数据, M6 会迁移到 Lua (scripts/ai/actions.lua).
    // 字段语义:
    //   gain_need_idx: 主要服务于哪个 need (NeedComponent 索引)
    //                  0 资源 / 1 社交 / 2 亲情 / 3 安全 / 4 信仰 / 5 好奇心
    //   gain         : 完成时给 satisfaction 的基础增量
    //   modulators[5]: 情绪调制 (0 怒 1 焦 2 恐 3 悲 4 孤)
    //   duration     : 执行秒数
    //   target_kind  : action 完成事件的 target 解析方式 (agency-aware 处理依赖)
    enum class TargetKind : uint8_t {
        NoTarget,   // 不指涉具体对象 (idle, hoard, pray ...)
        Leader,     // 指向当前 IsLeader-tagged 实体 (refuse_tax, confront_leader, flatter)
        Self,       // 指向 actor 自己
    };

    struct ActionDef {
        const char* name;
        int   gain_need_idx;
        float gain;
        std::array<float, EmotionComponent::E> modulators;
        float duration;
        EventBus::EventType complete_event;  // None 表示无副作用
        TargetKind target_kind;
    };

    // 默认 catalog (M6 改成 Lua 加载)
    // 4 类情绪 (chat log 1082-1086) × 3 变种 + 悲伤 3 + idle = 16 actions
    // modulators 索引: 0 怒 / 1 焦 / 2 恐 / 3 悲 / 4 孤
    // duration 大幅延长 (5-10s) 避免动作刷屏 + 给台词留呼吸时间
    inline std::vector<ActionDef> action_catalog = {
        // ─── 默认 ─────────────────────────────────────────
        { "idle",              5,  0.05f, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f},   3.0f, EventBus::EventType::None,             TargetKind::NoTarget },

        // ─── 愤怒型 (3) ───────────────────────────────────
        { "break_tablet",      0,  0.30f, {3.0f, 0.0f, 0.0f, 0.0f, 0.0f},   8.0f, EventBus::EventType::BrokeDown,        TargetKind::NoTarget },
        { "refuse_tax",        0,  0.40f, {2.5f, 0.5f, 0.0f, 0.0f, 0.0f},   6.0f, EventBus::EventType::TaxRefused,       TargetKind::Leader   },
        { "confront_leader",   1,  0.40f, {2.0f, 0.5f, 0.0f, 0.0f, 0.0f},   6.0f, EventBus::EventType::ConfrontedLeader, TargetKind::Leader   },

        // ─── 焦虑型 (3) ───────────────────────────────────
        { "hoard_resources",   3,  0.50f, {0.0f, 2.0f, 1.5f, 0.0f, 0.0f},   6.0f, EventBus::EventType::Hoarded,          TargetKind::NoTarget },
        { "hide_money",        3,  0.40f, {0.0f, 2.5f, 1.0f, 0.0f, 0.0f},   5.0f, EventBus::EventType::HidMoney,         TargetKind::NoTarget },
        { "prepay_miners",     3,  0.45f, {0.0f, 2.0f, 0.0f, 0.0f, 0.0f},   6.0f, EventBus::EventType::PrepaidMiners,    TargetKind::NoTarget },

        // ─── 恐慌型 (3) ───────────────────────────────────
        { "close_doors",       3,  0.35f, {0.0f, 0.5f, 2.5f, 0.0f, 0.0f},   4.0f, EventBus::EventType::ClosedDoors,      TargetKind::NoTarget },
        { "feign_illness",     3,  0.30f, {0.0f, 0.0f, 2.0f, 0.5f, 0.0f},   8.0f, EventBus::EventType::FeignedIllness,   TargetKind::NoTarget },
        { "flatter",           1,  0.30f, {0.0f, 0.0f, 2.5f, 0.0f, 0.5f},   5.0f, EventBus::EventType::Flattered,        TargetKind::Leader   },

        // ─── 孤独型 (3) ───────────────────────────────────
        { "visit_friend",      1,  0.40f, {0.0f, 0.0f, 0.0f, 0.5f, 2.0f},   6.0f, EventBus::EventType::VisitedFriend,    TargetKind::NoTarget },
        { "go_tavern",         1,  0.45f, {0.0f, 0.0f, 0.0f, 0.5f, 2.5f},   8.0f, EventBus::EventType::WentToTavern,     TargetKind::NoTarget },
        { "attend_funeral",    1,  0.30f, {0.0f, 0.0f, 0.0f, 1.5f, 2.0f},  10.0f, EventBus::EventType::AttendedFuneral,  TargetKind::NoTarget },

        // ─── 悲伤型 (3) ───────────────────────────────────
        { "confide_grief",     2,  0.35f, {0.0f, 0.0f, 0.0f, 2.0f, 0.5f},   6.0f, EventBus::EventType::ConfidedGrief,    TargetKind::NoTarget },
        { "pray",              4,  0.40f, {0.0f, 0.0f, 0.0f, 0.5f, 0.5f},   8.0f, EventBus::EventType::Prayed,           TargetKind::NoTarget },
        { "sink_into_grief",   2,  0.20f, {0.0f, 0.0f, 0.0f, 2.5f, 1.0f},  10.0f, EventBus::EventType::SankIntoGrief,    TargetKind::NoTarget },
    };

    // ─── M6 v2: action 选定钩子 (LineSystem 用此驱动同步台词) ───
    using ActionChosenHook = void(*)(Entity actor, EventBus::EventType complete_event);
    inline ActionChosenHook g_on_action_chosen = nullptr;

    inline uint32_t global_tick = 0;
    inline uint32_t REDECIDE_INTERVAL_TICKS = 60;  // ~1s @ 60fps

    // 调试: 记录每个 NPC 上一次决策时各 action 的 utility 分数
    inline std::unordered_map<uint32_t, std::vector<float>> last_scores;
    inline std::unordered_map<uint32_t, int>                last_chosen;

    inline float compute_utility(const NeedComponent& need,
                                 const EmotionComponent& emo,
                                 const ActionDef& a,
                                 const ActionBiasComponent* bias_opt,
                                 size_t action_id) {
        int ni = a.gain_need_idx;
        if (ni < 0 || ni >= NeedComponent::N) return 0.0f;

        float gap = std::max(0.0f, need.expectation[ni] - need.satisfaction[ni]);
        float salience = need.weights[ni] * gap;

        float modulator = 1.0f;  // base = 1 (没情绪也不归零)
        for (int i = 0; i < EmotionComponent::E; ++i) {
            modulator += emo.intensity[i] * a.modulators[i];
        }

        float base = a.gain * salience * modulator;

        // L1: 持久 utility 偏置 (剧本/设计师注入, 算积分一部分)
        if (bias_opt && action_id < ActionBiasComponent::MAX_ACTIONS) {
            base += bias_opt->bias[action_id];
        }
        return base;
    }

    // 强制 NPC 立即重决策 (AppraisalSystem 中断时调)
    inline void RequestRedecide(Registry& reg, Entity e) {
        if (auto opt = reg.try_get<DecisionComponent>(e); opt.has_value()) {
            opt->get().next_decision_tick = 0;
        }
    }

    inline void Update(Registry& reg, float dt) {
        (void)dt;
        ++global_tick;

        for (Entity e : reg.view<NeedComponent, EmotionComponent, DecisionComponent>()) {
            auto& dec = reg.get<DecisionComponent>(e);
            if (global_tick < dec.next_decision_tick) continue;

            auto& need = reg.get<NeedComponent>(e);
            auto& emo  = reg.get<EmotionComponent>(e);
            const ActionBiasComponent* bias_ptr = nullptr;
            if (auto opt = reg.try_get<ActionBiasComponent>(e); opt.has_value()) {
                bias_ptr = &opt->get();
            }

            float best_u   = -1.0f;
            int   best_id  = 0;

            std::vector<float>& scores = last_scores[e.id];
            scores.assign(action_catalog.size(), 0.0f);

            for (size_t i = 0; i < action_catalog.size(); ++i) {
                float u = compute_utility(need, emo, action_catalog[i], bias_ptr, i);
                scores[i] = u;
                if (u > best_u) {
                    best_u  = u;
                    best_id = static_cast<int>(i);
                }
            }

            // 切动作 -> 重置 progress.  同动作但已完成 -> 也重置以重新执行
            bool changed = (dec.current_action_id != static_cast<uint16_t>(best_id));
            bool restarted = (!changed && dec.action_progress >= 1.0f);
            dec.current_action_id = static_cast<uint16_t>(best_id);
            if (changed || restarted) {
                dec.action_progress = 0.0f;
            }
            last_chosen[e.id] = best_id;

            // 动作选定时立即触发台词钩子 (LineSystem 监听)
            // 只在 action 真正切换或重启时触发, 避免每次重决策都说话
            if ((changed || restarted) && g_on_action_chosen) {
                g_on_action_chosen(e, action_catalog[best_id].complete_event);
            }

            dec.next_decision_tick = global_tick + REDECIDE_INTERVAL_TICKS;
        }
    }
}
