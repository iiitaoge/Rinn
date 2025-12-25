cmake_minimum_required(VERSION 3.28)

# 1. 项目身份证
project(Project_Rinn 
    VERSION 0.1.0 
    LANGUAGES CXX C
)

# 2. 锁定技术栈：C++ 20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# 关闭编译器扩展（防止 MSVC 偷偷塞私货，保证跨平台）
set(CMAKE_CXX_EXTENSIONS OFF) 

# 3. 依赖管理：自动拉取 Raylib
include(FetchContent)

message(STATUS "正在配置依赖：Raylib... (这可能需要几分钟)")

FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://gitee.com/mirrors/raylib.git
    GIT_TAG        5.5  # 锁定版本 5.5
)

# 这一行是为了防止 Raylib 编译它自带的那几十个示例程序，浪费我们时间
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE) 

# 开始下载并加载 Raylib
FetchContent_MakeAvailable(raylib)

# 4. 引擎核心
# 告诉 CMake，源码在哪里
add_executable(${PROJECT_NAME}
    src/main.cpp
)

# 5. 链接库
# 把 Raylib 的功能“注入” exe 里
target_link_libraries(${PROJECT_NAME} PRIVATE raylib)

# 6. 资源拷贝 (为以后做准备)
# 每次编译，自动把 assets 文件夹复制到 exe 旁边
file(COPY ${CMAKE_SOURCE_DIR}/assets DESTINATION ${CMAKE_BINARY_DIR})