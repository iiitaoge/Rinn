#pragma once
#include "imgui.h"
#include "rlImGui.h"
#include <cmath>
#include "../Systems/RenderSystem.hpp"

namespace Rinn::LightUI {
	inline float azimuth_angle_rad = 0;
	inline float elevation_angle_rad = 0;

	inline void UPlightDir() {
		RenderSystem::lightDir.x = cos(elevation_angle_rad) * cos(azimuth_angle_rad);
		RenderSystem::lightDir.y = sin(elevation_angle_rad);
		RenderSystem::lightDir.z = cos(elevation_angle_rad) * sin(azimuth_angle_rad);
	}

	inline void LightUI() {
		// 光色
		ImGui::ColorEdit3("light color", &RenderSystem::light_color.x);
		// 环境光色
		ImGui::ColorEdit3("ambient color", &RenderSystem::ambient_color.x);
		// 地面的角 东西南北 水平坐标系
		ImGui::SliderAngle("azimuth", &azimuth_angle_rad, 0.0f, 360.0f);
		// 高度角 竖直坐标系
		ImGui::SliderAngle("elevation", &elevation_angle_rad, 0.0f, 90.0f);
	}

	inline void DrawLightPanel() {
		ImGui::Begin("Lighting");
		LightUI();
		ImGui::End();
		UPlightDir();
	}
}