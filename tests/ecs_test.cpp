// ============================================================================
// ECS 引擎核心单元测试
// ============================================================================
// 测试范围：
//   (1) SparseSet 增删改查的边界
//   (2) Entity 生命周期（创建→销毁→复用→版本检查）
//   (3) View 遍历的正确性
//   (4) System 的确定性（相同输入 → 相同输出）
// ============================================================================
// 构建方式：cmake -DBUILD_TESTS=ON ..
// ============================================================================

#include <gtest/gtest.h>
#include "Core/Types.hpp"
#include "Core/SparseSet.hpp"
#include "Core/ComponentID.hpp"
#include "Core/Registry.hpp"

#include <vector>
#include <algorithm>
#include <unordered_set>

// ============================================================================
// 测试专用组件（与游戏组件解耦，避免 raylib 依赖）
// ============================================================================
struct Position { float x, y; };
struct Speed    { float vx, vy; };
struct Health   { int hp; };
struct TagA     {};
struct TagB     {};


// ████████████████████████████████████████████████████████████████████████████
// (1) SparseSet 增删改查的边界
// ████████████████████████████████████████████████████████████████████████████

class SparseSetTest : public ::testing::Test {
protected:
    Rinn::SparseSet<Position> pool;
};

// --- emplace ---

TEST_F(SparseSetTest, Emplace_BasicAddAndGet) {
    Entity e(0, 0);
    auto& pos = pool.emplace(e, Position{1.0f, 2.0f});

    EXPECT_FLOAT_EQ(pos.x, 1.0f);
    EXPECT_FLOAT_EQ(pos.y, 2.0f);
    EXPECT_TRUE(pool.has(e));
    EXPECT_EQ(pool.size(), 1u);
}

TEST_F(SparseSetTest, Emplace_DuplicateReturnsExisting_NotReplace) {
    // 核心语义：重复 emplace 不覆盖，返回已有引用
    Entity e(0, 0);
    pool.emplace(e, Position{1.0f, 2.0f});
    auto& pos = pool.emplace(e, Position{99.0f, 99.0f});

    EXPECT_FLOAT_EQ(pos.x, 1.0f);   // 仍是原值
    EXPECT_FLOAT_EQ(pos.y, 2.0f);
    EXPECT_EQ(pool.size(), 1u);       // 数量不变
}

TEST_F(SparseSetTest, Emplace_MultipleEntities) {
    for (uint16_t i = 0; i < 100; ++i) {
        Entity e(i, 0);
        auto& pos = pool.emplace(e, Position{static_cast<float>(i), 0.0f});
        EXPECT_FLOAT_EQ(pos.x, static_cast<float>(i));
    }
    EXPECT_EQ(pool.size(), 100u);
}

// --- get ---

TEST_F(SparseSetTest, Get_MutableModification) {
    Entity e(5, 0);
    pool.emplace(e, Position{0.0f, 0.0f});

    // 通过引用修改
    pool.get(e).x = 42.0f;
    pool.get(e).y = -7.0f;

    EXPECT_FLOAT_EQ(pool.get(e).x, 42.0f);
    EXPECT_FLOAT_EQ(pool.get(e).y, -7.0f);
}

TEST_F(SparseSetTest, Get_ConstCorrectness) {
    Entity e(3, 0);
    pool.emplace(e, Position{10.0f, 20.0f});

    const auto& const_pool = pool;
    const auto& pos = const_pool.get(e);

    EXPECT_FLOAT_EQ(pos.x, 10.0f);
    EXPECT_FLOAT_EQ(pos.y, 20.0f);
}

// --- remove: swap-and-pop 正确性 ---

TEST_F(SparseSetTest, Remove_MiddleElement_SwapAndPop) {
    Entity e0(0, 0), e1(1, 0), e2(2, 0);
    pool.emplace(e0, Position{0.0f, 0.0f});
    pool.emplace(e1, Position{1.0f, 1.0f});
    pool.emplace(e2, Position{2.0f, 2.0f});

    pool.remove(e1);  // 删除中间 → 队尾(e2)移到 e1 的位置

    EXPECT_FALSE(pool.has(e1));
    EXPECT_TRUE(pool.has(e0));
    EXPECT_TRUE(pool.has(e2));
    EXPECT_EQ(pool.size(), 2u);

    // 数据完整性：移动后仍能正确 get
    EXPECT_FLOAT_EQ(pool.get(e0).x, 0.0f);
    EXPECT_FLOAT_EQ(pool.get(e2).x, 2.0f);
}

