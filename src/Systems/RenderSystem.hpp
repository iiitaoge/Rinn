#pragma once
#include <raylib.h>
#include "Core/Registry.hpp"
#include "components/Components.hpp"
namespace Rinn {
    // 基本结构
    class RenderSystem {
    public:
        // === 生命周期 ===
        void init(int width, int height, const char* title);
        void shutdown();

        // === 帧控制 ===
        void begin_frame(Color clearColor = BLACK);
        void end_frame();
        bool should_close() const;

        // === 时间 ===
        float delta_time() const;   // 本帧时间
        double elapsed() const;     // 总运行时间
        int fps() const;

        // === 基础绘制 ===
        void draw_rect(float x, float y, float w, float h, Color color);
        void draw_rect_filled(float x, float y, float w, float h, Color color);
        void draw_circle(float x, float y, float radius, Color color);
        void draw_line(float x1, float y1, float x2, float y2, Color color);
        void draw_text(const char* text, float x, float y, int size, Color color);

        // === ECS 集成（核心！）===
        void render(Registry& registry);  // 遍历所有可渲染 Entity

    private:
        int m_width = 0;
        int m_height = 0;
        bool m_initialized = false;
    };

    void RenderSystem::init(int width, int height, const char* title) {
        m_width = width;
        m_height = height;
        InitWindow(width, height, title);
        m_initialized = true;
    }

    void RenderSystem::shutdown() {
        CloseWindow();
    }

    void RenderSystem::begin_frame(Color clearColor) {
        BeginDrawing();                 // 开始绘画
        ClearBackground(clearColor);    // clearColor 清空 Back Buffer（否则会看到上一帧残影）
    }

    void RenderSystem::end_frame() {
        EndDrawing();                   // 
    }

    bool RenderSystem::should_close() const {
        return WindowShouldClose();            // 外界请求关闭
    }

    float RenderSystem::delta_time() const {
        return GetFrameTime();          // 运动与物理时间挂钩，而非帧率
    }

    double RenderSystem::elapsed() const {
        return GetTime();                // 程序启动时间
    }

    int RenderSystem::fps() const {
        return GetFPS();                // 帧率调整
    }

    void RenderSystem::draw_rect(float x, float y, float w, float h, Color color) {
        DrawRectangleLines((int)x, (int)y, (int)w, (int)h, color);      // 矩形边框
    }

    void RenderSystem::draw_rect_filled(float x, float y, float w, float h, Color color) {
        DrawRectangle((int)x, (int)y, (int)w, (int)h, color);           // 填充矩形
    }

    void RenderSystem::draw_circle(float x, float y, float radius, Color color) {
        DrawCircle((int)x, (int)y, radius, color);                      // 圆
    }

    void RenderSystem::draw_line(float x1, float y1, float x2, float y2, Color color) {
        DrawLine((int)x1, (int)y1, (int)x2, (int)y2, color);            // 线段
    }

    void RenderSystem::draw_text(const char* text, float x, float y, int size, Color color) {
        DrawText(text, (int)x, (int)y, size, color);                    // 文本
    }

    // 根据渲染逻辑编写，暂时空着
    void RenderSystem::render(Registry& registry) {
        // TODO: 未来按 layer 遍历
        for (Entity entity : registry.view<Transform, Sprite>()) {
            auto& t = registry.get<Transform>(entity);
            auto& s = registry.get<Sprite>(entity);
            DrawTexture(s.texture, t.x, t.y, WHITE);
        }
    }
}
