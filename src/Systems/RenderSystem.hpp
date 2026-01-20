#pragma once

// 基本结构
class RenderSystem {
public:
    void init(int width, int height, const char* title);
    void begin_frame();
    void end_frame();
    bool should_close();
};