TEST_F(SparseSetTest, Remove_FirstElement_SwapAndPop) {
    Entity e0(0, 0), e1(1, 0), e2(2, 0);
    pool.emplace(e0, Position{10.0f, 10.0f});
    pool.emplace(e1, Position{20.0f, 20.0f});
    pool.emplace(e2, Position{30.0f, 30.0f});

    pool.remove(e0);  // 删除头部

    EXPECT_FALSE(pool.has(e0));
    EXPECT_TRUE(pool.has(e1));
    EXPECT_TRUE(pool.has(e2));

    EXPECT_FLOAT_EQ(pool.get(e1).x, 20.0f);
    EXPECT_FLOAT_EQ(pool.get(e2).x, 30.0f);
}

TEST_F(SparseSetTest, Remove_LastElement_DirectPop) {
    Entity e(0, 0);
    pool.emplace(e, Position{1.0f, 1.0f});

    pool.remove(e);

    EXPECT_FALSE(pool.has(e));
    EXPECT_EQ(pool.size(), 0u);
}

TEST_F(SparseSetTest, Remove_NonExistent_NoOp) {
    Entity e(42, 0);
    // 不应崩溃
    pool.remove(e);
    EXPECT_EQ(pool.size(), 0u);
}

TEST_F(SparseSetTest, Remove_ThenReAdd) {
    Entity e(0, 0);
    pool.emplace(e, Position{1.0f, 1.0f});
    pool.remove(e);

    // 删除后可以重新添加
    auto& pos = pool.emplace(e, Position{999.0f, 888.0f});
    EXPECT_TRUE(pool.has(e));
    EXPECT_FLOAT_EQ(pos.x, 999.0f);
    EXPECT_EQ(pool.size(), 1u);
}

// --- clear ---

TEST_F(SparseSetTest, Clear_ResetsEverything) {
    for (uint16_t i = 0; i < 100; ++i) {
        pool.emplace(Entity(i, 0), Position{static_cast<float>(i), 0.0f});
    }
    EXPECT_EQ(pool.size(), 100u);

    pool.clear();
    EXPECT_EQ(pool.size(), 0u);

    // clear 之后 has 全部返回 false
    for (uint16_t i = 0; i < 100; ++i) {
        EXPECT_FALSE(pool.has(Entity(i, 0)));
    }
}

TEST_F(SparseSetTest, Clear_ThenReAdd) {
    pool.emplace(Entity(0, 0), Position{1.0f, 1.0f});
    pool.clear();

    auto& pos = pool.emplace(Entity(0, 0), Position{42.0f, 42.0f});
    EXPECT_TRUE(pool.has(Entity(0, 0)));
    EXPECT_FLOAT_EQ(pos.x, 42.0f);
}

// --- 边界条件 ---

TEST_F(SparseSetTest, Boundary_EntityIndex0) {
    Entity e(0, 0);
    pool.emplace(e, Position{0.0f, 0.0f});
    EXPECT_TRUE(pool.has(e));
    EXPECT_FLOAT_EQ(pool.get(e).x, 0.0f);
}

TEST_F(SparseSetTest, Boundary_EntityIndexMax) {
    Entity e(MAX_ENTITIES - 1, 0);
    pool.emplace(e, Position{1.0f, 1.0f});
    EXPECT_TRUE(pool.has(e));
    EXPECT_FLOAT_EQ(pool.get(e).x, 1.0f);
}

TEST_F(SparseSetTest, Boundary_EntityWithHighGeneration) {
    // 高版本号的实体应该正常工作（SparseSet 只用 index）
    Entity e(5, 9999);
    pool.emplace(e, Position{7.0f, 8.0f});
    EXPECT_TRUE(pool.has(e));
    EXPECT_FLOAT_EQ(pool.get(e).x, 7.0f);
}

