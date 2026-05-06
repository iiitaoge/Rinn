// DebugUI.hpp
#pragma once
#include "imgui.h"
#include "rlImGui.h"
#include "../Core/Registry.hpp"
#include "../Components/Components.hpp"

namespace Rinn::ComponentUI {

    // 初始化imgui
    inline void Init() { rlImGuiSetup(true); }
    // 关闭imgui
    inline void Shutdown() { rlImGuiShutdown(); }

    // 绘制实体调试器
    inline void DrawEntityInspector(Registry& reg) {

        // 每个组件类型一个 checkbox
        static bool show_transform = true;
        static bool show_velocity = false;
        static bool show_sprite = false;
        static bool show_collider = false;
        static bool show_need = true;
        static bool show_emotion = true;
        // ImGui::CheckboxFlags; // 横排勾选
        ImGui::Checkbox("Transform", &show_transform); ImGui::SameLine();
        ImGui::Checkbox("Velocity", &show_velocity);  ImGui::SameLine();
        ImGui::Checkbox("Sprite", &show_sprite);     ImGui::SameLine();
        ImGui::Checkbox("Collider", &show_collider); ImGui::SameLine();
        ImGui::Checkbox("Need", &show_need);
        ImGui::Checkbox("Emotion", &show_emotion);
        ImGui::Separator();
        // 每个勾选的组件 → 直接遍历它的 pool
        if (show_transform) {
            auto& pool = reg.pool<Transform>();
            if (ImGui::TreeNode("Transform", "Transform (%zu)", pool.size())) {
                for (size_t i = 0; i < pool.size(); ++i) {
                    Entity e = pool.raw_entity_data()[i];
                    auto& t = pool.raw_data()[i];
                    ImGui::Text("  [%d] (%.1f, %.1f)", e.index(), t.x, t.y);
                }
                ImGui::TreePop();
            }
        }
        if (show_velocity) {
            auto& pool = reg.pool<Velocity>();
            if (ImGui::TreeNode("Velocity", "Velocity (%zu)", pool.size())) {
                for (size_t i = 0; i < pool.size(); ++i) {
                    Entity e = pool.raw_entity_data()[i];
                    auto& v = pool.raw_data()[i];
                    ImGui::Text("  [%d] (%.1f, %.1f)", e.index(), v.vx, v.vy);
                }
                ImGui::TreePop();
            }
        }
        if (show_need) {
            auto& pool = reg.pool<NeedComponent>();
            if (ImGui::TreeNode("Need", "Need (%zu)", pool.size())) {
                constexpr const char* labels[NeedComponent::N] = {
                    "Resource", "Social", "Family", "Safety", "Faith", "Curiosity"
                };
                for (size_t i = 0; i < pool.size(); ++i) {
                    Entity e = pool.raw_entity_data()[i];
                    auto& need = pool.raw_data()[i];
                    if (ImGui::TreeNode(reinterpret_cast<void*>(static_cast<intptr_t>(e.id)), "Entity [%d]", e.index())) {
                        ImGui::Columns(4, "NeedColumns", false);
                        ImGui::Text("Need"); ImGui::NextColumn();
                        ImGui::Text("Weight"); ImGui::NextColumn();
                        ImGui::Text("Satisfaction"); ImGui::NextColumn();
                        ImGui::Text("Expectation"); ImGui::NextColumn();
                        ImGui::Separator();

                        for (int n = 0; n < NeedComponent::N; ++n) {
                            ImGui::Text("%s", labels[n]); ImGui::NextColumn();
                            ImGui::Text("%.1f", need.weights[n]); ImGui::NextColumn();
                            ImGui::Text("%.1f", need.satisfaction[n]); ImGui::NextColumn();
                            ImGui::Text("%.1f", need.expectation[n]); ImGui::NextColumn();
                        }

                        ImGui::Columns(1);
                        ImGui::TreePop();
                    }
                }
                ImGui::TreePop();
            }
        }
        if (show_emotion) {
            auto& pool = reg.pool<EmotionComponent>();
            if (ImGui::TreeNode("Emotion", "Emotion (%zu)", pool.size())) {
                constexpr const char* labels[EmotionComponent::E] = {
                    "Anger", "Anxiety", "Panic", "Sadness", "Loneliness"
                };
                for (size_t i = 0; i < pool.size(); ++i) {
                    Entity e = pool.raw_entity_data()[i];
                    auto& emotion = pool.raw_data()[i];
                    if (ImGui::TreeNode(reinterpret_cast<void*>(static_cast<intptr_t>(e.id)), "Entity [%d]", e.index())) {
                        ImGui::Columns(3, "EmotionColumns", false);
                        ImGui::Text("Emotion"); ImGui::NextColumn();
                        ImGui::Text("intensity"); ImGui::NextColumn();
                        ImGui::Text("decay_rate"); ImGui::NextColumn();
                        ImGui::Separator();

                        for (int e = 0; e < EmotionComponent::E; ++e) {
                            ImGui::Text("%s", labels[e]); ImGui::NextColumn();
                            ImGui::Text("%.1f", emotion.intensity[e]); ImGui::NextColumn();
                            ImGui::Text("%.1f", emotion.decay_rate[e]); ImGui::NextColumn();
                        }

                        ImGui::Columns(1);
                        ImGui::TreePop();
                    }
                }
                ImGui::TreePop();

            }

        }
    }

    inline void DrawEntityPanel(Registry& reg) {
        ImGui::Begin("Entity Inspector");
        // 所有面板都在这里
        DrawEntityInspector(reg);

        ImGui::End();
    }

}
