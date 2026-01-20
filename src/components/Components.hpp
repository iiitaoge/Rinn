#pragma once


namespace Rinn{
    
    // 渲染，目前 位置 + 精灵 预留 层级
    // 任意空间变换
    struct Transform {
        float x, y;
        int layer = 0;  // ← 预留层级字段，但暂时不用 画的顺序决定了谁遮挡谁
    };
    // Sprite = Image + 行为能力
    struct Sprite {
        Texture2D texture;
        float width, height;
        // 未来扩展：int animationFrame; 
    };

    struct RigidBody {
        float vx, vy;
    };

    struct Velocity {
        float vx, vy;
    };
}

