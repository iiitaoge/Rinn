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
    // Test Case 3: 销毁一个实体，再遍历，验证数量 -1
    // --------------------------------------------------------------------
    print_separator("Test Case 3: Destroy Entity & Re-iterate");

    // 保存旧 Handle，用于 Test Case 4
    Entity stale_handle = entities[2];  // 保存 Entity[2] 的旧 Handle
    
    std::cout << std::format("Destroying Entity[2]: index={}, gen={}\n", 
        stale_handle.index(), stale_handle.generation());

    registry.destroy_entity(entities[2]);

    std::cout << std::format("Registry size after destroy: {} (expected: {})\n", 
        registry.size(), TEST_COUNT - 1);

    if (registry.size() != TEST_COUNT - 1) {
        std::cerr << "ERROR: Registry size mismatch after destroy!\n";
        return 1;
    }

    // 重新遍历 View
    int count_after_destroy = 0;
    View<Transform, RigidBody> view_after(registry);

    std::cout << "View after destroy:\n";
    for (Entity e : view_after) {
        Transform& t = registry.get<Transform>(e);
        std::cout << std::format("  Entity[idx={}, gen={}]: pos=({:.1f}, {:.1f})\n",
            e.index(), e.generation(), t.x, t.y);
        ++count_after_destroy;
    }

    std::cout << std::format("View hit count: {} (expected: {})\n", 
        count_after_destroy, TEST_COUNT - 1);

    if (count_after_destroy != TEST_COUNT - 1) {
        std::cerr << "ERROR: View count mismatch after destroy!\n";
        return 1;
    }

    std::cout << "Test Case 3 PASSED.\n";

    // --------------------------------------------------------------------
    // Test Case 4: 用旧 Handle 访问，验证 is_alive() == false
    // --------------------------------------------------------------------
    print_separator("Test Case 4: Stale Handle Validation");

    std::cout << std::format("Checking stale_handle: index={}, gen={}\n",
        stale_handle.index(), stale_handle.generation());

    if (registry.is_alive(stale_handle)) {
        std::cerr << "ERROR: Stale handle should NOT be alive!\n";
        return 1;
    }

    std::cout << "is_alive(stale_handle) == false. PASS.\n";

    // 额外测试：创建新实体，验证 Generation 递增
    Entity new_entity = registry.create_entity();
    std::cout << std::format("New entity created: index={}, gen={}\n",
        new_entity.index(), new_entity.generation());

    // 如果新实体复用了旧索引，Generation 应该 +1
    if (new_entity.index() == stale_handle.index()) {
        if (new_entity.generation() != stale_handle.generation() + 1) {
            std::cerr << "ERROR: Generation should be incremented on reuse!\n";
            return 1;
        }
        std::cout << "Index reused with incremented generation. PASS.\n";
    } else {
        std::cout << "New index allocated (no reuse yet). OK.\n";
    }

    // 验证旧 Handle 仍然无效
    if (registry.is_alive(stale_handle)) {
        std::cerr << "ERROR: Stale handle should still be invalid after new entity creation!\n";
        return 1;
    }

    std::cout << "Stale handle remains invalid after reuse. PASS.\n";

    // --------------------------------------------------------------------
    // Summary
    // --------------------------------------------------------------------
    print_separator("ALL TEST CASES PASSED");

    return 0;
}