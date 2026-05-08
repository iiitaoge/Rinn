#pragma once
#include "imgui.h"
#include "../Core/Registry.hpp"
#include "../Components/Components.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <cstdarg>

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
            // M3-M5 加新组件:
            // if (auto opt = reg.try_get<DecisionComponent>(selected); opt.has_value())
            //     detail::DrawDecision(opt->get());
            // ...
        } else {
            ImGui::TextDisabled("← Select an NPC on the left");
        }
        ImGui::EndChild();

        ImGui::End();
    }
}

namespace Rinn::AIDebugUI {
    // 一次性绘制三个面板, 主循环只调这个
    inline void Draw(Registry& reg) {
        TimeControl::Draw();
        EventLog::Draw();
        NpcInspector::Draw(reg);
    }
}