// --- entity_data() ---

TEST_F(SparseSetTest, EntityData_PointerConsistency) {
    Entity e0(0, 0), e1(1, 0), e2(2, 0);
    pool.emplace(e0, Position{0, 0});
    pool.emplace(e1, Position{1, 1});
    pool.emplace(e2, Position{2, 2});

    const Entity* data = pool.entity_data();
    EXPECT_EQ(data[0], e0);
    EXPECT_EQ(data[1], e1);
    EXPECT_EQ(data[2], e2);
}

TEST_F(SparseSetTest, EntityData_AfterSwapAndPop) {
    Entity e0(0, 0), e1(1, 0), e2(2, 0);
    pool.emplace(e0, Position{0, 0});
    pool.emplace(e1, Position{1, 1});
    pool.emplace(e2, Position{2, 2});

    pool.remove(e0);  // e2 移到 index 0

    const Entity* data = pool.entity_data();
    EXPECT_EQ(pool.size(), 2u);
    // e2 被交换到了 e0 的位置
    EXPECT_EQ(data[0], e2);
    EXPECT_EQ(data[1], e1);
}

// --- 迭代器 ---

TEST_F(SparseSetTest, Iterator_RangeForLoop) {
    pool.emplace(Entity(0, 0), Position{10.0f, 0.0f});
    pool.emplace(Entity(1, 0), Position{20.0f, 0.0f});
    pool.emplace(Entity(2, 0), Position{30.0f, 0.0f});

    float sum = 0.0f;
    for (const auto& pos : pool) {
        sum += pos.x;
    }
    EXPECT_FLOAT_EQ(sum, 60.0f);
}


// ████████████████████████████████████████████████████████████████████████████
// (2) Entity 生命周期（创建→销毁→复用→版本检查）
// ████████████████████████████████████████████████████████████████████████████

class EntityLifecycleTest : public ::testing::Test {
protected:
    Rinn::Registry registry;
};

// --- 创建 ---

TEST_F(EntityLifecycleTest, Create_FirstEntityIsIndex0Gen0) {
    Entity e = registry.create_entity();

    EXPECT_EQ(e.index(), 0);
    EXPECT_EQ(e.generation(), 0);
    EXPECT_TRUE(registry.is_alive(e));
}

TEST_F(EntityLifecycleTest, Create_SequentialIndices) {
    Entity e0 = registry.create_entity();
    Entity e1 = registry.create_entity();
    Entity e2 = registry.create_entity();

    EXPECT_EQ(e0.index(), 0);
    EXPECT_EQ(e1.index(), 1);
    EXPECT_EQ(e2.index(), 2);
    EXPECT_EQ(registry.size(), 3u);
}

TEST_F(EntityLifecycleTest, Create_AllUniqueHandles) {
    std::vector<Entity> entities;
    for (int i = 0; i < 1000; ++i) {
        entities.push_back(registry.create_entity());
    }

    // 所有 id 唯一
    std::unordered_set<uint32_t> ids;
    for (auto e : entities) {
        ids.insert(e.id);
    }
    EXPECT_EQ(ids.size(), 1000u);
}

// --- 销毁 ---

TEST_F(EntityLifecycleTest, Destroy_MakesEntityInvalid) {
    Entity e = registry.create_entity();
    registry.destroy_entity(e);

    EXPECT_FALSE(registry.is_alive(e));
    EXPECT_EQ(registry.size(), 0u);
}

TEST_F(EntityLifecycleTest, Destroy_RemovesAllComponents) {
    Entity e = registry.create_entity();
    (void)registry.emplace<Position>(e, Position{1, 2});
    (void)registry.emplace<Health>(e, Health{100});

    registry.destroy_entity(e);

    // 重新创建同 index 的实体，组件应为空
    Entity reused = registry.create_entity();
    EXPECT_FALSE(registry.has<Position>(reused));
    EXPECT_FALSE(registry.has<Health>(reused));
}

