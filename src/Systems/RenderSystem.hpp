#pragma once
#include "Core/Registry.hpp"
#include "Resources/ResourceManager.hpp"
#include "Components/Components.hpp"
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <format>
#include <algorithm>

namespace Rinn::RenderSystem {

    inline Font chinese_font = {};
    inline Camera3D camera = { 0 };
    inline Shader test_shader = { 0 };

    // 世界坐标缩放：100像素 = 1米
    constexpr float WORLD_SCALE = 0.01f;

    // ================================================================
    // 初始化窗口
    // ================================================================
    inline void Init(int width, int height, const char* title) {
        InitWindow(width, height, title);
        SetTargetFPS(144);

        // 3D 摄像机：60° 俯视透视
        camera.position = { 0.0f, 10.0f, 6.0f };
        camera.target = { 0.0f, 0.0f, 0.0f };
        camera.up = { 0.0f, 1.0f, 0.0f };
        camera.fovy = 45.0f;
        camera.projection = CAMERA_PERSPECTIVE;

        // 加载中文字体：ASCII + 常用汉字 + 标点符号
        std::vector<int> codepoints;
        for (int i = 32; i < 127; i++) codepoints.push_back(i);           // ASCII
        for (int i = 0x4E00; i <= 0x9FFF; i++) codepoints.push_back(i);   // 常用汉字
        for (int i = 0x3000; i <= 0x303F; i++) codepoints.push_back(i);   // CJK 标点符号 (。 《 》 【 】 、)
        for (int i = 0xFF00; i <= 0xFFEF; i++) codepoints.push_back(i);   // 全角 ASCII (！ ？ ， ： ； （ ）)
        for (int i = 0x2000; i <= 0x206F; i++) codepoints.push_back(i);   // 常用符号补充 (' ' " " …)
        
        chinese_font = LoadFontEx("../../../assets/font/SIMHEI.TTF", 32, codepoints.data(), (int)codepoints.size());

        if (chinese_font.glyphCount == 0) std::cerr << "字体加载失败!" << std::endl;
        else std::cerr << "字体加载成功!" << std::endl;

        // 加载测试 shader
        test_shader = LoadShader("../../../assets/shaders/test.vs", "../../../assets/shaders/test.fs");
    }

    // ================================================================
    // 更新摄像机跟随 
    // ================================================================
    inline void UpdateCamera(float target_x, float target_z) {
        float tx = target_x * WORLD_SCALE;
        float tz = target_z * WORLD_SCALE;
        camera.target = { tx, 0.0f, tz };
        camera.position = { tx, 10.0f, tz + 6.0f };
    }

    // ================================================================
    // 帧管理
    // ================================================================
    inline void BeginFrame(Color clear_color = { 30, 30, 30, 255 }) {
        BeginDrawing();
        ClearBackground(clear_color);
        BeginMode3D(camera);
    }

    inline void EndCameraMode() {
        EndMode3D();
    }

    inline void EndFrame() {
        EndDrawing();
    }

    inline bool ShouldClose() { return WindowShouldClose(); }
    inline float DeltaTime() { return GetFrameTime(); }
    inline int FPS() { return GetFPS(); }
    inline void Shutdown() { CloseWindow(); }

    // 光源

    // 方法 A：用 Raylib 的 Vector3
    Vector3 vertical_normal = { 0.0f, 0.0f, 1.0f };
    Vector3 tile_normal = { 0.0f, 1.0f, 0.0f };
    Vector3 facenormal = { 0 };

    inline void Facenormal() {
        int loc = GetShaderLocation(test_shader, "facenormal");
        SetShaderValue(test_shader, loc, &facenormal, SHADER_UNIFORM_VEC3);

    }


    // ================================================================
    // 绘制辅助
    // ================================================================
    inline void DrawText(const char* text, int x, int y, int size, Color color) {
        ::DrawText(text, x, y, size, color);
    }

    inline void DrawTextCN(const char* text, float x, float y, float size, Color color) {
        DrawTextEx(chinese_font, text, { x, y }, size, 2, color);
    }

    // ================================================================
    // 对话气泡：世界坐标 → 屏幕坐标投影
    // ================================================================
    inline void DrawTextBubbles(Registry& reg) {
        for (Entity e : reg.view<Transform, TextBubble>()) {
            const auto& t = reg.get<Transform>(e);
            const auto& tb = reg.get<TextBubble>(e);
            
            Vector3 pos3D = { t.x * WORLD_SCALE, 0.0f, t.y * WORLD_SCALE };
            Vector2 screen_pos = GetWorldToScreen(pos3D, camera);
            
            // 美观绘制黑边白底对话气泡
            int text_width = MeasureTextEx(chinese_font, tb.text, 24, 2).x;
            int draw_x = screen_pos.x - text_width / 2;
            int draw_y = screen_pos.y - 70; // 悬浮在人物头顶
            
            DrawRectangle(draw_x - 10, draw_y - 10, text_width + 20, 40, { 255, 255, 255, 220 });
            DrawRectangleLines(draw_x - 10, draw_y - 10, text_width + 20, 40, { 50, 50, 50, 255 });
            DrawTextCN(tb.text, draw_x, draw_y, 24, BLACK);
        }
    }

