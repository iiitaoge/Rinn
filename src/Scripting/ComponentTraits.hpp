#pragma once
#include <sol/sol.hpp>
#include "components/Components.hpp"
namespace Rinn {
    // 未找到特化模板将会使用主模板
    // 主模板（未特化会编译报错）
    template<typename T>
    struct ComponentTrait {
        static_assert(sizeof(T) == 0, "请为此组件添加 ComponentTrait 特化！");
    };
    // ========== Transform ==========
    template<>
    struct ComponentTrait<Transform> {
        static constexpr const char* name = "Transform";

        static Transform from_table(sol::table t) {
            return { t.get_or("x", 0.0f), t.get_or("y", 0.0f), t.get_or("layer", 0) };
        }

        static sol::table to_table(sol::state_view lua, const Transform& c) {
            return lua.create_table_with("x", c.x, "y", c.y, "layer", c.layer);
        }
    };
    // ========== Velocity ==========
    template<>
    struct ComponentTrait<Velocity> {
        static constexpr const char* name = "Velocity";

        static Velocity from_table(sol::table t) {
            return { t.get_or("vx", 0.0f), t.get_or("vy", 0.0f) };
        }

        static sol::table to_table(sol::state_view lua, const Velocity& c) {
            return lua.create_table_with("vx", c.vx, "vy", c.vy);
        }
    };
    // ========== RigidBody ==========
    template<>
    struct ComponentTrait<RigidBody> {
        static constexpr const char* name = "RigidBody";

        static RigidBody from_table(sol::table t) {
            return { t.get_or("vx", 0.0f), t.get_or("vy", 0.0f) };
        }

        static sol::table to_table(sol::state_view lua, const RigidBody& c) {
            return lua.create_table_with("vx", c.vx, "vy", c.vy);
        }
    };
    // ========== Sprite ==========
    template<>
    struct ComponentTrait<Sprite> {
        static constexpr const char* name = "Sprite";
        static Sprite from_table(sol::table t) {
            return {
                t.get_or<uint16_t>("texture_id", 0),
                t.get_or("width", 64.0f),
                t.get_or("height", 64.0f)
            };
        }
        static sol::table to_table(sol::state_view lua, const Sprite& c) {
            return lua.create_table_with(
                "texture_id", c.texture_id,
                "width", c.width,
                "height", c.height
            );
        }
    };
}