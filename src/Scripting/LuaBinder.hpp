#pragma once
#include "Core/Registry.hpp"
#include "components/Components.hpp"
#include "Resources/ResourceManager.hpp"
#include "Resources/PrefabManager.hpp"
#include "Systems/InputSystem.hpp"
#include <sol/sol.hpp>
#include <string>

namespace Rinn {

    // ============================================================================
    // LuaBinder - 意图驱动 API
    // ============================================================================
    // 设计原则：
    //   - Lua 只能声明意图，不能直接操作组件
    //   - C++ System 执行具体物理操作
    //   - 最大解耦，最高安全性
    // ============================================================================

    // ========================================
    // 🎮 意图 API：Lua 声明，C++ 执行
    // ========================================
    inline void bind_intent_api(sol::state& lua, Registry& reg) {
        
        // move(entity, direction, speed)
        // 方向: "up", "down", "left", "right"
        lua["move"] = [&reg](Entity e, const std::string& direction, float speed) {
            if (!reg.is_alive(e)) return;
            
            // 确保实体有 Velocity 组件
            if (!reg.has<Velocity>(e)) return;
            
            auto& vel = reg.get<Velocity>(e);
            
            if (direction == "up") {
                vel.vy = -speed;
            } else if (direction == "down") {
                vel.vy = speed;
            } else if (direction == "left") {
                vel.vx = -speed;
            } else if (direction == "right") {
                vel.vx = speed;
            }
        };

        // stop(entity)
        lua["stop"] = [&reg](Entity e) {
            if (!reg.is_alive(e)) return;
            if (!reg.has<Velocity>(e)) return;
            
            auto& vel = reg.get<Velocity>(e);
            vel.vx = 0.0f;
            vel.vy = 0.0f;
        };
    }

    // ========================================
    // 🔍 查询 API：只读访问
    // ========================================
    inline void bind_query_api(sol::state& lua, Registry& reg) {
        
        // query_position(entity) -> {x, y}
        lua["query_position"] = [&reg](Entity e, sol::this_state ts) -> sol::table {
            sol::state_view lua_view(ts);
            sol::table result = lua_view.create_table();
            
            if (!reg.is_alive(e) || !reg.has<Transform>(e)) {
                result["x"] = 0.0f;
                result["y"] = 0.0f;
                return result;
            }
            
            const auto& t = reg.get<Transform>(e);
            result["x"] = t.x;
            result["y"] = t.y;
            return result;
        };

        // query_velocity(entity) -> {vx, vy}
        lua["query_velocity"] = [&reg](Entity e, sol::this_state ts) -> sol::table {
            sol::state_view lua_view(ts);
            sol::table result = lua_view.create_table();
            
            if (!reg.is_alive(e) || !reg.has<Velocity>(e)) {
                result["vx"] = 0.0f;
                result["vy"] = 0.0f;
                return result;
            }
            
            const auto& v = reg.get<Velocity>(e);
            result["vx"] = v.vx;
            result["vy"] = v.vy;
            return result;
        };
    }

    // ========================================
    // 🏭 生成 API：实体创建
    // ========================================
    inline void bind_spawn_api(sol::state& lua, Registry& reg, 
                               ResourceManager& rm, PrefabManager& pm) {
        
        // spawn(prefab_name, x, y) -> Entity
        lua["spawn"] = [&reg, &rm, &pm](const std::string& prefab, float x, float y) {
            return pm.spawn(reg, rm, prefab, x, y);
        };

        // destroy(entity)
        lua["destroy"] = [&reg](Entity e) {
            if (reg.is_alive(e)) {
                reg.destroy_entity(e);
            }
        };

        // is_alive(entity) -> bool
        lua["is_alive"] = [&reg](Entity e) {
            return reg.is_alive(e);
        };
    }

    // ========================================
    // 🖼️ 资源 API
    // ========================================
    inline void bind_resource_api(sol::state& lua, ResourceManager& rm, 
                                  PrefabManager& pm, Registry& reg) {
        
        // load_texture(path) -> texture_id
        lua["load_texture"] = [&rm](const std::string& path) {
            return rm.load_texture(path);
        };

        // set_sprite_texture(entity, texture_id)
        // 允许设置精灵纹理（这是安全的，不破坏物理）
        lua["set_sprite_texture"] = [&reg](Entity e, uint16_t tex_id) {
            if (!reg.is_alive(e) || !reg.has<Sprite>(e)) return;
            reg.get<Sprite>(e).texture_id = tex_id;
        };
    }

