#pragma once
#include "../Components/Components.hpp"
#include "../Core/Registry.hpp"
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace Rinn::CollisionSystem {

    struct Hit { Entity a, b; };

    // ================================================================
    // 空间哈希网格（宽相）
    // ================================================================
    constexpr int CELL_SIZE = 64; // 2 个瓦片宽，玩家(33px)最多跨 2×2 格

    inline uint64_t cell_key(int cx, int cy) {
        return (uint64_t(uint32_t(cx)) << 32) | uint64_t(uint32_t(cy));
    }

    // System 拥有的状态（不在 Component 里，允许非 POD）
    inline std::unordered_map<uint64_t, std::vector<Entity>> grid;

    // 将实体插入它覆盖的所有格子
    inline void insert_to_grid(Entity e, const Transform& t, const Collider& c) {
        float ax = t.x + c.offset_x;
        float ay = t.y + c.offset_y;
        int x0 = (int)std::floor(ax / CELL_SIZE);
        int y0 = (int)std::floor(ay / CELL_SIZE);
        int x1 = (int)std::floor((ax + c.width  - 1) / CELL_SIZE);
        int y1 = (int)std::floor((ay + c.height - 1) / CELL_SIZE);
        for (int cx = x0; cx <= x1; ++cx)
            for (int cy = y0; cy <= y1; ++cy)
                grid[cell_key(cx, cy)].push_back(e);
    }

    // ================================================================
    // AABB 窄相
    // ================================================================
    inline bool overlaps(const Transform& ta, const Collider& ca,
                         const Transform& tb, const Collider& cb) {
        float ax = ta.x + ca.offset_x, ay = ta.y + ca.offset_y;
        float bx = tb.x + cb.offset_x, by = tb.y + cb.offset_y;
        return ax < bx + cb.width  && ax + ca.width  > bx
            && ay < by + cb.height && ay + ca.height > by;
    }

    // ================================================================
    // 碰撞层掩码过滤
    // ================================================================
    inline bool layers_match(const Collider& ca, const Collider& cb) {
        return (ca.layer & cb.mask) || (cb.layer & ca.mask);
    }

    // ================================================================
    // 宽相 + 窄相 二阶段检测
    // ================================================================
    inline std::vector<Hit> detect(Registry& reg) {
        // 1. 重建空间网格
        grid.clear();
        for (Entity e : reg.view<Transform, Collider>()) {
            insert_to_grid(e, reg.get<Transform>(e), reg.get<Collider>(e));
        }

        // 2. 只让动态实体（有 Velocity）发起查询
        std::vector<Hit> hits;
        for (Entity a : reg.view<Transform, Collider, Velocity>()) {
            const auto& ta = reg.get<Transform>(a);
            const auto& ca = reg.get<Collider>(a);

            // 计算自身覆盖的格子范围
            float ax = ta.x + ca.offset_x;
            float ay = ta.y + ca.offset_y;
            int x0 = (int)std::floor(ax / CELL_SIZE) - 1;
            int y0 = (int)std::floor(ay / CELL_SIZE) - 1;
            int x1 = (int)std::floor((ax + ca.width  - 1) / CELL_SIZE) + 1;
            int y1 = (int)std::floor((ay + ca.height - 1) / CELL_SIZE) + 1;

            // 遍历自身 + 邻格
            for (int cx = x0; cx <= x1; ++cx) {
                for (int cy = y0; cy <= y1; ++cy) {
                    auto it = grid.find(cell_key(cx, cy));
                    if (it == grid.end()) continue;
                    for (Entity b : it->second) {
                        if (a.index() >= b.index()) continue; // 避免重复对和自碰撞
                        const auto& cb = reg.get<Collider>(b);
                        if (!layers_match(ca, cb)) continue;  // 碰撞层过滤
                        const auto& tb = reg.get<Transform>(b);
                        if (overlaps(ta, ca, tb, cb))
                            hits.push_back({a, b});
                    }
                }
            }
        }
        return hits;
    }

    // ================================================================
    // 碰撞解算（几乎不变）
    // ================================================================
    inline void resolve(Registry& reg, const std::vector<Hit>& hits) {
        for (auto& [a, b] : hits) {
            auto& ta = reg.get<Transform>(a); auto& ca = reg.get<Collider>(a);
            auto& tb = reg.get<Transform>(b); auto& cb = reg.get<Collider>(b);

            // 二次校验：前一次 resolve 推开后，当前重叠可能已消解
            if (!overlaps(ta, ca, tb, cb)) continue;

            bool a_movable = reg.has<Velocity>(a);
            bool b_movable = reg.has<Velocity>(b);
            if (!a_movable && !b_movable) continue;

            // 计算各轴穿透深度
            float ox = std::min(ta.x + ca.width - tb.x, tb.x + cb.width - ta.x);
            float oy = std::min(ta.y + ca.height - tb.y, tb.y + cb.height - ta.y);

            // 沿最小穿透轴推开
            if (ox < oy) {
                float sign = (ta.x < tb.x) ? -1.0f : 1.0f;
                if (a_movable && b_movable) { ta.x += sign * ox * 0.5f; tb.x -= sign * ox * 0.5f; }
                else if (a_movable) { ta.x += sign * ox; }
                else if (b_movable) { tb.x -= sign * ox; }
            }
            else {
                float sign = (ta.y < tb.y) ? -1.0f : 1.0f;
                if (a_movable && b_movable) { ta.y += sign * oy * 0.5f; tb.y -= sign * oy * 0.5f; }
                else if (a_movable) { ta.y += sign * oy; }
                else if (b_movable) { tb.y -= sign * oy; }
            }
        }
    }
}
