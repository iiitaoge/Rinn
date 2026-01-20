cmake_minimum_required(VERSION 3.28)

project(Project_Rinn 
    VERSION 0.1.0 
    LANGUAGES CXX C
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# =========================================================
# 1. 引入 Raylib (这个你有 Gitee 镜像，保持在线拉取)
# =========================================================
include(FetchContent)
FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://gitee.com/mirrors/raylib.git
    GIT_TAG        5.5
)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(raylib)

# =========================================================
# 2. 引入本地 Lua (手动下载版)
# =========================================================
# 检查你是否放对了位置
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/external/lua/CMakeLists.txt")
    message(FATAL_ERROR "❌ 找不到 Lua！请确保你下载了 marovira/lua 并解压到了 external/lua")
endif()

# 直接添加子目录，就像它是你写的代码一样
add_subdirectory(external/lua)

# =========================================================
# 3. 引入本地 Sol2 (手动下载版)
# =========================================================
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/external/sol2/include")
    message(FATAL_ERROR "❌ 找不到 Sol2！请确保你下载了 sol2 并解压到了 external/sol2")
endif()

# Sol2 是纯头文件库，简单定义一个接口库即可
add_library(sol2 INTERFACE)
target_include_directories(sol2 INTERFACE "${CMAKE_SOURCE_DIR}/external/sol2/include")
# Sol2 需要依赖 Lua
target_link_libraries(sol2 INTERFACE liblua)

# =========================================================
# 4. 你的项目配置
# =========================================================
add_executable(${PROJECT_NAME}
    src/main.cpp
    src/Core/ComponentID.hpp
    src/Core/Registry.hpp
    src/Core/SparseSet.hpp
    src/Core/Types.hpp
    src/components/Components.hpp
    src/Scripting/ScriptContext.hpp
    src/Scripting/LuaBinder.hpp
)

# Include 路径：让 #include <Core/xxx> 能找到
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/src)

# 链接库 (Raylib + Sol2 + Lua)
target_link_libraries(${PROJECT_NAME} PRIVATE 
    raylib 
    sol2 
    liblua
)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE 
    /W4 
    /permissive- 
    /utf-8

    /wd5321     # 忽略Sol2警告
    )
endif()