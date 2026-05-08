#pragma once
#include "imgui.h"
#include "../Core/Registry.hpp"
#include "../Components/Components.hpp"
#include "../Systems/EventSystem.hpp"
#include "../Systems/AppraisalSystem.hpp"
#include "../Systems/DecisionSystem.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <vector>

// =====================================================================
// AI Debug Infra — M3 前置工具集
// =====================================================================
//   TimeControl  : 暂停 / 单步 / 倍速 (包装 dt)
//   EventLog     : 环形缓冲事件日志 (M3 EventBus 钩到这里)
//   NpcInspector : 按实体而非组件展示 AI 状态
// 三个模块各自独立，main.cpp 调一行 AIDebugUI::Draw(reg) 全画。
// =====================================================================

namespace Rinn::TimeControl {

    inline bool  paused              = false;
    inline bool  single_step_request = false;
    inline float speed_multiplier    = 1.0f;
    inline float last_dt             = 0.0f;  // 上一帧实际喂给 System 的 dt
    inline float game_time           = 0.0f;  // 累计游戏时间 (暂停帧不累加)

    // 每帧调一次。real_dt = RenderSystem::DeltaTime()。
    // 返回值喂给所有 dt-based System (Physic / EmotionDecay / ...)。
    inline float Tick(float real_dt) {
        float dt;
        if (paused) {
            if (single_step_request) {
                dt = real_dt;
                single_step_request = false;
            } else {
                dt = 0.0f;
            }
        } else {
            dt = real_dt * speed_multiplier;
        }
        last_dt    = dt;
        game_time += dt;
        return dt;
    }

    inline void Draw() {
        ImGui::Begin("Time Control");
        if (ImGui::Button(paused ? "Resume" : "Pause")) {
            paused = !paused;
        }
        ImGui::SameLine();
        if (ImGui::Button("Step")) {
            paused = true;
            single_step_request = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(pause, then step 1 frame)");

        ImGui::SliderFloat("Speed x", &speed_multiplier, 0.0f, 4.0f, "%.2f");
        ImGui::Separator();
        ImGui::Text("game_dt:   %.4f s", last_dt);
        ImGui::Text("game_time: %.2f s", game_time);
        ImGui::End();
    }
}

namespace Rinn::EventLog {

    enum class Level : uint8_t { Info = 0, Warn = 1, Error = 2 };

    struct Entry {
        float t;              // game_time at push
        Level level;
        char  source[16];
        char  msg[112];
    };

    constexpr size_t CAPACITY = 256;
    inline std::array<Entry, CAPACITY> ring{};
    inline size_t head  = 0;     // 下一个写入位
    inline size_t count = 0;     // 有效条数 (上限 CAPACITY)

    inline void Push(Level level, const char* source, const char* msg) {
        Entry& e = ring[head];
        e.t     = TimeControl::game_time;
        e.level = level;
        std::strncpy(e.source, source, sizeof(e.source) - 1);
        e.source[sizeof(e.source) - 1] = '\0';
        std::strncpy(e.msg, msg, sizeof(e.msg) - 1);
        e.msg[sizeof(e.msg) - 1] = '\0';
        head = (head + 1) % CAPACITY;
        if (count < CAPACITY) ++count;
    }

    // printf 风格便利接口
    inline void PushFmt(Level level, const char* source, const char* fmt, ...) {
        char buf[112];
        va_list args;
        va_start(args, fmt);
        std::vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        Push(level, source, buf);
    }

    inline void Clear() {
        head  = 0;
        count = 0;
    }

    inline void Draw() {
        // 首次绘制塞两条欢迎日志, 用来确认管线通了
        static bool first_draw = true;
        if (first_draw) {
            Push(Level::Info, "EventLog", "Initialized.");
            Push(Level::Info, "EventLog", "Waiting for EventBus (M3) to push events.");
            first_draw = false;
        }

        static bool show_info     = true;
        static bool show_warn     = true;
        static bool show_error    = true;
        static bool autoscroll    = true;

        ImGui::Begin("Event Log");
        ImGui::Checkbox("Info",  &show_info);   ImGui::SameLine();
        ImGui::Checkbox("Warn",  &show_warn);   ImGui::SameLine();
        ImGui::Checkbox("Error", &show_error);  ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &autoscroll); ImGui::SameLine();
        if (ImGui::Button("Clear")) Clear();
        ImGui::SameLine();
        if (ImGui::Button("Test Push")) {
            PushFmt(Level::Info,  "TEST", "info  @ %.2fs", TimeControl::game_time);
            PushFmt(Level::Warn,  "TEST", "warn  @ %.2fs", TimeControl::game_time);
            PushFmt(Level::Error, "TEST", "error @ %.2fs", TimeControl::game_time);
        }
        ImGui::Separator();

