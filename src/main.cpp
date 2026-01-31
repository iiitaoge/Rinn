#include <iostream>
#include <format>
#include "Core/Registry.hpp"
#include "components/Components.hpp"
#include <sol/sol.hpp>
#include "Scripting/ScriptContext.hpp"
#include "Scripting/LuaBinder.hpp"
#include "Systems/RenderSystem.hpp"

// ============================================================================
// 精灵渲染测试
// ============================================================================

int main() {
    using namespace Rinn;

    std::cout << "=== C++ 初始化 ===" << std::endl;

    // 1. 创建核心系统
    Registry reg;
    ResourceManager rm;
    RenderSystem renderer;
    ScriptContext ctx;

    std::cout << "核心系统创建完成" << std::endl;

    // 2. 绑定 Lua
    bind_registry(ctx.state(), reg);
    bind_resources(ctx.state(), rm);
    std::cout << "Lua 绑定完成" << std::endl;

    // 3. 初始化渲染窗口
    renderer.init(800, 600, "Project Rinn - Sprite Test");
    std::cout << "窗口初始化完成" << std::endl;

    // 4. 执行 Lua 脚本（创建实体和加载贴图）
    try {
        ctx.run_file("D:/cs/vs/Project_Rinn/scripts/test.lua");
    } catch (const sol::error& e) {
        std::cerr << "Lua 错误: " << e.what() << std::endl;
        renderer.shutdown();
        return 1;
    }

    // 5. 验证
    std::cout << "=== C++ 验证 ===" << std::endl;
    std::cout << "Registry 实体数: " << reg.size() << std::endl;

    // 6. 主循环
    std::cout << "=== 进入主循环 ===" << std::endl;
    while (!renderer.should_close()) {
        renderer.begin_frame(RAYWHITE);
        
        // 渲染所有带 Transform + Sprite 的实体
        renderer.render(reg, rm);
        
        // 显示 FPS
        renderer.draw_text(std::format("FPS: {}", renderer.fps()).c_str(), 10, 10, 20, DARKGRAY);
        
        renderer.end_frame();
    }

    // 7. 清理
    renderer.shutdown();
    std::cout << "=== 程序结束 ===" << std::endl;
    return 0;
}