TEST_F(EntityLifecycleTest, Destroy_MiddleEntity_OthersUnaffected) {
    Entity e0 = registry.create_entity();
    Entity e1 = registry.create_entity();
    Entity e2 = registry.create_entity();

    (void)registry.emplace<Position>(e0, Position{0, 0});
    (void)registry.emplace<Position>(e1, Position{1, 1});
    (void)registry.emplace<Position>(e2, Position{2, 2});

    registry.destroy_entity(e1);

    EXPECT_TRUE(registry.is_alive(e0));
    EXPECT_FALSE(registry.is_alive(e1));
    EXPECT_TRUE(registry.is_alive(e2));

    // 其他实体的组件不受影响
    EXPECT_FLOAT_EQ(registry.get<Position>(e0).x, 0.0f);
    EXPECT_FLOAT_EQ(registry.get<Position>(e2).x, 2.0f);
}

// --- 复用 ---

TEST_F(EntityLifecycleTest, Reuse_SameIndex_BumpedGeneration) {
    Entity e0 = registry.create_entity();
    uint16_t old_index = e0.index();

    registry.destroy_entity(e0);
    Entity e1 = registry.create_entity();

    EXPECT_EQ(e1.index(), old_index);     // 相同索引
    EXPECT_EQ(e1.generation(), 1);         // 版本+1
    EXPECT_TRUE(registry.is_alive(e1));
}

TEST_F(EntityLifecycleTest, Reuse_OldHandleIsStale) {
    Entity original = registry.create_entity();
    registry.destroy_entity(original);
    Entity reused = registry.create_entity();

    // 旧 Handle 已失效（版本不匹配）
    EXPECT_FALSE(registry.is_alive(original));
    // 新 Handle 有效
    EXPECT_TRUE(registry.is_alive(reused));
    // 二者 index 相同但 id 不同
    EXPECT_EQ(original.index(), reused.index());
    EXPECT_NE(original.id, reused.id);
}

TEST_F(EntityLifecycleTest, Reuse_MultiCycle_GenerationIncrements) {
    Entity e = registry.create_entity();

    for (int cycle = 1; cycle <= 10; ++cycle) {
        registry.destroy_entity(e);
        EXPECT_FALSE(registry.is_alive(e));

        Entity next = registry.create_entity();
        EXPECT_EQ(next.index(), 0);                    // 始终复用 index 0
        EXPECT_EQ(next.generation(), static_cast<uint16_t>(cycle));
        EXPECT_TRUE(registry.is_alive(next));

        e = next;
    }
}

TEST_F(EntityLifecycleTest, Reuse_FIFO_Order) {
    // 验证环形缓冲区的 FIFO 复用顺序
    Entity e0 = registry.create_entity();  // index 0
    Entity e1 = registry.create_entity();  // index 1
    Entity e2 = registry.create_entity();  // index 2

    registry.destroy_entity(e0);  // 入队：[0]
    registry.destroy_entity(e1);  // 入队：[0, 1]
    registry.destroy_entity(e2);  // 入队：[0, 1, 2]

    Entity r0 = registry.create_entity();  // 出队：index 0
    Entity r1 = registry.create_entity();  // 出队：index 1
    Entity r2 = registry.create_entity();  // 出队：index 2

    EXPECT_EQ(r0.index(), 0);
    EXPECT_EQ(r1.index(), 1);
    EXPECT_EQ(r2.index(), 2);

    // 全部 generation == 1
    EXPECT_EQ(r0.generation(), 1);
    EXPECT_EQ(r1.generation(), 1);
    EXPECT_EQ(r2.generation(), 1);
}

// --- 版本检查 ---

TEST_F(EntityLifecycleTest, Version_NullEntityIsInvalid) {
    Entity null_e;  // 默认构造 = NULL_ID
    EXPECT_TRUE(null_e.is_null());
}

TEST_F(EntityLifecycleTest, Version_HandleLayout) {
    // 验证 Handle 的位布局：[generation(16) | index(16)]
    Entity e(42, 7);  // index=42, generation=7

    EXPECT_EQ(e.index(), 42);
    EXPECT_EQ(e.generation(), 7);
    EXPECT_EQ(e.id, (uint32_t(7) << 16) | 42);
}

