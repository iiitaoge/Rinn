#pragma once
#include "imgui.h"
#include "ComponentUI.hpp"
#include "LightUI.hpp"
#include "AIDebugUI.hpp"

namespace Rinn::DebugPanelUI {

    inline bool Header(const char* label, bool default_open = false) {
        ImGuiTreeNodeFlags flags = default_open ? ImGuiTreeNodeFlags_DefaultOpen : 0;
        return ImGui::CollapsingHeader(label, flags);
    }

    inline void Draw(Registry& reg) {
        ImGui::SetNextWindowSize(ImVec2(420, 720), ImGuiCond_FirstUseEver);
        ImGui::Begin("Debug Tools");

        if (Header("Lighting", true)) {
            LightUI::LightUI();
        }
        LightUI::UPlightDir();

        if (Header("Time Control")) {
            TimeControl::DrawContent();
        }

        if (Header("Entity Inspector")) {
            ComponentUI::DrawEntityInspector(reg);
        }

        if (Header("NPC Inspector")) {
            NpcInspector::DrawContent(reg);
        }

        if (Header("AI Controls")) {
            DemoControls::DrawContent(reg);
        }

        if (Header("EventBus")) {
            EventBusInspector::DrawContent(reg);
        }

        if (Header("Knowledge")) {
            KnowledgeInspector::DrawContent(reg);
        }

        if (Header("Relations")) {
            RelationInspector::DrawContent(reg);
        }

        if (Header("Event Log")) {
            EventLog::DrawContent();
        }

        ImGui::End();
    }
}
