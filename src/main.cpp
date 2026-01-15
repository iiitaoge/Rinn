#include <iostream>
#include <format>
#include "Core/Registry.hpp"
#include "components/Components.hpp"

// ============================================================================
// Test Case 1: 创建 N 个实体，挂上 Position + Velocity，验证 View 遍历
// ============================================================================

void print_separator(const char* title) {
    std::cout << "\n========== " << title << " ==========\n";
}

int main() {
    using namespace Rinn;

    Registry registry;
    constexpr int TEST_COUNT = 5;

    // --------------------------------------------------------------------
    // Step 1: 创建实体并挂载组件
    // --------------------------------------------------------------------
    print_separator("Step 1: Create Entities & Emplace Components");

    std::array<Entity, TEST_COUNT> entities;

    for (int i = 0; i < TEST_COUNT; ++i) {
        entities[i] = registry.create_entity();
        
        // 挂载 Transform (Position)
        (void)registry.emplace<Transform>(entities[i], 
            static_cast<float>(i * 10),     // x
            static_cast<float>(i * 10 + 5)  // y
        );
        
        // 挂载 RigidBody (Velocity)
        (void)registry.emplace<RigidBody>(entities[i], 
            static_cast<float>(i),          // vx
            static_cast<float>(i * 2)       // vy
        );

        std::cout << std::format("Entity[{}] created: index={}, gen={}\n", 
            i, entities[i].index(), entities[i].generation());
    }

    std::cout << std::format("Registry size: {} entities\n", registry.size());

    // --------------------------------------------------------------------
    // Step 2: 使用 View<Transform, RigidBody> 遍历
    // --------------------------------------------------------------------
    print_separator("Step 2: View<Transform, RigidBody> Iteration");

    int count = 0;
    View<Transform, RigidBody> view(registry);

    for (Entity e : view) {
        Transform& t = registry.get<Transform>(e);
        RigidBody& rb = registry.get<RigidBody>(e);

        std::cout << std::format("  Entity[idx={}, gen={}]: pos=({:.1f}, {:.1f}), vel=({:.1f}, {:.1f})\n",
            e.index(), e.generation(), t.x, t.y, rb.vx, rb.vy);

        ++count;
    }

    std::cout << std::format("View hit count: {} (expected: {})\n", count, TEST_COUNT);

    // Assertion: 应该遍历到全部实体
    if (count != TEST_COUNT) {
        std::cerr << "ERROR: View count mismatch!\n";
        return 1;
    }

    // --------------------------------------------------------------------
    // Step 3: 验证单个组件获取
    // --------------------------------------------------------------------
    print_separator("Step 3: Component Access Verification");

    for (int i = 0; i < TEST_COUNT; ++i) {
        if (!registry.is_alive(entities[i])) {
            std::cerr << std::format("ERROR: Entity[{}] should be alive!\n", i);
            return 1;
        }

        if (!registry.has<Transform>(entities[i])) {
            std::cerr << std::format("ERROR: Entity[{}] should have Transform!\n", i);
            return 1;
        }

        if (!registry.has<RigidBody>(entities[i])) {
            std::cerr << std::format("ERROR: Entity[{}] should have RigidBody!\n", i);
            return 1;
        }
    }

    std::cout << "All entities alive and have required components. PASS.\n";

    // --------------------------------------------------------------------
    // Summary
    // --------------------------------------------------------------------
    print_separator("Test Case 1 PASSED");

    return 0;
}