// --- Registry::clear ---

TEST_F(EntityLifecycleTest, RegistryClear_ResetsEverything) {
    for (int i = 0; i < 100; ++i) {
        Entity e = registry.create_entity();
        (void)registry.emplace<Position>(e, Position{0, 0});
    }
    EXPECT_EQ(registry.size(), 100u);

    registry.clear();
    EXPECT_EQ(registry.size(), 0u);

    // clear 后创建新实体从 index 0 开始
    Entity e = registry.create_entity();
    EXPECT_EQ(e.index(), 0);
    EXPECT_EQ(e.generation(), 0);
}


// ████████████████████████████████████████████████████████████████████████████
// (3) View 遍历的正确性
// ████████████████████████████████████████████████████████████████████████████

class ViewTest : public ::testing::Test {
protected:
    Rinn::Registry registry;
};

// --- 单组件 View ---

TEST_F(ViewTest, SingleComponent_MatchesOnly) {
    Entity e0 = registry.create_entity();
    Entity e1 = registry.create_entity();
    Entity e2 = registry.create_entity();

    (void)registry.emplace<Position>(e0, Position{0, 0});
    (void)registry.emplace<Position>(e1, Position{1, 1});
    // e2: 无 Position

    int count = 0;
    for (Entity e : registry.view<Position>()) {
        EXPECT_TRUE(registry.has<Position>(e));
        count++;
    }
    EXPECT_EQ(count, 2);
}

// --- 多组件 View (交集) ---

TEST_F(ViewTest, MultiComponent_Intersection) {
    Entity e0 = registry.create_entity();
    Entity e1 = registry.create_entity();
    Entity e2 = registry.create_entity();

    // e0: Position + Speed  ✓ 匹配
    (void)registry.emplace<Position>(e0, Position{0, 0});
    (void)registry.emplace<Speed>(e0, Speed{1, 0});

    // e1: Position only     ✗ 不匹配
    (void)registry.emplace<Position>(e1, Position{1, 1});

    // e2: Speed only        ✗ 不匹配
    (void)registry.emplace<Speed>(e2, Speed{0, 1});

    int count = 0;
    for (Entity e : registry.view<Position, Speed>()) {
        EXPECT_TRUE(registry.has<Position>(e));
        EXPECT_TRUE(registry.has<Speed>(e));
        count++;
    }
    EXPECT_EQ(count, 1);  // 只有 e0
}

TEST_F(ViewTest, ThreeComponent_Intersection) {
    Entity e0 = registry.create_entity();
    Entity e1 = registry.create_entity();

    // e0: 三组件全有
    (void)registry.emplace<Position>(e0, Position{0, 0});
    (void)registry.emplace<Speed>(e0, Speed{1, 1});
    (void)registry.emplace<Health>(e0, Health{100});

    // e1: 只有两个
    (void)registry.emplace<Position>(e1, Position{0, 0});
    (void)registry.emplace<Speed>(e1, Speed{1, 1});

    int count = 0;
    for ([[maybe_unused]] Entity e : registry.view<Position, Speed, Health>()) {
        count++;
    }
    EXPECT_EQ(count, 1);  // 只有 e0
}

// --- 空 View ---

TEST_F(ViewTest, EmptyView_NoEntities) {
    int count = 0;
    for ([[maybe_unused]] Entity e : registry.view<Position>()) {
        count++;
    }
    EXPECT_EQ(count, 0);
}

TEST_F(ViewTest, EmptyView_NoMatch) {
    Entity e = registry.create_entity();
    (void)registry.emplace<Position>(e, Position{0, 0});

    // 查询 Speed，没有实体匹配
    int count = 0;
    for ([[maybe_unused]] Entity e : registry.view<Speed>()) {
        count++;
    }
    EXPECT_EQ(count, 0);
}

// --- Tag 组件过滤 ---

