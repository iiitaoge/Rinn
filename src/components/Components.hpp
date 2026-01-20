#pragma once


namespace Rinn{
    // 这是一个纯数据结构 (POD)
    struct Transform {
        float x, y;
    };

    struct RigidBody {
        float vx, vy;
    };

    struct Velocity {
        float vx, vy;
    };
}

