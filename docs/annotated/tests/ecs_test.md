# ecs_test.cpp — ECS 核心单元测试（深度注释版）

> 文件路径: `tests/ecs_test.cpp`  
> 角色: 使用 Google Test 框架对 ECS 核心功能进行系统性验证：SparseSet 操作、Entity 生命周期、View 遍历正确性、System 确定性。

---

## 文件级设计意图

**为什么要写测试？** ECS 核心一旦有 Bug，所有游戏行为都会崩溃。但 ECS 的 Bug 特别难调试——实体句柄失效、组件被错误覆盖、View 遗漏实体——这些在游戏运行中表现为"偶尔闪一下"或"某个敌人消失了"。自动化测试能在修改代码后**立即**发现这些问题。

**测试设计哲学**: 每个测试只验证一个行为，命名清晰到不需要注释就能理解目的。

---

## Google Test 框架

> **语法知识 — Test Fixture (`TEST_F`)**:
>
> ```cpp
> class SparseSetTest : public ::testing::Test {
> protected:
>     Rinn::SparseSet<Position> pool;  // 每个测试独有一份
> };
>
> TEST_F(SparseSetTest, TestName) {
>     // pool 在这里已经被默认构造好
> }
> ```
>
> **`::testing::Test`**: Google Test 的基类。`TEST_F` 宏展开后是这个基类的子类。
>
> **关键**: 每个 `TEST_F` 创建一个**全新的 fixture 实例**。测试之间完全隔离——前一个测试对 `pool` 的修改不会影响下一个测试。这避免了测试间的隐式状态依赖。
>
> **`protected`**: `TEST_F` 宏展开的类继承自 fixture，`protected` 让测试代码可以访问 `pool`。

> **断言宏**:
>
> | 宏 | 含义 | 失败行为 |
> |-----|------|---------|
> | `EXPECT_EQ(a, b)` | 期望相等 | 记录失败，继续执行 |
> | `ASSERT_EQ(a, b)` | 断言相等 | 记录失败，**中止当前测试** |
> | `EXPECT_TRUE(expr)` | 期望为真 | 继续 |
> | `EXPECT_FLOAT_EQ(a, b)` | 浮点近似相等（4 ULP） | 继续 |
> | `EXPECT_NEAR(a, b, ε)` | 绝对误差 < ε | 继续 |
>
> **`EXPECT` vs `ASSERT`**: `EXPECT` 失败后继续运行其他检查，报告所有失败项。`ASSERT` 失败后立即终止，用于后续检查依赖前置条件的场景。

---

## (1) SparseSet 测试组

### 基本 emplace + get

```cpp
TEST_F(SparseSetTest, Emplace_BasicAddAndGet) {
    Entity e(0, 0);
    auto& pos = pool.emplace(e, Position{1.0f, 2.0f});
    EXPECT_FLOAT_EQ(pos.x, 1.0f);
    EXPECT_FLOAT_EQ(pos.y, 2.0f);
    EXPECT_TRUE(pool.has(e));
    EXPECT_EQ(pool.size(), 1u);  // u = unsigned 后缀
}
```

**验证**: emplace 后 has() 返回 true，size 增加，返回的引用数据正确。

### 幂等语义

```cpp
TEST_F(SparseSetTest, Emplace_DuplicateReturnsExisting_NotReplace) {
    Entity e(0, 0);
    pool.emplace(e, Position{1.0f, 2.0f});
    auto& pos = pool.emplace(e, Position{99.0f, 99.0f});
    EXPECT_FLOAT_EQ(pos.x, 1.0f);  // 原值未变！
}
```

**核心语义测试**: 重复 emplace 不覆盖已有数据。这验证了 SparseSet.md 中描述的幂等设计。

### Swap-and-Pop 正确性

```cpp
TEST_F(SparseSetTest, Remove_MiddleElement_SwapAndPop) {
    Entity e0(0,0), e1(1,0), e2(2,0);
    pool.emplace(e0, {0,0});
    pool.emplace(e1, {1,1});
    pool.emplace(e2, {2,2});
    pool.remove(e1);  // 删中间

    EXPECT_FALSE(pool.has(e1));      // e1 已删
    EXPECT_TRUE(pool.has(e0));       // e0 不受影响
    EXPECT_TRUE(pool.has(e2));       // e2 被 swap 但仍可查找
    EXPECT_FLOAT_EQ(pool.get(e2).x, 2.0f);  // 数据完整
}
```