TEST_F(ViewTest, TagComponent_Filter) {
    Entity player  = registry.create_entity();
    Entity enemy   = registry.create_entity();
    Entity neutral = registry.create_entity();

    (void)registry.emplace<Position>(player,  Position{0, 0});
    (void)registry.emplace<Position>(enemy,   Position{1, 1});
    (void)registry.emplace<Position>(neutral, Position{2, 2});

    (void)registry.emplace<TagA>(player);   // TagA = "玩家"
    (void)registry.emplace<TagB>(enemy);    // TagB = "敌人"
    // neutral: 无标签

    // 只遍历"玩家"
    int count_a = 0;
    for (Entity e : registry.view<Position, TagA>()) {
        EXPECT_EQ(e.index(), player.index());
        count_a++;
    }
    EXPECT_EQ(count_a, 1);

    // 只遍历"敌人"
    int count_b = 0;
    for (Entity e : registry.view<Position, TagB>()) {
        EXPECT_EQ(e.index(), enemy.index());
        count_b++;
    }
    EXPECT_EQ(count_b, 1);
}

// --- View 数据访问正确性 ---

TEST_F(ViewTest, DataAccess_CorrectValues) {
    Entity e0 = registry.create_entity();
    Entity e1 = registry.create_entity();

    (void)registry.emplace<Position>(e0, Position{10.0f, 20.0f});
    (void)registry.emplace<Speed>(e0, Speed{1.0f, 2.0f});

    (void)registry.emplace<Position>(e1, Position{30.0f, 40.0f});
    (void)registry.emplace<Speed>(e1, Speed{3.0f, 4.0f});

    float sum_x = 0.0f;
    for (Entity e : registry.view<Position, Speed>()) {
        sum_x += registry.get<Position>(e).x;
        sum_x += registry.get<Speed>(e).vx;
    }
    // (10 + 1) + (30 + 3) = 44
    EXPECT_FLOAT_EQ(sum_x, 44.0f);
}

// --- 销毁后 View 的正确性 ---

TEST_F(ViewTest, AfterDestroy_ExcludesDeadEntity) {
    Entity e0 = registry.create_entity();
    Entity e1 = registry.create_entity();
    Entity e2 = registry.create_entity();

    (void)registry.emplace<Position>(e0, Position{0, 0});
    (void)registry.emplace<Position>(e1, Position{1, 1});
    (void)registry.emplace<Position>(e2, Position{2, 2});

    registry.destroy_entity(e1);

    // 销毁后创建 View → e1 不应出现
    int count = 0;
    for (Entity e : registry.view<Position>()) {
        EXPECT_NE(e.index(), e1.index());
        count++;
    }
    EXPECT_EQ(count, 2);
}

// --- 移除组件后 View 的正确性 ---

TEST_F(ViewTest, AfterRemoveComponent_ExcludesEntity) {
    Entity e0 = registry.create_entity();
    Entity e1 = registry.create_entity();

    (void)registry.emplace<Position>(e0, Position{0, 0});
    (void)registry.emplace<Speed>(e0, Speed{1, 1});

    (void)registry.emplace<Position>(e1, Position{1, 1});
    (void)registry.emplace<Speed>(e1, Speed{2, 2});

    // 移除 e1 的 Speed（不销毁实体）
    registry.remove<Speed>(e1);

    int count = 0;
    for (Entity e : registry.view<Position, Speed>()) {
        EXPECT_EQ(e.index(), e0.index());  // 只剩 e0
        count++;
    }
    EXPECT_EQ(count, 1);
}

// --- View 收集所有 Entity ---

TEST_F(ViewTest, CollectAll_CorrectEntities) {
    std::vector<Entity> created;
    for (int i = 0; i < 50; ++i) {
        Entity e = registry.create_entity();
        (void)registry.emplace<Position>(e, Position{static_cast<float>(i), 0});
        created.push_back(e);
    }

    std::vector<Entity> collected;
    for (Entity e : registry.view<Position>()) {
        collected.push_back(e);
    }

    EXPECT_EQ(collected.size(), 50u);

    // 验证每个创建的实体都被遍历到
    for (auto& ce : created) {
        bool found = std::any_of(collected.begin(), collected.end(),
            [&](Entity e) { return e.id == ce.id; });
        EXPECT_TRUE(found) << "Entity index=" << ce.index() << " not found in View";
    }
}

