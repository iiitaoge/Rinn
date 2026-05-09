#pragma once
#include "../Core/Types.hpp"
#include <cstdint>
#include <functional>
#include <queue>
#include <unordered_map>
#include <vector>

// =====================================================================
// EventBus - M3
// ---------------------------------------------------------------------
// 两个动词:
//   Subscribe(type, handler) - subscriber init 时调, 把 handler 存进 bus
//   Publish(event)           - publisher 触发时调, 入队
// 主循环每帧一次:
//   Drain()                  - FIFO drain 到空, dispatch 给已注册 handler
//
// 确定性: FIFO 入/出 + 同帧 drain 到空
// 边界: cycle guard (MAX_DRAIN_PER_FRAME) 防 handler 内 publish 死循环
// =====================================================================

namespace Rinn::EventBus {

    enum class EventType : uint16_t {
        None = 0,
        // ─── 信息事件 (M4) ──────────────────────────────
        TaxIncreased,
        PriestDied,
        HeardLastWords,
        // ─── 愤怒系动作完成 ───────────────────────────────
        BrokeDown,           // break_tablet
        TaxRefused,          // refuse_tax
        ConfrontedLeader,    // confront_leader
        // ─── 焦虑系动作完成 ───────────────────────────────
        Hoarded,             // hoard_resources
        HidMoney,            // hide_money
        PrepaidMiners,       // prepay_miners
        // ─── 恐慌系动作完成 ───────────────────────────────
        ClosedDoors,         // close_doors
        FeignedIllness,      // feign_illness
        Flattered,           // flatter
        // ─── 孤独系动作完成 ───────────────────────────────
        VisitedFriend,       // visit_friend
        WentToTavern,        // go_tavern
        AttendedFuneral,     // attend_funeral
        // ─── 悲伤系动作完成 ───────────────────────────────
        ConfidedGrief,       // confide_grief
        Prayed,              // pray
        SankIntoGrief,       // sink_into_grief
    };

    struct Event {
        EventType type      = EventType::None;
        Entity    actor     = {};   // 谁触发的
        Entity    target    = {};   // 影响谁
        float     payload_f = 0.0f;
        int32_t   payload_i = 0;
    };

    using Handler = std::function<void(const Event&)>;

    inline std::unordered_map<EventType, std::vector<Handler>> subscribers;
    inline std::vector<Handler>                                wildcard_subscribers;
    inline std::queue<Event>                                   queue;

    inline size_t last_drain_count    = 0;
    inline size_t total_published     = 0;
    inline bool   last_drain_overflow = false;

    constexpr int MAX_DRAIN_PER_FRAME = 1024;

    inline void Subscribe(EventType type, Handler h) {
        subscribers[type].push_back(std::move(h));
    }

    inline void SubscribeAll(Handler h) {
        wildcard_subscribers.push_back(std::move(h));
    }

    inline void Publish(const Event& e) {
        queue.push(e);
        ++total_published;
    }

    inline void Drain() {
        last_drain_count    = 0;
        last_drain_overflow = false;

        while (!queue.empty()) {
            if (last_drain_count >= MAX_DRAIN_PER_FRAME) {
                last_drain_overflow = true;
                break;
            }

            Event e = queue.front();
            queue.pop();
            ++last_drain_count;

            auto it = subscribers.find(e.type);
            if (it != subscribers.end()) {
                for (auto& h : it->second) h(e);
            }
            for (auto& h : wildcard_subscribers) h(e);
        }
    }

    inline void Clear() {
        subscribers.clear();
        wildcard_subscribers.clear();
        std::queue<Event> empty;
        std::swap(queue, empty);
        last_drain_count    = 0;
        total_published     = 0;
        last_drain_overflow = false;
    }

    inline const char* TypeName(EventType t) {
        switch (t) {
            case EventType::None:             return "None";
            case EventType::TaxIncreased:     return "TaxIncreased";
            case EventType::PriestDied:       return "PriestDied";
            case EventType::HeardLastWords:   return "HeardLastWords";
            case EventType::BrokeDown:        return "BrokeDown";
            case EventType::TaxRefused:       return "TaxRefused";
            case EventType::ConfrontedLeader: return "ConfrontedLeader";
            case EventType::Hoarded:          return "Hoarded";
            case EventType::HidMoney:         return "HidMoney";
            case EventType::PrepaidMiners:    return "PrepaidMiners";
            case EventType::ClosedDoors:      return "ClosedDoors";
            case EventType::FeignedIllness:   return "FeignedIllness";
            case EventType::Flattered:        return "Flattered";
            case EventType::VisitedFriend:    return "VisitedFriend";
            case EventType::WentToTavern:     return "WentToTavern";
            case EventType::AttendedFuneral:  return "AttendedFuneral";
            case EventType::ConfidedGrief:    return "ConfidedGrief";
            case EventType::Prayed:           return "Prayed";
            case EventType::SankIntoGrief:    return "SankIntoGrief";
        }
        return "<unknown>";
    }
}
