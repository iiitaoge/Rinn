#pragma once
#include <sol/sol.hpp>

namespace Rinn {
    // 封装 Lua 虚拟机
    class ScriptContext {
    // RALL，创建一个完整的Lua解释器
    sol::state lua;
    public:
        ScriptContext() {
            // 引入基本函数和数学函数
            lua.open_libraries(sol::lib::base, sol::lib::math);
        }

        // 接受字符串，Lua解释器解释并执行
        void run(const std::string& code) {
            lua.script(code);
        }

        void run_file(const std::string& path) {
            lua.script_file(path);
        }

        // 返回引用，便于绑定C++函数/类到Lua
        sol::state& state() { return lua; }
    };
}