    // ========================================
    // 🎮 输入 API (保持不变)
    // ========================================
    inline void bind_input_api(sol::state& lua) {
        sol::table input = lua.create_named_table("input");

        // 键盘
        input["is_key_down"] = Input::is_key_down;
        input["is_key_pressed"] = Input::is_key_pressed;
        input["is_key_released"] = Input::is_key_released;

        // 鼠标
        input["get_mouse_x"] = Input::get_mouse_x;
        input["get_mouse_y"] = Input::get_mouse_y;
        input["is_mouse_down"] = Input::is_mouse_down;
        input["is_mouse_pressed"] = Input::is_mouse_pressed;
        input["is_mouse_released"] = Input::is_mouse_released;

        // KEY 常量表
        sol::table KEY = lua.create_named_table("KEY");
        KEY["A"] = KEY_A; KEY["B"] = KEY_B; KEY["C"] = KEY_C; KEY["D"] = KEY_D;
        KEY["E"] = KEY_E; KEY["F"] = KEY_F; KEY["G"] = KEY_G; KEY["H"] = KEY_H;
        KEY["I"] = KEY_I; KEY["J"] = KEY_J; KEY["K"] = KEY_K; KEY["L"] = KEY_L;
        KEY["M"] = KEY_M; KEY["N"] = KEY_N; KEY["O"] = KEY_O; KEY["P"] = KEY_P;
        KEY["Q"] = KEY_Q; KEY["R"] = KEY_R; KEY["S"] = KEY_S; KEY["T"] = KEY_T;
        KEY["U"] = KEY_U; KEY["V"] = KEY_V; KEY["W"] = KEY_W; KEY["X"] = KEY_X;
        KEY["Y"] = KEY_Y; KEY["Z"] = KEY_Z;

        KEY["0"] = KEY_ZERO;  KEY["1"] = KEY_ONE;   KEY["2"] = KEY_TWO;
        KEY["3"] = KEY_THREE; KEY["4"] = KEY_FOUR;  KEY["5"] = KEY_FIVE;
        KEY["6"] = KEY_SIX;   KEY["7"] = KEY_SEVEN; KEY["8"] = KEY_EIGHT;
        KEY["9"] = KEY_NINE;

        KEY["SPACE"] = KEY_SPACE;
        KEY["ENTER"] = KEY_ENTER;
        KEY["ESCAPE"] = KEY_ESCAPE;
        KEY["TAB"] = KEY_TAB;
        KEY["BACKSPACE"] = KEY_BACKSPACE;
        KEY["DELETE"] = KEY_DELETE;

        KEY["UP"] = KEY_UP;
        KEY["DOWN"] = KEY_DOWN;
        KEY["LEFT"] = KEY_LEFT;
        KEY["RIGHT"] = KEY_RIGHT;

        KEY["SHIFT"] = KEY_LEFT_SHIFT;
        KEY["CTRL"] = KEY_LEFT_CONTROL;
        KEY["ALT"] = KEY_LEFT_ALT;

        // MOUSE 常量表
        sol::table MOUSE = lua.create_named_table("MOUSE");
        MOUSE["LEFT"] = MOUSE_BUTTON_LEFT;
        MOUSE["RIGHT"] = MOUSE_BUTTON_RIGHT;
        MOUSE["MIDDLE"] = MOUSE_BUTTON_MIDDLE;
    }

    // ========================================
    // 🚀 统一绑定入口
    // ========================================
    inline void bind_all(sol::state& lua, Registry& reg, 
                         ResourceManager& rm, PrefabManager& pm) {
        
        // Entity 类型绑定
        lua.new_usertype<Entity>("Entity",
            "index", &Entity::index,
            "generation", &Entity::generation,
            "is_null", &Entity::is_null
        );

        // 绑定所有 API
        bind_intent_api(lua, reg);
        bind_query_api(lua, reg);
        bind_spawn_api(lua, reg, rm, pm);
        bind_resource_api(lua, rm, pm, reg);
        bind_input_api(lua);
    }

}