    // ================================================================
    // 3D面片：ground=true 水平铺地，ground=false 竖直站立
    // ================================================================
    inline void DrawSprite3D(Texture2D tex, Rectangle src, Vector3 pos, Vector2 size, bool ground) {
        rlSetTexture(tex.id);
        rlBegin(RL_QUADS);
        rlColor4ub(255, 255, 255, 255);
        
        float u1 = src.x / tex.width;
        float v1 = src.y / tex.height;
        float u2 = (src.x + src.width) / tex.width;
        float v2 = (src.y + src.height) / tex.height;

        if (ground) {
            // 水平铺在 XZ 平面，pos 是角落坐标
            // 节点的顺序影响法线，只有法线向上才能正确绘制
            rlTexCoord2f(u1, v1); rlVertex3f(pos.x, pos.y, pos.z);
            rlTexCoord2f(u1, v2); rlVertex3f(pos.x, pos.y, pos.z + size.y);
            rlTexCoord2f(u2, v2); rlVertex3f(pos.x + size.x, pos.y, pos.z + size.y);
            rlTexCoord2f(u2, v1); rlVertex3f(pos.x + size.x, pos.y, pos.z);
        } else {
            // 竖直站立，X 方向居中
            float hw = size.x / 2.0f;
            rlTexCoord2f(u1, v2); rlVertex3f(pos.x - hw, pos.y, pos.z);
            rlTexCoord2f(u2, v2); rlVertex3f(pos.x + hw, pos.y, pos.z);
            rlTexCoord2f(u2, v1); rlVertex3f(pos.x + hw, pos.y + size.y, pos.z);
            rlTexCoord2f(u1, v1); rlVertex3f(pos.x - hw, pos.y + size.y, pos.z);
        }

        rlEnd();
        rlSetTexture(0);
    }

    // ================================================================
    // 精灵绘制：两趟渲染（地面 → 立式精灵）
    // ================================================================
    inline void DrawSprites(Registry& reg, ResourceManager& res) {
        std::vector<Entity> ground_tiles;
        std::vector<Entity> sprites;

        for (Entity e : reg.view<Transform, Sprite>()) {
            if (reg.get<Sprite>(e).is_ground) ground_tiles.push_back(e);
            else sprites.push_back(e);
        }
        // 地面法线
        facenormal = tile_normal;
        Facenormal();
        // Pass 1: 地面瓦片（不透明，无需排序）
        for (Entity e : ground_tiles) {
            const auto& t = reg.get<Transform>(e);
            const auto& s = reg.get<Sprite>(e);
            Texture2D& tex = res.get_texture(s.texture_id);

            Rectangle src = { s.src_x, s.src_y, 
                              s.src_w == 0 ? (float)tex.width : s.src_w, 
                              s.src_h == 0 ? (float)tex.height : s.src_h };

            Vector3 pos = { t.x * WORLD_SCALE, 0.0f, t.y * WORLD_SCALE };
            Vector2 size = { s.width * WORLD_SCALE, s.height * WORLD_SCALE };
            DrawSprite3D(tex, src, pos, size, true);
        }

        // 立式法线
        facenormal = vertical_normal;
        Facenormal();
        // Pass 2: 立式精灵（半透明，Back-to-Front 排序）
        std::sort(sprites.begin(), sprites.end(),
            [&reg](Entity a, Entity b) {
                const auto& ta = reg.get<Transform>(a);
                const auto& tb = reg.get<Transform>(b);
                const auto& sa = reg.get<Sprite>(a);
                const auto& sb = reg.get<Sprite>(b);
                if (ta.layer != tb.layer) return ta.layer < tb.layer;
                return (ta.y + sa.height) < (tb.y + sb.height);
            });

        for (Entity e : sprites) {
            const auto& t = reg.get<Transform>(e);
            const auto& s = reg.get<Sprite>(e);
            Texture2D& tex = res.get_texture(s.texture_id);

            Rectangle src = { s.src_x, s.src_y, 
                              s.src_w == 0 ? (float)tex.width : s.src_w, 
                              s.src_h == 0 ? (float)tex.height : s.src_h };

            Vector3 pos = { t.x * WORLD_SCALE, 0.0f, t.y * WORLD_SCALE };
            Vector2 size = { s.width * WORLD_SCALE, s.height * WORLD_SCALE };
            DrawSprite3D(tex, src, pos, size, false);
        }
    }

}