        ImGui::BeginChild("LogScroll", ImVec2(0, 0), false,
                          ImGuiWindowFlags_HorizontalScrollbar);

        // 从最旧到最新遍历环形缓冲
        size_t start = (count == CAPACITY) ? head : 0;
        for (size_t i = 0; i < count; ++i) {
            const Entry& e = ring[(start + i) % CAPACITY];
            bool show =
                (e.level == Level::Info  && show_info ) ||
                (e.level == Level::Warn  && show_warn ) ||
                (e.level == Level::Error && show_error);
            if (!show) continue;

            ImVec4 color =
                e.level == Level::Error ? ImVec4(1.0f, 0.4f, 0.4f, 1.0f) :
                e.level == Level::Warn  ? ImVec4(1.0f, 0.85f, 0.3f, 1.0f) :
                                          ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
            ImGui::TextColored(color, "[%7.2f] %-10s %s", e.t, e.source, e.msg);
        }

        if (autoscroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f) {
            ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
        ImGui::End();
    }
}

namespace Rinn::NpcInspector {

    inline Entity selected{};   // 默认 null

    namespace detail {
        inline void DrawNeed(const NeedComponent& n) {
            if (!ImGui::TreeNode("Need")) return;
            constexpr const char* labels[NeedComponent::N] = {
                "Resource", "Social", "Family", "Safety", "Faith", "Curiosity"
            };
            ImGui::Columns(4, "InspectorNeedCols", false);
            ImGui::Text("Need");   ImGui::NextColumn();
            ImGui::Text("Weight"); ImGui::NextColumn();
            ImGui::Text("Sat.");   ImGui::NextColumn();
            ImGui::Text("Exp.");   ImGui::NextColumn();
            ImGui::Separator();
            for (int i = 0; i < NeedComponent::N; ++i) {
                ImGui::Text("%s",   labels[i]);          ImGui::NextColumn();
                ImGui::Text("%.1f", n.weights[i]);       ImGui::NextColumn();
                ImGui::Text("%.1f", n.satisfaction[i]);  ImGui::NextColumn();
                ImGui::Text("%.1f", n.expectation[i]);   ImGui::NextColumn();
            }
            ImGui::Columns(1);
            ImGui::TreePop();
        }

        inline void DrawEmotion(const EmotionComponent& e) {
            if (!ImGui::TreeNode("Emotion")) return;
            constexpr const char* labels[EmotionComponent::E] = {
                "Anger", "Anxiety", "Panic", "Sadness", "Loneliness"
            };
            ImGui::Columns(4, "InspectorEmotionCols", false);
            ImGui::Text("Emotion");   ImGui::NextColumn();
            ImGui::Text("Intensity"); ImGui::NextColumn();
            ImGui::Text("Decay/s");   ImGui::NextColumn();
            ImGui::Text("Target");    ImGui::NextColumn();
            ImGui::Separator();
            for (int i = 0; i < EmotionComponent::E; ++i) {
                ImGui::Text("%s",   labels[i]);        ImGui::NextColumn();
                ImGui::Text("%.2f", e.intensity[i]);   ImGui::NextColumn();
                ImGui::Text("%.2f", e.decay_rate[i]);  ImGui::NextColumn();
                if (e.target[i].is_null()) ImGui::Text("-");
                else                       ImGui::Text("E%d", e.target[i].index());
                ImGui::NextColumn();
            }
            ImGui::Columns(1);
            ImGui::TreePop();
        }

        // M5: 当前 action + progress + 上一次决策的 utility 分数表
        inline void DrawDecision(Entity npc, const DecisionComponent& d) {
            if (!ImGui::TreeNode("Decision")) return;

            const auto& cat = DecisionSystem::action_catalog;
            const char* cur_name = (d.current_action_id < cat.size())
                ? cat[d.current_action_id].name : "<invalid>";

            ImGui::Text("Current action:    %s (id=%u)", cur_name, d.current_action_id);
            ImGui::Text("Progress:          %.2f", d.action_progress);
            ImGui::Text("Next decision tick %u (now %u)",
                        d.next_decision_tick, DecisionSystem::global_tick);

            ImGui::Separator();
            ImGui::Text("Last utility scores:");

            auto it = DecisionSystem::last_scores.find(npc.id);
            if (it == DecisionSystem::last_scores.end() || it->second.empty()) {
                ImGui::TextDisabled("(no decision yet)");
                ImGui::TreePop();
                return;
            }
            const auto& scores = it->second;
            int chosen = -1;
            if (auto cit = DecisionSystem::last_chosen.find(npc.id);
                cit != DecisionSystem::last_chosen.end()) chosen = cit->second;

            ImGui::Columns(2, "InspectorActionCols", false);
            ImGui::Text("Action");  ImGui::NextColumn();
            ImGui::Text("Utility"); ImGui::NextColumn();
            ImGui::Separator();
            for (size_t i = 0; i < scores.size() && i < cat.size(); ++i) {
                if (static_cast<int>(i) == chosen) {
                    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s *", cat[i].name);
                } else {
                    ImGui::Text("%s", cat[i].name);
                }
                ImGui::NextColumn();
                ImGui::Text("%.3f", scores[i]);
                ImGui::NextColumn();
            }
            ImGui::Columns(1);
            ImGui::TreePop();
        }
    }

