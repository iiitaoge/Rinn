#pragma once
#include "../Components/Components.hpp"
#include "../Core/Registry.hpp"
#include <vector>

namespace Rinn::CollisionSystem {

    struct Hit { Entity a, b; };

    // AABB 重叠检测：两个轴都重叠 → 碰撞
    inline bool overlaps(const Transform& ta, const Collider& ca,
                         const Transform& tb, const Collider& cb) {
        float ax = ta.x + ca.offset_x, ay = ta.y + ca.offset_y;
        float bx = tb.x + cb.offset_x, by = tb.y + cb.offset_y;
        return ax < bx + cb.width  && ax + ca.width  > bx       // X轴重叠
            && ay < by + cb.height && ay + ca.height > by;      // Y轴重叠
    }

    // O(n²) 暴力检测，返回所有碰撞对
    // 先用着
    inline std::vector<Hit> detect(Registry& reg) {
        std::vector<Hit> hits;
        std::vector<Entity> entities;
        for (Entity e : reg.view<Transform, Collider>())
            entities.push_back(e);
        for (size_t i = 0; i < entities.size(); ++i) {
            for (size_t j = i + 1; j < entities.size(); ++j) {
                Entity a = entities[i], b = entities[j];
                if (overlaps(reg.get<Transform>(a), reg.get<Collider>(a),
                             reg.get<Transform>(b), reg.get<Collider>(b)))
                    hits.push_back({a, b});
            }
        }
        return hits;
    }

    inline void resolve(Registry& reg, const std::vector<Hit>& hits) {
        for (auto& [a, b] : hits) {
            auto& ta = reg.get<Transform>(a); auto& ca = reg.get<Collider>(a);
            auto& tb = reg.get<Transform>(b); auto& cb = reg.get<Collider>(b);

            // 计算各轴穿透深度
            float ox = std::min(ta.x + ca.width - tb.x, tb.x + cb.width - ta.x);
            float oy = std::min(ta.y + ca.height - tb.y, tb.y + cb.height - ta.y);

            // 沿最小穿透轴推开，各退一半
            if (ox < oy) {
                float sign = (ta.x < tb.x) ? -1.0f : 1.0f;
                ta.x += sign * ox * 0.5f;
                tb.x -= sign * ox * 0.5f;
            }
            else {
                float sign = (ta.y < tb.y) ? -1.0f : 1.0f;
                ta.y += sign * oy * 0.5f;
                tb.y -= sign * oy * 0.5f;
            }
        }
    }
}
