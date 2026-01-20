#include <iostream>
#include <format>
#include "Core/Registry.hpp"
#include "components/Components.hpp"
#include <sol/sol.hpp>
#include "Scripting/ScriptContext.hpp"
#include <Scripting/LuaBinder.hpp>
#include <raylib.h>
// ============================================================================
// Test Case 1: 创建 N 个实体，挂上 Position + Velocity，验证 View 遍历
// ============================================================================


int main() {
    using namespace Rinn;

    // 1. 创建 ECS
    Registry reg;
    std::array<Entity, 10> entities;
    for (int i = 0; i < 10; i++) {
        entities[i] = reg.create_entity();

        (void)reg.emplace<Rinn::Transform>(entities[i],
            static_cast<float>(10 * i),
            static_cast<float>(10 * i + 5)
        );
    }

    // 2. 初始化窗口
    InitWindow(800, 600, "Rinn Test");
    SetTargetFPS(60);

    // 3. 主循环
    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(DARKGRAY);

        // 遍历有 Transform 的实体，画出来
        for (Entity ent : reg.view<Rinn::Transform>()) {
            auto& t = reg.get<Rinn::Transform>(ent);
            DrawCircle((int)t.x, (int)t.y, 10, RED);
        }

        EndDrawing();
    }

    CloseWindow();

    //Registry reg;           // 创建 Registry
    //Rinn::ScriptContext ctx;

    //bind_registry(ctx.state(), reg);  // ← 绑定到 Lua

    //ctx.run("print('hello')");
    //ctx.run_file("D:/cs/vs/Project_Rinn/scripts/test.lua");

    return 0;
}