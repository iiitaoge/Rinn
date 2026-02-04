#include <iostream>
#include <format>
#include "Core/Registry.hpp"
#include "components/Components.hpp"
#include <sol/sol.hpp>
#include "Scripting/ScriptContext.hpp"
#include "Scripting/LuaBinder.hpp"
#include "Resources/PrefabManager.hpp"
#include "Systems/RenderSystem.hpp"
#include "Systems/PhysicsSystem.hpp"

// ============================================================================
// Project Rinn - 意图驱动架构测试
// ============================================================================

int main() {
    using namespace Rinn;

    std::cout << "=== C++ 初始化 ===" << std::endl;

    // 1. 创建核心系统
    Registry reg;
    ResourceManager rm;
    PrefabManager pm;
    RenderSystem renderer;
    PhysicsSystem physics;
    ScriptContext ctx;

    // 2. 注册预制体（C++ 控制组件组合）
    register_default_prefabs(pm);
    std::cout << "预制体注册完成" << std::endl;

    // 3. 统一绑定所有 Lua API
    bind_all(ctx.state(), reg, rm, pm);
    std::cout << "Lua 绑定完成" << std::endl;

    // 4. 初始化渲染窗口
    renderer.init(800, 600, "Project Rinn - Intent API Test");
    std::cout << "窗口初始化完成" << std::endl;

    // 5. 执行 Lua 脚本
    try {
        ctx.run_file("D:/cs/vs/Project_Rinn/scripts/test.lua");
    } catch (const sol::error& e) {
        std::cerr << "Lua 错误: " << e.what() << std::endl;
        renderer.shutdown();
        return 1;
    }

    // 6. 验证
    std::cout << "=== C++ 验证 ===" << std::endl;
    std::cout << "Registry 实体数: " << reg.size() << std::endl;

    // 7. 主循环
    std::cout << "=== 进入主循环 ===" << std::endl;
    
    sol::protected_function on_update = ctx.state()["on_update"];
    
    while (!renderer.should_close()) {
        float dt = renderer.delta_time();
        
        // 调用 Lua 的 on_update(dt)
        if (on_update.valid()) {
            auto result = on_update(dt);
            if (!result.valid()) {
                sol::error err = result;
                std::cerr << "Lua on_update 错误: " << err.what() << std::endl;
            }
        }
        
        // 物理系统更新（Velocity → Transform）
        physics.update(reg, dt);
        
        renderer.begin_frame(RAYWHITE);
        
        // 渲染所有带 Transform + Sprite 的实体
        renderer.render(reg, rm);
        
        // 显示 FPS 和操作提示
        renderer.draw_text(std::format("FPS: {}", renderer.fps()).c_str(), 10, 10, 20, DARKGRAY);
        renderer.draw_text("WASD: Move | Space: Print Position", 10, 35, 16, GRAY);
        
        renderer.end_frame();
    }

    // 8. 清理
    renderer.shutdown();
    std::cout << "=== 程序结束 ===" << std::endl;
    return 0;
}