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

    int step2_count = 0;
    View<Transform, RigidBody> view(registry);

    for (Entity e : view) {
        Transform& t = registry.get<Transform>(e);
        RigidBody& rb = registry.get<RigidBody>(e);

        std::cout << std::format("  Entity[idx={}, gen={}]: pos=({:.1f}, {:.1f}), vel=({:.1f}, {:.1f})\n",
            e.index(), e.generation(), t.x, t.y, rb.vx, rb.vy);

        ++step2_count;
    }

    std::cout << std::format("View hit count: {} (expected: {})\n", step2_count, TEST_COUNT);

    // Assertion: 应该遍历到全部实体
    if (step2_count != TEST_COUNT) {
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
    // Test Case 5: 组件移除后，View 不再命中该实体
    // --------------------------------------------------------------------
    print_separator("Test Case 5: Component Removal");

    // 当前存活: entities[0], entities[1], entities[3], entities[4], new_entity
    // (entities[2] 已在 Case 3 销毁)
    
    // 给 new_entity 挂上组件，让它也能被 View 命中
    (void)registry.emplace<Transform>(new_entity, 100.0f, 100.0f);
    (void)registry.emplace<RigidBody>(new_entity, 5.0f, 5.0f);

    // 验证 new_entity 现在能被 View 命中
    {
        int pre_remove_count = 0;
        View<Transform, RigidBody> view_pre(registry);
        for ([[maybe_unused]] Entity e : view_pre) {
            ++pre_remove_count;
        }
        std::cout << std::format("Before removal: View hits {} entities\n", pre_remove_count);
    }

    // 移除 entities[0] 的 RigidBody 组件（但不销毁实体）
    std::cout << std::format("Removing RigidBody from Entity[0] (idx={})...\n", entities[0].index());
    registry.remove<RigidBody>(entities[0]);

    // 验证：entities[0] 仍然存活
    if (!registry.is_alive(entities[0])) {
        std::cerr << "ERROR: Entity[0] should still be alive after component removal!\n";
        return 1;
    }

    // 验证：entities[0] 仍有 Transform
    if (!registry.has<Transform>(entities[0])) {
        std::cerr << "ERROR: Entity[0] should still have Transform!\n";
        return 1;
    }

    // 验证：entities[0] 没有 RigidBody
    if (registry.has<RigidBody>(entities[0])) {
        std::cerr << "ERROR: Entity[0] should NOT have RigidBody after removal!\n";
        return 1;
    }

    // 验证：View<Transform, RigidBody> 不再命中 entities[0]
    {
        int post_remove_count = 0;
        bool found_entity_0 = false;
        View<Transform, RigidBody> view_post(registry);
        
        for (Entity e : view_post) {
            if (e.index() == entities[0].index()) {
                found_entity_0 = true;
            }
            ++post_remove_count;
        }

        std::cout << std::format("After removal: View hits {} entities\n", post_remove_count);

        if (found_entity_0) {
            std::cerr << "ERROR: Entity[0] should NOT be in View after RigidBody removal!\n";
            return 1;
        }
    }

    std::cout << "Test Case 5 PASSED.\n";

    // --------------------------------------------------------------------
    // Test Case 6: 只有部分组件的实体，不被多组件 View 命中
    // --------------------------------------------------------------------
    print_separator("Test Case 6: Signature Filtering");

    // 创建一个只有 Transform 的实体
    Entity partial_entity = registry.create_entity();
    (void)registry.emplace<Transform>(partial_entity, 999.0f, 999.0f);
    // 故意不挂 RigidBody

    std::cout << std::format("Created partial_entity (idx={}) with ONLY Transform\n", 
        partial_entity.index());

    // 验证：View<Transform> 应该命中它
    {
        bool found_in_transform_view = false;
        View<Transform> view_t(registry);
        for (Entity e : view_t) {
            if (e.index() == partial_entity.index()) {
                found_in_transform_view = true;
                break;
            }
        }

        if (!found_in_transform_view) {
            std::cerr << "ERROR: partial_entity should be in View<Transform>!\n";
            return 1;
        }
        std::cout << "partial_entity found in View<Transform>. PASS.\n";
    }

    // 验证：View<Transform, RigidBody> 不应该命中它
    {
        bool found_in_dual_view = false;
        View<Transform, RigidBody> view_tr(registry);
        for (Entity e : view_tr) {
            if (e.index() == partial_entity.index()) {
                found_in_dual_view = true;
                break;
            }
        }

        if (found_in_dual_view) {
            std::cerr << "ERROR: partial_entity should NOT be in View<Transform, RigidBody>!\n";
            return 1;
        }
        std::cout << "partial_entity NOT in View<Transform, RigidBody>. PASS.\n";
    }

    std::cout << "Test Case 6 PASSED.\n";

    // --------------------------------------------------------------------
    // Test Case 7: try_get 安全访问路径
    // --------------------------------------------------------------------
    print_separator("Test Case 7: try_get Safe Access");

    // 场景 1: 对有组件的实体调用 try_get，应返回有效值
    {
        auto result = registry.try_get<Transform>(entities[1]);
        if (!result.has_value()) {
            std::cerr << "ERROR: try_get<Transform> should return value for entities[1]!\n";
            return 1;
        }
        
        Transform& t = result->get();
        std::cout << std::format("try_get<Transform>(entities[1]): ({:.1f}, {:.1f}) - OK\n", t.x, t.y);
    }

    // 场景 2: 对没有该组件的实体调用 try_get，应返回 nullopt
    // (entities[0] 在 Case 5 中已被移除 RigidBody)
    {
        auto result = registry.try_get<RigidBody>(entities[0]);
        if (result.has_value()) {
            std::cerr << "ERROR: try_get<RigidBody> should return nullopt for entities[0]!\n";
            return 1;
        }
        std::cout << "try_get<RigidBody>(entities[0]) == nullopt - OK\n";
    }

    // 场景 3: 对只有 Transform 的 partial_entity 调用 try_get<RigidBody>
    {
        auto result = registry.try_get<RigidBody>(partial_entity);
        if (result.has_value()) {
            std::cerr << "ERROR: try_get<RigidBody> should return nullopt for partial_entity!\n";
            return 1;
        }
        std::cout << "try_get<RigidBody>(partial_entity) == nullopt - OK\n";
    }

    // 场景 4: 对死亡实体调用 try_get，应返回 nullopt (不崩溃)
    {
        auto result = registry.try_get<Transform>(stale_handle);
        if (result.has_value()) {
            std::cerr << "ERROR: try_get should return nullopt for stale handle!\n";
            return 1;
        }
        std::cout << "try_get<Transform>(stale_handle) == nullopt - OK (no crash)\n";
    }

    // 场景 5: try_get 返回的引用可修改
    {
        auto result = registry.try_get<Transform>(entities[3]);
        if (!result.has_value()) {
            std::cerr << "ERROR: try_get<Transform> should return value for entities[3]!\n";
            return 1;
        }

        Transform& t = result->get();
        float old_x = t.x;
        t.x = 12345.0f;

        // 验证修改生效
        Transform& t2 = registry.get<Transform>(entities[3]);
        if (t2.x != 12345.0f) {
            std::cerr << "ERROR: try_get returned non-reference!\n";
            return 1;
        }

        // 还原
        t.x = old_x;
        std::cout << "try_get returns mutable reference - OK\n";
    }

    std::cout << "Test Case 7 PASSED.\n";

    // ====================================================================
    // TIER 2: 边界条件测试
    // ====================================================================

    // --------------------------------------------------------------------
    // Test Case 8: 空 View — 没有任何实体有目标组件
    // --------------------------------------------------------------------
    print_separator("Test Case 8: Empty View");

    // 定义一个新组件类型用于测试（没有任何实体拥有）
    struct Unused {
        int dummy;
    };

    {
        int empty_count = 0;
        View<Unused> empty_view(registry);
        for ([[maybe_unused]] Entity e : empty_view) {
            ++empty_count;
        }

        if (empty_count != 0) {
            std::cerr << "ERROR: Empty View should yield 0 entities!\n";
            return 1;
        }
        std::cout << "View<Unused> iteration count: 0 - OK\n";
    }

    // 额外测试：多组件 View 但其中一个池为空
    {
        int mixed_count = 0;
        View<Transform, Unused> mixed_view(registry);
        for ([[maybe_unused]] Entity e : mixed_view) {
            ++mixed_count;
        }

        if (mixed_count != 0) {
            std::cerr << "ERROR: Mixed View with empty component should yield 0!\n";
            return 1;
        }
        std::cout << "View<Transform, Unused> iteration count: 0 - OK\n";
    }

    std::cout << "Test Case 8 PASSED.\n";

    // --------------------------------------------------------------------
    // Test Case 9: Registry::clear() — 全部重置
    // --------------------------------------------------------------------
    print_separator("Test Case 9: Registry Clear");

    // 保存一些现有 Handle 用于验证
    std::array<Entity, 3> handles_before_clear = { entities[1], entities[3], entities[4] };

    std::cout << std::format("Before clear: registry.size() = {}\n", registry.size());

    // 执行 clear
    registry.clear();

    std::cout << std::format("After clear: registry.size() = {}\n", registry.size());

    if (registry.size() != 0) {
        std::cerr << "ERROR: Registry size should be 0 after clear!\n";
        return 1;
    }

    // 验证旧 Handle 全部失效
    for (size_t i = 0; i < handles_before_clear.size(); ++i) {
        if (registry.is_alive(handles_before_clear[i])) {
            std::cerr << std::format("ERROR: Handle[{}] should be invalid after clear!\n", i);
            return 1;
        }
    }
    std::cout << "All old handles invalidated after clear - OK\n";

    // 验证 View 为空
    {
        int clear_count = 0;
        View<Transform> view_after_clear(registry);
        for ([[maybe_unused]] Entity e : view_after_clear) {
            ++clear_count;
        }
        if (clear_count != 0) {
            std::cerr << "ERROR: View should be empty after clear!\n";
            return 1;
        }
        std::cout << "View<Transform> empty after clear - OK\n";
    }

    // 验证可以继续创建新实体
    Entity post_clear_entity = registry.create_entity();
    (void)registry.emplace<Transform>(post_clear_entity, 1.0f, 1.0f);

    if (!registry.is_alive(post_clear_entity)) {
        std::cerr << "ERROR: New entity after clear should be alive!\n";
        return 1;
    }
    std::cout << std::format("New entity after clear: idx={}, gen={} - OK\n",
        post_clear_entity.index(), post_clear_entity.generation());

    std::cout << "Test Case 9 PASSED.\n";

    // --------------------------------------------------------------------
    // Test Case 10: 大量创建+销毁 — Ring Buffer 压力测试
    // --------------------------------------------------------------------
    print_separator("Test Case 10: Stress Test (10000+ entities)");

    // 先清空，从干净状态开始
    registry.clear();

    constexpr int STRESS_COUNT = 10000;
    std::vector<Entity> stress_entities;
    stress_entities.reserve(STRESS_COUNT);

    // Phase 1: 创建 10000 个实体
    std::cout << std::format("Creating {} entities...\n", STRESS_COUNT);
    for (int i = 0; i < STRESS_COUNT; ++i) {
        Entity e = registry.create_entity();
        (void)registry.emplace<Transform>(e, static_cast<float>(i), static_cast<float>(i));
        stress_entities.push_back(e);
    }

    if (registry.size() != STRESS_COUNT) {
        std::cerr << std::format("ERROR: Expected {} entities, got {}\n", STRESS_COUNT, registry.size());
        return 1;
    }
    std::cout << std::format("Created {} entities - OK\n", STRESS_COUNT);

    // Phase 2: 销毁前 5000 个
    std::cout << "Destroying first 5000 entities...\n";
    for (int i = 0; i < 5000; ++i) {
        registry.destroy_entity(stress_entities[i]);
    }

    if (registry.size() != 5000) {
        std::cerr << std::format("ERROR: Expected 5000 entities, got {}\n", registry.size());
        return 1;
    }

    // 验证前 5000 个 Handle 失效
    for (int i = 0; i < 5000; ++i) {
        if (registry.is_alive(stress_entities[i])) {
            std::cerr << std::format("ERROR: Entity[{}] should be dead!\n", i);
            return 1;
        }
    }
    std::cout << "First 5000 entities destroyed and invalidated - OK\n";

    // Phase 3: 再创建 5000 个（应该复用旧索引）
    std::cout << "Creating 5000 new entities (should reuse indices)...\n";
    int reused_count = 0;
    for (int i = 0; i < 5000; ++i) {
        Entity e = registry.create_entity();
        (void)registry.emplace<Transform>(e, 999.0f, 999.0f);

        // 检查是否复用了旧索引
        if (e.index() < 5000) {
            // 验证 Generation 是否递增
            Entity old_handle = stress_entities[e.index()];
            if (e.generation() == old_handle.generation() + 1) {
                ++reused_count;
            }
        }
    }

    std::cout << std::format("Reused {} indices with incremented generation\n", reused_count);

    if (registry.size() != 10000) {
        std::cerr << std::format("ERROR: Expected 10000 entities, got {}\n", registry.size());
        return 1;
    }

    // Phase 4: 验证 View 遍历正确
    {
        int view_count = 0;
        View<Transform> final_view(registry);
        for ([[maybe_unused]] Entity e : final_view) {
            ++view_count;
        }

        if (view_count != 10000) {
            std::cerr << std::format("ERROR: View should hit 10000 entities, got {}\n", view_count);
            return 1;
        }
        std::cout << std::format("Final View<Transform> hits {} entities - OK\n", view_count);
    }

    std::cout << "Test Case 10 PASSED.\n";

    // --------------------------------------------------------------------
    // Summary
    // --------------------------------------------------------------------
    print_separator("ALL TEST CASES PASSED");
    std::cout << "\n*** ECS Core Validation Complete! ***\n";

    return 0;
}