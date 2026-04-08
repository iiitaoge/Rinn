# PhysicSystem.hpp — 物理系统（深度注释版）

> 文件路径: `src/Systems/PhysicSystem.hpp`  
> 角色: 最简物理积分器 — 每帧将速度乘以时间步长累加到位置。

---

## 文件级设计意图

**当前实现**: 显式欧拉积分 (Explicit Euler Integration) — 物理模拟中最简单的方法。

**可选积分方法**:

| 方法 | 公式 | 精度 | 稳定性 | 适用场景 |
|------|------|------|--------|---------|
| **欧拉（当前）** | `x += v * dt` | O(dt) | 低 | 简单 2D 移动 |
| Semi-implicit Euler | `v += a * dt; x += v * dt` | O(dt) | 中 | 弹簧、碰撞 |
| Velocity Verlet | `x += v*dt + 0.5*a*dt²; v += 0.5*(a+a')*dt` | O(dt²) | 高 | 天体物理 |
| RK4 (Runge-Kutta 4) | 4 次求值，加权平均 | O(dt⁴) | 极高 | 精密模拟 |

**为什么选欧拉？** 当前没有加速度、弹簧、重力等复杂物理。只有"手柄输入 → 设置速度 → 积分到位置"。欧拉已完全够用，用更高阶的方法是过度工程。

**欧拉的缺陷**: 当 dt 波动（掉帧时 dt 突然变大）时，实体可能"穿墙"——一帧的位移超过碰撞盒宽度，碰撞检测完全错过。解决方案: 固定时间步 (`dt = 1/60` 固定，与帧率解耦)。

---

## 逐行注释

```cpp
inline void update(Registry& reg, float dt) {
    float maxX = (float)GetScreenWidth();
    float maxY = (float)GetScreenHeight();
```

`maxX/maxY` 在当前代码中**未使用**（边界约束移至 Lua 侧）。保留是为了将来可能的 C++ 侧边界检查。

> **语法知识 — `(float)` C 风格强制转换**:
>
> `GetScreenWidth()` 返回 `int`，赋给 `float` 需要转换。C 风格 `(float)x` 等价于 `static_cast<float>(x)`，但不推荐——它不区分安全和不安全的转换（见 Types.md 中的四种类型转换说明）。

---

```cpp
    for (Entity e : reg.view<Transform, Velocity>()) {
        auto& t = reg.get<Transform>(e);
        auto& v = reg.get<Velocity>(e);
        t.x += v.vx * dt;
        t.y += v.vy * dt;
    }
}
```

**欧拉积分 `x += v * dt`**:

数学推导:
```
连续形式: dx/dt = v (速度是位置对时间的导数)
离散近似: Δx ≈ v × Δt (将微小变化替换为有限差分)
代码实现: x += v * dt
```

**`dt` 的作用 — 帧率无关性**:
```
60fps: dt ≈ 0.0167s → 每帧位移 = 200 × 0.0167 = 3.33 像素
30fps: dt ≈ 0.0333s → 每帧位移 = 200 × 0.0333 = 6.67 像素 (×2 补偿)
1 秒累计: 60 × 3.33 = 30 × 6.67 ≈ 200 像素 ✓ 帧率无关
```

**View 过滤**: 只有同时拥有 `Transform` 和 `Velocity` 的实体被遍历。静态物体（只有 Transform）或无位置的抽象实体不受影响。

---

## 文件级总结

| 设计决策 | 选择 | 理由 |
|---------|------|------|
| 积分方法 | 欧拉 | 无加速度/重力，足够 |
| 时间步 | 可变 dt | 简单，但帧率波动时有穿墙风险 |
| 边界约束 | 移至 Lua | "C++ 做机制，Lua 做策略" |
