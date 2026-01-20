#include <iostream>
#include <format>
#include "Core/Registry.hpp"
#include "components/Components.hpp"
#include <sol/sol.hpp>
#include "Scripting/ScriptContext.hpp"
#include "Scripting/LuaBinder.hpp"
#include <raylib.h>
#include "Systems/RenderSystem.hpp"
// ============================================================================
// Test Case 1: 创建 N 个实体，挂上 Position + Velocity，验证 View 遍历
// ============================================================================


int main() {
    using namespace Rinn;

    // 1. 创建 ECS
    Registry reg;

    Rinn::ScriptContext ctx;

    bind_registry(ctx.state(), reg);  // ← 绑定到 Lua

    ctx.run_file("D:/cs/vs/Project_Rinn/scripts/test.lua");


    // 2. 初始化窗口
    InitWindow(800, 600, "Rinn Test");
    SetTargetFPS(60);

    // 3. 主循环
    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(DARKGRAY);

        // 遍历所有有 Transform 的实体，画出来
        for (Entity ent : reg.view<Rinn::Transform>()) {
            auto& t = reg.get<Rinn::Transform>(ent);
            DrawCircle((int)t.x, (int)t.y, 15, RED);
        }

        EndDrawing();
    }

    CloseWindow();


    return 0;
}