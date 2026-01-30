#pragma once
#include "components/Components.hpp"
#include <tuple>
namespace Rinn {
    // ========================================
    // 🔧 添加/删除组件只改这里！
    // ========================================
    using AllComponents = std::tuple<
        Transform,
        Velocity,
        RigidBody,
        Sprite
        // 新增组件加在这里
    >;
}