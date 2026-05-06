
#pragma once
#include "../Core/Registry.hpp"

namespace Rinn::PhysicSystem {
    // 纯积分器，边界交给lua处理
    inline void update(Registry& reg, float dt) {
        float maxX = (float)GetScreenWidth();
        float maxY = (float)GetScreenHeight();
        for (Entity e : reg.view<Transform, Velocity>()) {
            auto& t = reg.get<Transform>(e);
            auto& v = reg.get<Velocity>(e);

            t.x += v.vx * dt;
            t.y += v.vy * dt;

        }
    }
}