    inline void Draw(Registry& reg) {
        ImGui::Begin("NPC Inspector");

        // 左侧：拥有 NeedComponent 的实体列表 (= NPC + Player)
        ImGui::BeginChild("NpcList", ImVec2(150, 0), true);
        auto& need_pool = reg.pool<NeedComponent>();
        if (need_pool.size() == 0) {
            ImGui::TextDisabled("(no NPCs)");
        }
        for (size_t i = 0; i < need_pool.size(); ++i) {
            Entity e = need_pool.raw_entity_data()[i];
            char label[32];
            std::snprintf(label, sizeof(label), "Entity %d", e.index());
            if (ImGui::Selectable(label, selected.id == e.id)) {
                selected = e;
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // 右侧：选中实体的全部 AI 组件
        ImGui::BeginChild("NpcDetail");
        if (!selected.is_null() && reg.is_alive(selected)) {
            ImGui::Text("Entity %d  (gen %d)",
                        selected.index(), selected.generation());
            ImGui::Separator();

            if (auto opt = reg.try_get<NeedComponent>(selected); opt.has_value()) {
                detail::DrawNeed(opt->get());
            }
            if (auto opt = reg.try_get<EmotionComponent>(selected); opt.has_value()) {
                detail::DrawEmotion(opt->get());
            }
            if (auto opt = reg.try_get<DecisionComponent>(selected); opt.has_value()) {
                detail::DrawDecision(selected, opt->get());
            }
        } else {
            ImGui::TextDisabled("← Select an NPC on the left");
        }
        ImGui::EndChild();

        ImGui::End();
    }
}

// =====================================================================
// DemoVillage - M4 验证场景: 1 leader + 3 NPC, 全部带 NeedComponent +
// EmotionComponent + StoneTabletComponent. 用于跑加税广播 demo.
// =====================================================================
namespace Rinn::DemoVillage {

    inline std::vector<Entity> npcs;     // [0] = leader, 其余是 receiver
    inline bool already_setup = false;

    inline void Setup(Registry& reg) {
        if (already_setup) return;
        already_setup = true;
        npcs.clear();

        auto make_npc = [&](std::array<float, 6> w,
                            std::array<float, 6> sat,
                            std::array<float, 6> exp,
                            bool tablet_online) -> Entity {
            Entity e = reg.create_entity();
            reg.emplace<NeedComponent>(e, NeedComponent{ w, sat, exp });
            reg.emplace<EmotionComponent>(e, EmotionComponent{
                {0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
                {Entity{}, Entity{}, Entity{}, Entity{}, Entity{}},
                {0.10f, 0.10f, 0.20f, 0.05f, 0.05f}
            });
            reg.emplace<StoneTabletComponent>(e, StoneTabletComponent{ tablet_online, Entity{}, 100 });
            reg.emplace<DecisionComponent>(e, DecisionComponent{
                Entity{},   // current_action_target
                0,          // current_action_id (idle)
                1.0f,       // action_progress (1.0 = 等待重决策)
                0           // next_decision_tick (立即决策)
            });
            return e;
        };

        // [0] leader: 高安全权重 + 安全缺口 -> 倾向 hoard
        npcs.push_back(make_npc({3,2,3,5,2,1}, {5,4,4,3,4,2}, {5,4,4,5,4,2}, true));
        // [1] blacksmith: 高资源权重 + 资源缺口 -> 倾向 break_tablet (怒火加成)
        npcs.push_back(make_npc({5,3,4,2,1,2}, {3,3,4,3,1,3}, {5,3,4,3,1,3}, true));
        // [2] priest: 高信仰权重 + 信仰缺口 -> 倾向 pray
        npcs.push_back(make_npc({2,4,2,2,5,2}, {3,3,3,3,3,3}, {3,3,3,3,5,3}, true));
        // [3] hermit: 高安全 + 离线 (验证: 不会收到加税, utility 不变)
        npcs.push_back(make_npc({3,1,2,4,3,1}, {3,2,3,2,3,2}, {3,2,3,4,3,2}, false));
    }

    inline Entity leader() { return npcs.empty() ? Entity{} : npcs[0]; }
}

namespace Rinn::EventBusInspector {

    inline void Draw(Registry& reg) {
        ImGui::Begin("EventBus");

        ImGui::Text("queue size:      %zu",  EventBus::queue.size());
        ImGui::Text("drained / frame: %zu",  EventBus::last_drain_count);
        ImGui::Text("total published: %zu",  EventBus::total_published);
        ImGui::Text("typed subs:      %zu",  EventBus::subscribers.size());
        ImGui::Text("wildcard subs:   %zu",  EventBus::wildcard_subscribers.size());

        if (EventBus::last_drain_overflow) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                               "OVERFLOW: drain hit MAX_DRAIN_PER_FRAME (cycle?)");
        }

        ImGui::Separator();

        // M4 demo: 一键拉起测试村庄
        if (!DemoVillage::already_setup) {
            if (ImGui::Button("Setup Demo Village (M4)")) {
                DemoVillage::Setup(reg);
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(spawn 4 NPCs: leader + 2 online + 1 offline hermit)");
        } else {
            ImGui::Text("Demo village ready: %zu NPCs, leader = E%u",
                        DemoVillage::npcs.size(),
                        DemoVillage::leader().is_null() ? 0u
                            : (unsigned)DemoVillage::leader().index());
        }

        // M4 demo: 触发加税广播 (用 leader 做 actor)
        bool can_broadcast = !DemoVillage::leader().is_null();
        if (!can_broadcast) ImGui::BeginDisabled();
        if (ImGui::Button("Tax Raise (10%)")) {
            EventBus::Publish(EventBus::Event{
                EventBus::EventType::TaxIncreased,
                DemoVillage::leader(), {}, 0.10f, 0
            });
        }
        if (!can_broadcast) ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("(broadcasts via leader's tablet)");

        if (ImGui::Button("Priest Died")) {
            EventBus::Publish(EventBus::Event{
                EventBus::EventType::PriestDied,
                DemoVillage::leader(), {}, 0.0f, 0
            });
        }

        ImGui::End();
    }
}

// =====================================================================
// KnowledgeInspector - M4 主验证面板
// 列出所有 KnowledgeFact entity + 每条 fact 的 knowers (谁知道)
// =====================================================================
namespace Rinn::KnowledgeInspector {

    inline void Draw(Registry& reg) {
        ImGui::Begin("Knowledge");

        auto& fact_pool = reg.pool<KnowledgeFactComponent>();
        ImGui::Text("Active facts: %zu", fact_pool.size());
        ImGui::Text("fact_index size: %zu", AppraisalSystem::fact_index.size());
        ImGui::Separator();

        if (fact_pool.size() == 0) {
            ImGui::TextDisabled("(no facts yet — trigger a broadcast)");
            ImGui::End();
            return;
        }

        for (size_t i = 0; i < fact_pool.size(); ++i) {
            Entity fact_e = fact_pool.raw_entity_data()[i];
            auto& fc = reg.get<KnowledgeFactComponent>(fact_e);

            const char* name = EventBus::TypeName(static_cast<EventBus::EventType>(fc.fact_type));
            ImGui::PushID(static_cast<int>(fact_e.id));

            if (ImGui::TreeNode(name, "%s  [E%u]  subject=E%d  knowers=%zu",
                    name, fact_e.index(),
                    fc.subject.is_null() ? -1 : (int)fc.subject.index(),
                    fc.knowers.count())) {

                ImGui::Text("Knowers (by Entity index):");
                ImGui::BeginChild("knowers", ImVec2(0, 80), true);
                bool any = false;
                for (size_t b = 0; b < fc.knowers.size(); ++b) {
                    if (fc.knowers.test(b)) {
                        if (any) ImGui::SameLine();
                        ImGui::Text("E%zu", b);
                        any = true;
                    }
                }
                if (!any) ImGui::TextDisabled("(nobody yet)");
                ImGui::EndChild();
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        ImGui::End();
    }
}

namespace Rinn::AIDebugUI {
    // 一次性绘制所有面板, 主循环只调这个
    inline void Draw(Registry& reg) {
        TimeControl::Draw();
        EventLog::Draw();
        EventBusInspector::Draw(reg);
        KnowledgeInspector::Draw(reg);
        NpcInspector::Draw(reg);
    }
}
