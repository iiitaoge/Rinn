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
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
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
# 引入 Dear ImGui + rlImGui (raylib 桥接)
# =========================================================
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG master
)
FetchContent_Declare(rlImGui
    GIT_REPOSITORY https://github.com/raylib-extras/rlImGui.git
    GIT_TAG main
)
FetchContent_MakeAvailable(imgui rlImGui)

# =========================================================
# 4. 你的项目配置
# =========================================================
add_executable(${PROJECT_NAME}
    src/main.cpp
    src/Core/ComponentID.hpp
    src/Core/Registry.hpp
    src/Core/SparseSet.hpp
    src/Core/Types.hpp
    src/Components/Components.hpp
    src/Scripting/ScriptContext.hpp
    src/Scripting/LuaBinder.hpp
    src/Resources/ResourceManager.hpp
    src/Systems/RenderSystem.hpp
    src/Systems/InputSystem.hpp
    src/Systems/PhysicSystem.hpp
    src/Systems/CollisionSystem.hpp
    src/UI/DebugUI.hpp
    src/UI/LightUI.hpp

    # Dear ImGui 核心源文件
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp

    # rlImGui 桥接
    ${rlimgui_SOURCE_DIR}/rlImGui.cpp
)

# Include 路径
target_include_directories(${PROJECT_NAME} PRIVATE 
    ${CMAKE_SOURCE_DIR}/src
    ${imgui_SOURCE_DIR}
    ${rlimgui_SOURCE_DIR}
)

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

# =========================================================
# 5. 单元测试 (可选，cmake -DBUILD_TESTS=ON ..)
# =========================================================
option(BUILD_TESTS "Build ECS unit tests with Google Test" OFF)

if(BUILD_TESTS)
    enable_testing()

    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.15.2
    )
    # MSVC: 使用动态 CRT，避免链接冲突
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    add_executable(ecs_tests tests/ecs_test.cpp)

    target_include_directories(ecs_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(ecs_tests PRIVATE GTest::gtest_main)

    if(MSVC)
        target_compile_options(ecs_tests PRIVATE /W4 /permissive- /utf-8)
    endif()

    include(GoogleTest)
    gtest_discover_tests(ecs_tests)
endif()