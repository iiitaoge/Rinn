cmake_minimum_required(VERSION 3.28)

# ============================================================================
# PROJECT RINN: CMake Configuration
# ============================================================================

# 1. Project Declaration
project(Project_Rinn 
    VERSION 0.1.0 
    LANGUAGES CXX C
)

# 2. C++ Standard Configuration - C++20 Strict
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)  # Disable compiler-specific extensions for cross-platform compatibility

# 3. FetchContent Configuration for Raylib
include(FetchContent)

message(STATUS "Fetching Raylib dependency... (This may take a while on first run)")

FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://gitee.com/mirrors/raylib.git
    GIT_TAG        5.5  # Raylib version 5.5
)

# Disable examples to prevent dozens of warnings during build
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE) 

# Fetch and make Raylib available
FetchContent_MakeAvailable(raylib)

# 4. Executable Target
# Add all source files to the executable
add_executable(${PROJECT_NAME}
    # Main entry point
    src/main.cpp
    
    # Core ECS Components
    src/Core/ComponentID.hpp
    src/Core/Registry.hpp
    src/Core/SparseSet.hpp
    src/Core/Types.hpp
    
    # Game Components
    src/components/Components.hpp
)

# 5. Link Libraries
# Link Raylib functionality to the executable
target_link_libraries(${PROJECT_NAME} PRIVATE raylib)

# 6. Source Group Organization for Visual Studio
# Organize files in VS Solution Explorer into logical groups
source_group("Source Files\\Core" FILES
    src/Core/ComponentID.hpp
    src/Core/Registry.hpp
    src/Core/SparseSet.hpp
    src/Core/Types.hpp
)

source_group("Source Files\\Components" FILES
    src/components/Components.hpp
)

source_group("Source Files" FILES
    src/main.cpp
)

# Organize documentation files into "AI_Mentor" virtual folder
source_group("AI_Mentor\\Context" FILES
    docs/context/context_ecs.txt
    docs/context/context_raylib.txt
    docs/context/context_rllm.txt
    docs/context/context_sol2.txt
)

source_group("AI_Mentor" FILES
    docs/progress.md
    docs/PROJECT_MANIFEST.md
)

# 7. Asset Copy (for future use)
# Automatically copy assets folder to executable directory on each build
if(EXISTS ${CMAKE_SOURCE_DIR}/assets)
    file(COPY ${CMAKE_SOURCE_DIR}/assets DESTINATION ${CMAKE_BINARY_DIR})
endif()
