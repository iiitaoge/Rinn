#pragma once
#include <sol/sol.hpp>

namespace Rinn {
	inline void Init_lua(sol::state& lua) {
		lua.open_libraries(sol::lib::base, sol::lib::math); // 记得开启基础库，否则无法使用 print 等功能
	}
}