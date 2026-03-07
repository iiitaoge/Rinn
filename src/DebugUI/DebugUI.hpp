// DebugUI.hpp
#pragma once
#include "imgui.h"
#include "rlImGui.h"
#include "../Core/Registry.hpp"
#include "../Components/Components.hpp"

namespace Rinn::DebugUI {

    // 初始化imgui
    inline void Init() { rlImGuiSetup(true); }
    // 关闭imgui
    inline void Shutdown() { rlImGuiShutdown(); }

    // 绘制实体调试器
    inline void DrawEntityInspector(Registry& reg) {
        ImGui::Begin("Entity Inspector");

        // 每个组件类型一个 checkbox
        static bool show_transform = true;
        static bool show_velocity = false;
        static bool show_sprite = false;
        static bool show_collider = false;
        // ImGui::CheckboxFlags; // 横排勾选
        ImGui::Checkbox("Transform", &show_transform); ImGui::SameLine();
        ImGui::Checkbox("Velocity", &show_velocity);  ImGui::SameLine();
        ImGui::Checkbox("Sprite", &show_sprite);     ImGui::SameLine();
        ImGui::Checkbox("Collider", &show_collider);
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

        ImGui::End();
    }

    inline void Draw(Registry& reg) {
        rlImGuiBegin();
        // 所有面板都在这里
        DrawEntityInspector(reg);

        rlImGuiEnd();
    }

}