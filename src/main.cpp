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
    RenderSystem render;


    bind_registry(ctx.state(), reg);  // ← 绑定到 Lua

    ctx.run_file("D:/cs/vs/Project_Rinn/scripts/test.lua");

    render.init(2000, 1200, "render");

    // 2. 加载测试贴图
    Texture2D testTexture = LoadTexture("D:/cs/vs/Project_Rinn/assets/test.png");

    SetTargetFPS(60);

    // 3. 创建实体并挂载组件
    Entity e = reg.create_entity();
    reg.emplace<Rinn::Transform>(e, 100.0f, 100.0f);
    reg.emplace<Sprite>(e, testTexture, 64.0f, 64.0f);  // 贴图 + 尺寸

    // 4. 主循环
    while (!render.should_close()) {
        render.begin_frame(DARKGRAY);
        render.render(reg);  // ← 会绘制这个精灵
        render.end_frame();
    }

    // 5. 清理
    UnloadTexture(testTexture);
    render.shutdown();


    return 0;
}