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
    inline Camera3D camera = { 0 }; // 新增：3D 摄像机

    // ================================================================
    // 初始化窗口
    // ================================================================
    inline void Init(int width, int height, const char* title) {
        InitWindow(width, height, title);
        SetTargetFPS(144);

        // --- HD-2D 摄像机默认配置 ---
        camera.position = { 0.0f, 10.0f, 6.0f }; // (修改) 缩放100倍：Y=10.0米, Z=6.0米
        camera.target = { 0.0f, 0.0f, 0.0f };
        camera.up = { 0.0f, 1.0f, 0.0f };
        camera.fovy = 45.0f;
        camera.projection = CAMERA_PERSPECTIVE;
        // ------------------------------

        // 加载中文字体：ASCII + 常用汉字
        std::vector<int> codepoints;
        for (int i = 32; i < 127; i++) codepoints.push_back(i);           // ASCII
        for (int i = 0x4E00; i <= 0x9FFF; i++) codepoints.push_back(i);   // 常用汉字
        chinese_font = LoadFontEx("../../../assets/font/SIMHEI.TTF", 32, codepoints.data(), (int)codepoints.size());

        if (chinese_font.glyphCount == 0) std::cerr << "字体加载失败!" << std::endl;
        else std::cerr << "字体加载成功!" << std::endl;
    }

    // ================================================================
    // 更新摄像机跟随 
    // ================================================================
    inline void UpdateCamera(float target_x, float target_z) {
        // [修复1]：为了不被近/远裁切面 (0.01 - 1000) 给切成两半，全局缩小 100 倍！100像素 = 1米
        float tx = target_x * 0.01f;
        float tz = target_z * 0.01f;
        camera.target = { tx, 0.0f, tz };
        
        // 60度俯视：抬高10米，后撤6米
        camera.position = { tx, 10.0f, tz + 6.0f };
    }

    // ================================================================
    // 帧管理 (支持 2D & 3D 混合渲染)
    // ================================================================
    inline void BeginFrame(Color clear_color = RAYWHITE) {
        BeginDrawing();
        ClearBackground(clear_color);
        
        // 👇 进入 HD-2D 的透视 3D 空间
        BeginMode3D(camera);  
    }

    inline void EndFrame() {
        // 👇 退出 3D 空间，之后可以叠加画 2D 全屏 UI (例如血条、黑边等)
        EndMode3D();          
        
        EndDrawing();
    }

    inline bool ShouldClose() { return WindowShouldClose(); }
    inline float DeltaTime() { return GetFrameTime(); }
    inline int FPS() { return GetFPS(); }
    inline void Shutdown() { CloseWindow(); }

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
    // 自定义核心：永远竖直的面片（彻底解决转视角、人物乱翻转的问题）
    // ================================================================
    inline void DrawSprite3D(Texture2D tex, Rectangle src, Vector3 pos, Vector2 size) {
        rlSetTexture(tex.id);
        rlBegin(RL_QUADS);
        rlColor4ub(255, 255, 255, 255);
        
        float hw = size.x / 2.0f;
        float u1 = src.x / tex.width;
        float v1 = src.y / tex.height;
        float u2 = (src.x + src.width) / tex.width;
        float v2 = (src.y + src.height) / tex.height;

        // [修复2]：不再让纸片瞎转着找镜头，永远笔挺地站着面朝南方 (+Z轴)
        rlTexCoord2f(u1, v2); rlVertex3f(pos.x - hw, pos.y, pos.z);
        rlTexCoord2f(u2, v2); rlVertex3f(pos.x + hw, pos.y, pos.z);
        rlTexCoord2f(u2, v1); rlVertex3f(pos.x + hw, pos.y + size.y, pos.z);
        rlTexCoord2f(u1, v1); rlVertex3f(pos.x - hw, pos.y + size.y, pos.z);

        rlEnd();
        rlSetTexture(0);
    }

    // ================================================================
    // 精灵绘制 — HD-2D Billboard (纸片人) 渲染
    // ================================================================
    inline void DrawSprites(Registry& reg, ResourceManager& res) {

        std::vector<Entity> entities;
        for (Entity e : reg.view<Transform, Sprite>()) {
            entities.push_back(e);
        }

        // Z-sorting 深度排序，由于有透明像素，必须强制从远到近画
        std::sort(entities.begin(), entities.end(),
            [&reg](Entity a, Entity b) {
                const auto& ta = reg.get<Transform>(a);
                const auto& tb = reg.get<Transform>(b);
                if (ta.layer != tb.layer) return ta.layer < tb.layer;
                return ta.y < tb.y; 
            });

        for (Entity e : entities) {
            const auto& t = reg.get<Transform>(e);
            const auto& s = reg.get<Sprite>(e);

            Texture2D& tex = res.get_texture(s.texture_id);

            Rectangle src = { s.src_x, s.src_y, 
                              s.src_w == 0 ? (float)tex.width : s.src_w, 
                              s.src_h == 0 ? (float)tex.height : s.src_h };

            // [修复1同步]：位置和体积全部缩小 100 倍！
            Vector3 pos3D = { t.x * 0.01f, 0.0f, t.y * 0.01f }; // Y=0，直接踩在地板上
            Vector2 size2D = { s.width * 0.01f, s.height * 0.01f };

            // 丢弃难用的 DrawBillboardRec，使用稳定的正交立牌
            DrawSprite3D(tex, src, pos3D, size2D);
        }
    }

}