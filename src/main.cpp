#include <iostream>
#include <format>
#include "Core/Registry.hpp"
#include "components/Components.hpp"
#include <sol/sol.hpp>
#include "Scripting/ScriptContext.hpp"
#include "Scripting/LuaBinder.hpp"

// ============================================================================
// Lua 绑定功能测试
// ============================================================================

int main() {
    using namespace Rinn;

    std::cout << "=== C++ 初始化 ===" << std::endl;

    // 1. 创建 ECS
    Registry reg;
    std::cout << "Registry 创建完成" << std::endl;

    // 2. 创建 Lua 上下文
    ScriptContext ctx;
    std::cout << "ScriptContext 创建完成" << std::endl;

    // 3. 绑定 ECS 到 Lua
    bind_registry(ctx.state(), reg);
    std::cout << "bind_registry 完成" << std::endl;

    // 4. 执行测试脚本
    std::cout << "\n=== 执行 Lua 脚本 ===" << std::endl;
    try {
        ctx.run_file("D:/cs/vs/Project_Rinn/scripts/test.lua");
    } catch (const sol::error& e) {
        std::cerr << "Lua 错误: " << e.what() << std::endl;
        return 1;
    }

    // 5. C++ 侧验证
    std::cout << "=== C++ 验证 ===" << std::endl;
    std::cout << "Registry 当前实体数: " << reg.size() << std::endl;

    // 遍历所有带 Transform 的实体
    std::cout << "遍历 View<Transform>:" << std::endl;
    int count = 0;
    for (Entity e : reg.view<Rinn::Transform>()) {
        auto& t = reg.get<Rinn::Transform>(e);
        std::cout << std::format("  Entity[{}]: x={}, y={}\n", e.index(), t.x, t.y);
        count++;
    }
    std::cout << "共 " << count << " 个实体有 Transform" << std::endl;

    std::cout << "\n=== 测试完成 ===" << std::endl;
    return 0;
}