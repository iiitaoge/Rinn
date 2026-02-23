#pragma once
#include "Core/Registry.hpp"
#include "Resources/ResourceManager.hpp"
#include "Components/Components.hpp"
#include "raylib.h"
#include "rlgl.h"
#include <format>
#include <algorithm>

// ============================================================================
// RenderSystem.hpp - 渲染系统（namespace + 自由函数）
// ============================================================================
// DOD 原则：
//   - 无状态
//   - 纯函数操作外部资源
// ============================================================================

namespace Rinn::RenderSystem {

    // ================================================================
    // 初始化窗口
    // ================================================================
    inline void Init(int width, int height, const char* title) {
        InitWindow(width, height, title);
        SetTargetFPS(144);
    }

    // ================================================================
    // 帧管理
    // ================================================================
    inline void BeginFrame(Color clear_color = RAYWHITE) {
        BeginDrawing();
        ClearBackground(clear_color);
    }

    inline void EndFrame() {
        EndDrawing();
    }

    inline bool ShouldClose() {
        return WindowShouldClose();
    }

    inline float DeltaTime() {
        return GetFrameTime();
    }

    inline int FPS() {
        return GetFPS();
    }

    inline void Shutdown() {
        CloseWindow();
    }

    // ================================================================
    // 绘制辅助
    // ================================================================
    inline void DrawText(const char* text, int x, int y, int size, Color color) {
        ::DrawText(text, x, y, size, color);
    }

    inline void DrawRect(float x, float y, float w, float h, Color color) {
        DrawRectangleLines((int)x, (int)y, (int)w, (int)h, color);
    }

    inline void DrawRectFilled(float x, float y, float w, float h, Color color) {
        DrawRectangle((int)x, (int)y, (int)w, (int)h, color);
    }

}