// --- View 遍历中的"最小池"优化 ---

TEST_F(ViewTest, SmallestPool_Optimization) {
    // Position: 100 个实体
    for (int i = 0; i < 100; ++i) {
        Entity e = registry.create_entity();
        (void)registry.emplace<Position>(e, Position{0, 0});

        // 只有前 5 个同时有 Speed
        if (i < 5) {
            (void)registry.emplace<Speed>(e, Speed{0, 0});
        }
    }

    // view<Position, Speed> 应选 Speed 池（size=5）作为驱动
    int count = 0;
    for ([[maybe_unused]] Entity e : registry.view<Position, Speed>()) {
        count++;
    }
    EXPECT_EQ(count, 5);
}


// ████████████████████████████████████████████████████████████████████████████
// (4) System 的确定性（相同输入 → 相同输出）
// ████████████████████████████████████████████████████████████████████████████

// 测试用 Physics System（与真实版本逻辑一致，但不依赖 raylib）
namespace TestPhysics {
    inline void Update(Rinn::Registry& registry, float dt) {
        for (Entity entity : registry.view<Position, Speed>()) {
            auto& pos = registry.get<Position>(entity);
            auto& spd = registry.get<Speed>(entity);
            pos.x += spd.vx * dt;
            pos.y += spd.vy * dt;
        }
    }
}

class SystemDeterminismTest : public ::testing::Test {
protected:
    Rinn::Registry reg_a;
    Rinn::Registry reg_b;

    // 构建两个完全一致的世界
    void SetUpIdenticalWorld(Rinn::Registry& reg) {
        Entity e0 = reg.create_entity();
        Entity e1 = reg.create_entity();
        Entity e2 = reg.create_entity();

        (void)reg.emplace<Position>(e0, Position{0.0f, 0.0f});
        (void)reg.emplace<Speed>(e0, Speed{100.0f, 0.0f});

        (void)reg.emplace<Position>(e1, Position{10.0f, 20.0f});
        (void)reg.emplace<Speed>(e1, Speed{-50.0f, 30.0f});

        (void)reg.emplace<Position>(e2, Position{5.0f, 5.0f});
        (void)reg.emplace<Speed>(e2, Speed{0.0f, -10.0f});
    }
};

TEST_F(SystemDeterminismTest, SameInput_SameOutput) {
    SetUpIdenticalWorld(reg_a);
    SetUpIdenticalWorld(reg_b);

    const float dt = 1.0f / 60.0f;

    // 两个世界跑 100 帧
    for (int i = 0; i < 100; ++i) {
        TestPhysics::Update(reg_a, dt);
        TestPhysics::Update(reg_b, dt);
    }

    // 逐实体对比位置
    for (Entity ea : reg_a.view<Position>()) {
        auto& pos_a = reg_a.get<Position>(ea);
        auto& pos_b = reg_b.get<Position>(Entity(ea.index(), ea.generation()));

        EXPECT_FLOAT_EQ(pos_a.x, pos_b.x);
        EXPECT_FLOAT_EQ(pos_a.y, pos_b.y);
    }
}

TEST_F(SystemDeterminismTest, SingleStep_KnownResult) {
    // 已知输入 → 已知输出（手算验证）
    Rinn::Registry reg;
    Entity e = reg.create_entity();
    (void)reg.emplace<Position>(e, Position{0.0f, 0.0f});
    (void)reg.emplace<Speed>(e, Speed{60.0f, 120.0f});

    TestPhysics::Update(reg, 0.5f);

    auto& pos = reg.get<Position>(e);
    EXPECT_FLOAT_EQ(pos.x, 30.0f);   // 60 × 0.5
    EXPECT_FLOAT_EQ(pos.y, 60.0f);   // 120 × 0.5
}

TEST_F(SystemDeterminismTest, MultiFrame_Accumulation) {
    Rinn::Registry reg;
    Entity e = reg.create_entity();
    (void)reg.emplace<Position>(e, Position{0.0f, 0.0f});
    (void)reg.emplace<Speed>(e, Speed{10.0f, 0.0f});

    // 10 帧 × dt=0.1 → 位移 = 10 × 0.1 × 10 = 10.0
    for (int i = 0; i < 10; ++i) {
        TestPhysics::Update(reg, 0.1f);
    }

    auto& pos = reg.get<Position>(e);
    EXPECT_NEAR(pos.x, 10.0f, 1e-5f);  // 浮点容差
}

