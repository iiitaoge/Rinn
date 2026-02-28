// DebugUI.hpp
#pragma once
#include "imgui.h"
#include "rlImGui.h"
#include "../Core/Registry.hpp"
#include "../Components/Components.hpp"

namespace Rinn::DebugUI {

    inline void Init() { rlImGuiSetup(true); }
    inline void Shutdown() { /* rlImGuiShutdown() */ }

    inline void DrawEntityInspector(Registry& reg) {
        ImGui::Begin("Entity Inspector");



        ImGui::End();
    }

    inline void Draw(Registry& reg) {
        rlImGuiBegin();
        // 所有面板都在这里
        DrawEntityInspector(reg);

        rlImGuiEnd();
    }

}