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

    // 新增：中文字体（namespace 级别的变量）
    inline Font chinese_font = {};

    // ================================================================
    // 初始化窗口
    // ================================================================
    inline void Init(int width, int height, const char* title) {
        InitWindow(width, height, title);
        SetTargetFPS(144);

        // 加载中文字体：ASCII + 常用汉字
        std::vector<int> codepoints;
        for (int i = 32; i < 127; i++) codepoints.push_back(i);           // ASCII
        for (int i = 0x4E00; i <= 0x9FFF; i++) codepoints.push_back(i);   // 常用汉字
        chinese_font = LoadFontEx("../../../assets/font/SIMHEI.TTF", 32, codepoints.data(), (int)codepoints.size());

        // 验证是否加载成功
        if (chinese_font.glyphCount == 0) {
            std::cerr << "字体加载失败!" << std::endl;
        }
        else
            std::cerr << "字体加载成功!" << std::endl;
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

    // 原来的 DrawText 保留给英文用
    // 新增一个中文版本
    inline void DrawTextCN(const char* text, float x, float y, float size, Color color) {
        DrawTextEx(chinese_font, text, { x, y }, size, 2, color);
    }


    inline void DrawRect(float x, float y, float w, float h, Color color) {
        DrawRectangleLines((int)x, (int)y, (int)w, (int)h, color);
    }

    inline void DrawRectFilled(float x, float y, float w, float h, Color color) {
        DrawRectangle((int)x, (int)y, (int)w, (int)h, color);
    }

    // ================================================================
    // 精灵绘制 — 遍历所有 Transform + Sprite 实体
    // ================================================================
    inline void DrawSprites(Registry& reg, ResourceManager& res) {

        // 1. 收集所有有 Transform+Sprite 的实体到 vector
        std::vector<Entity> entities;
        for (Entity e : reg.view<Transform, Sprite>()) {
            entities.push_back(e);
        }
        // 2. 按 layer 排序（小的先画 = 底层）
        std::sort(entities.begin(), entities.end(),
            [&reg](Entity a, Entity b) {
                return reg.get<Transform>(a).layer < reg.get<Transform>(b).layer;
            });



        for (Entity e : entities) {
            const auto& t = reg.get<Transform>(e);
            const auto& s = reg.get<Sprite>(e);

            Texture2D& tex = res.get_texture(s.texture_id);

            // 如果 Lua 没有指定截取宽/高 (等于0)，则默认使用整张图的原始宽/高
            Rectangle src = { s.src_x, s.src_y, 
                              s.src_w == 0 ? (float)tex.width : s.src_w, 
                              s.src_h == 0 ? (float)tex.height : s.src_h };

            //               屏幕x  屏幕y   显示宽    显示高
            Rectangle dst = { t.x, t.y, s.width, s.height };

            DrawTexturePro(tex, src, dst, { 0.0f, 0.0f }, 0.0f, WHITE);
            //             贴图  源区域 目标区域  旋转原点     旋转角度  着色
        }
    }

}