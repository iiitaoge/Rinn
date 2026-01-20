#include <iostream>
#include <format>
#include "Core/Registry.hpp"
#include "components/Components.hpp"
#include <sol/sol.hpp>
#include "Scripting/ScriptContext.hpp"
#include <Scripting/LuaBinder.hpp>
// ============================================================================
// Test Case 1: 创建 N 个实体，挂上 Position + Velocity，验证 View 遍历
// ============================================================================


int main() {
    using namespace Rinn;

    Registry reg;           // 创建 Registry
    Rinn::ScriptContext ctx;

    bind_registry(ctx.state(), reg);  // ← 绑定到 Lua

    ctx.run("print('hello')");
    ctx.run_file("D:/cs/vs/Project_Rinn/scripts/test.lua");

    return 0;
}