TEST_F(SystemDeterminismTest, ZeroDeltaTime_NoChange) {
    Rinn::Registry reg;
    Entity e = reg.create_entity();
    (void)reg.emplace<Position>(e, Position{42.0f, 77.0f});
    (void)reg.emplace<Speed>(e, Speed{9999.0f, 9999.0f});

    TestPhysics::Update(reg, 0.0f);

    auto& pos = reg.get<Position>(e);
    EXPECT_FLOAT_EQ(pos.x, 42.0f);   // 不变
    EXPECT_FLOAT_EQ(pos.y, 77.0f);
}

TEST_F(SystemDeterminismTest, SystemIgnoresIncompleteEntities) {
    Rinn::Registry reg;
    Entity full    = reg.create_entity();
    Entity pos_only = reg.create_entity();
    Entity spd_only = reg.create_entity();

    // full: 有 Position + Speed → 应被更新
    (void)reg.emplace<Position>(full, Position{0.0f, 0.0f});
    (void)reg.emplace<Speed>(full, Speed{10.0f, 10.0f});

    // pos_only: 只有 Position → 不应被更新
    (void)reg.emplace<Position>(pos_only, Position{100.0f, 100.0f});

    // spd_only: 只有 Speed → 不应被更新（View 不会遍历到）
    (void)reg.emplace<Speed>(spd_only, Speed{999.0f, 999.0f});

    TestPhysics::Update(reg, 1.0f);

    EXPECT_FLOAT_EQ(reg.get<Position>(full).x, 10.0f);       // 更新了
    EXPECT_FLOAT_EQ(reg.get<Position>(pos_only).x, 100.0f);  // 未动
}

TEST_F(SystemDeterminismTest, NegativeVelocity) {
    Rinn::Registry reg;
    Entity e = reg.create_entity();
    (void)reg.emplace<Position>(e, Position{100.0f, 200.0f});
    (void)reg.emplace<Speed>(e, Speed{-25.0f, -50.0f});

    TestPhysics::Update(reg, 2.0f);

    auto& pos = reg.get<Position>(e);
    EXPECT_FLOAT_EQ(pos.x, 50.0f);    // 100 + (-25) × 2
    EXPECT_FLOAT_EQ(pos.y, 100.0f);   // 200 + (-50) × 2
}

TEST_F(SystemDeterminismTest, LargeEntityCount_Deterministic) {
    // 压力测试：1000 个实体，结果可复现
    auto setup = [](Rinn::Registry& reg) {
        for (int i = 0; i < 1000; ++i) {
            Entity e = reg.create_entity();
            (void)reg.emplace<Position>(e, Position{
                static_cast<float>(i), static_cast<float>(i * 2)
            });
            (void)reg.emplace<Speed>(e, Speed{
                static_cast<float>(i % 10), static_cast<float>((i + 5) % 10)
            });
        }
    };

    setup(reg_a);
    setup(reg_b);

    for (int frame = 0; frame < 60; ++frame) {
        TestPhysics::Update(reg_a, 1.0f / 60.0f);
        TestPhysics::Update(reg_b, 1.0f / 60.0f);
    }

    // 逐实体对比
    auto view_a = reg_a.view<Position>();
    auto view_b = reg_b.view<Position>();

    auto it_a = view_a.begin();
    auto it_b = view_b.begin();

    while (it_a != view_a.end()) {
        Entity ea = *it_a;
        Entity eb = *it_b;

        EXPECT_EQ(ea.index(), eb.index());

        auto& pa = reg_a.get<Position>(ea);
        auto& pb = reg_b.get<Position>(eb);

        EXPECT_FLOAT_EQ(pa.x, pb.x);
        EXPECT_FLOAT_EQ(pa.y, pb.y);

        ++it_a;
        ++it_b;
    }
}
