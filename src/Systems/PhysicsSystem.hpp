#pragma once
#include "Core/Registry.hpp"
#include "components/Components.hpp"

// ============================================================================
// PhysicsSystem.hpp - 物理系统
// ============================================================================
// 职责：
//   - 遍历所有拥有 Transform + Velocity 的实体
//   - 根据速度和 delta time 更新位置
// ============================================================================
// 设计原则：
//   - 使用 get<T>() 获取引用，直接修改组件
//   - 无需 emplace，因为组件已存在
//   - 位移公式：position += velocity × dt
// ============================================================================

namespace Rinn {

    class PhysicsSystem {
    public:
        // ================================================================
        // 更新所有物理实体的位置
        // ================================================================
        // 参数：
        //   registry - ECS 注册表
        //   dt - 帧间隔时间（秒），由 GetFrameTime() 提供
        // ================================================================
        void update(Registry& registry, float dt) {
            // 遍历所有拥有 Transform 和 Velocity 的实体
            for (Entity entity : registry.view<Transform, Velocity>()) {
                // 获取组件引用（直接修改，无需 emplace）
                auto& transform = registry.get<Transform>(entity);
                auto& velocity = registry.get<Velocity>(entity);

                // 应用物理公式：新位置 = 旧位置 + 速度 × 时间
                transform.x += velocity.vx * dt;
                transform.y += velocity.vy * dt;
            }
        }
    };

}