**这是最重要的测试之一**。swap-and-pop 的正确性依赖于 Sparse、Dense、dense_to_entity 三个数组的精确同步更新。任何一步顺序错误都会导致数据损坏。

---

## (2) Entity 生命周期测试组

### 首个实体

```cpp
TEST_F(EntityLifecycleTest, Create_FirstEntityIsIndex0Gen0) {
    Entity e = registry.create_entity();
    EXPECT_EQ(e.index(), 0);
    EXPECT_EQ(e.generation(), 0);
}
```

### 索引复用 + 版本递增

```cpp
TEST_F(EntityLifecycleTest, Reuse_SameIndex_BumpedGeneration) {
    Entity e0 = registry.create_entity();
    registry.destroy_entity(e0);
    Entity e1 = registry.create_entity();
    EXPECT_EQ(e1.index(), e0.index());  // 同一索引
    EXPECT_EQ(e1.generation(), 1);       // 版本+1
}
```

验证环形缓冲区的 FIFO 复用和版本号递增。

### 僵尸句柄检测 — 最关键的安全测试

```cpp
TEST_F(EntityLifecycleTest, Reuse_OldHandleIsStale) {
    Entity original = registry.create_entity();
    registry.destroy_entity(original);
    Entity reused = registry.create_entity();
    EXPECT_FALSE(registry.is_alive(original));  // 旧 Handle 失效
    EXPECT_TRUE(registry.is_alive(reused));     // 新 Handle 有效
}
```

**为什么必须测试？** 如果不检测僵尸句柄，游戏代码可能通过旧 Handle 访问到新实体的数据——一个角色死亡后，另一个角色占用了它的索引，结果旧引用操控了新角色。这种 Bug 在游戏中极隐蔽。

---

## (3) View 测试组

### 多组件交集

```cpp
TEST_F(ViewTest, MultiComponent_Intersection) {
    // e0: Position + Speed  ← 满足条件
    // e1: 只有 Position     ← 不满足
    // e2: 只有 Speed        ← 不满足
    int count = 0;
    for (Entity e : registry.view<Position, Speed>()) { count++; }
    EXPECT_EQ(count, 1);
}
```

验证 View 返回的是**交集**（同时拥有两个组件的实体）而非并集。

### 最小池优化验证

```cpp
TEST_F(ViewTest, SmallestPool_Optimization) {
    // Position 池: 100 个, Speed 池: 5 个
    int count = 0;
    for ([[maybe_unused]] Entity e : registry.view<Position, Speed>()) { count++; }
    EXPECT_EQ(count, 5);
}
```

> **语法知识 — `[[maybe_unused]]` (C++17)**:
>
> 告诉编译器"这个变量可能未使用，别警告"。在 `/W4` 级别下，未使用的循环变量会产生警告。`[[maybe_unused]]` 抑制它。

---

## (4) System 确定性测试

### 相同输入 → 相同输出

```cpp
TEST_F(SystemDeterminismTest, SameInput_SameOutput) {
    SetUpIdenticalWorld(reg_a);
    SetUpIdenticalWorld(reg_b);
    for (int i = 0; i < 100; ++i) {
        TestPhysics::Update(reg_a, dt);
        TestPhysics::Update(reg_b, dt);
    }
    // 逐实体位置比较 → 必须 bit-exact 相同
}
```

**为什么测试确定性？** 如果 ECS 的遍历顺序不确定（如哈希表迭代），相同输入可能产生不同输出 → 网络同步失败、回放功能损坏。SparseSet 的 Dense 数组是确定性的（取决于插入和删除顺序）→ 应该确保这一点。

### 零 dt 不变性

```cpp
TEST_F(SystemDeterminismTest, ZeroDeltaTime_NoChange) {
    // dt = 0 → 位置不应改变
}
```

边界条件: `v * 0 = 0` 无论 v 多大。如果物理系统有除以 dt 的操作（某些积分器），dt=0 会导致除以零。欧拉积分没这个问题。

---

## 文件级总结

| 测试类别 | 数量 | 覆盖要点 |
|---------|------|---------|
| SparseSet | ~8 | emplace 幂等、swap-and-pop、clear、size、has |
| Entity 生命周期 | ~5 | 创建、销毁、复用、僵尸检测、批量创建 |
| View | ~4 | 交集过滤、最小池优化、空 View、数据访问 |
| System 确定性 | ~3 | 相同输入相同输出、零 dt、边界条件 |
