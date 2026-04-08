# Boids.lua — Boids 群体行为算法（深度注释版）

> 文件路径: `scripts/Boids.lua`  
> 角色: 实现 Craig Reynolds (1986) 的 Boids 三规则——分离、对齐、聚合——用于模拟鸟群/鱼群的涌现行为。

---

## 算法背景

**Boids** = "Bird-oid Object"。Reynolds 发现只需三条简单局部规则，就能在全局涌现出复杂的群体行为——无需中央协调器。

| 规则 | 输入 | 输出 | 物理类比 |
|------|------|------|---------|
| **Separation (分离)** | 邻居位置 | 远离太近的邻居的力 | 斥力（短程） |
| **Alignment (对齐)** | 邻居速度 | 朝平均方向飞的力 | 摩擦/黏性 |
| **Cohesion (聚合)** | 邻居位置 | 朝群体中心靠拢的力 | 引力（长程） |

**涌现行为 (Emergence)**: 个体只看到局部邻居，但整体呈现出 V 字编队、分裂重组、绕障碍物流动等复杂行为。没有"领队"概念。

---

## 逐行注释

### get_neighbors — 邻居搜索

```lua
function get_neighbors(me, boids, radius)
    local result = {}
    local mx, my = get_pos(me)
```

> **`get_pos(me)`**: 调用 C++ 绑定函数，返回 `std::pair<float,float>`，在 Lua 中展开为两个返回值。

```lua
    for _, other in ipairs(boids) do
        if other ~= me then
```

> **语法知识 — `ipairs` vs `pairs`**:
>
> | 函数 | 遍历范围 | 顺序 | 性能 |
> |------|---------|------|------|
> | `ipairs(t)` | 连续整数键 1,2,3... | 有序 | O(n) |
> | `pairs(t)` | 所有键（含字符串键） | 无序 | O(n) |
>
> `ipairs` 遇到 `nil` 值就停止。`pairs` 遍历所有键值对。
>
> **`_` 惯例**: 下划线变量名表示"不关心这个值"。`ipairs` 返回 `(index, value)`，我们只需要 value（`other`），故索引用 `_`。

```lua
            local ox, oy = get_pos(other)
            local dist = math.sqrt((mx - ox)^2 + (my - oy)^2)
```

> **语法知识 — `^` 幂运算**:
>
> Lua 的 `^` 是幂运算符（C++ 中 `^` 是 XOR 位运算，完全不同）。
> ```lua
> 2^3 = 8      -- Lua: 2的3次方
> ```
> ```cpp
> 2^3 = 1      // C++: 2 XOR 3 = 0b10 XOR 0b11 = 0b01 = 1
> ```

**欧几里得距离**: `√((x₁-x₂)² + (y₁-y₂)²)` — 两点间的直线距离。

**性能注意**: `math.sqrt` 每次调用约 10ns。对 N 个 boid 查找邻居 = O(N²) 次距离计算。100 个 boid → 10000 次 sqrt → ~100μs。可优化为距离平方比较（避免 sqrt）。

```lua
            if dist < radius then
                result[#result + 1] = other
            end
```

> **语法知识 — `#` 长度运算符**:
>
> `#result` 返回 table 的"序列长度"（连续整数键 1,2,...,n 的最大 n）。
>
> **`result[#result + 1] = other`**: 在数组末尾追加元素。等价于 `table.insert(result, other)` 但直接下标访问更快（省去函数调用开销）。

---

### separation — 分离力

```lua
function separation(me, neighbors)
    local fx, fy = 0, 0
    local mx, my = get_pos(me)
    for _, other in ipairs(neighbors) do
        local ox, oy = get_pos(other)
        local dx, dy = mx - ox, my - oy
        local dist = math.sqrt(dx*dx + dy*dy)
        if dist > 0 then
            fx = fx + dx / dist / dist
            fy = fy + dy / dist / dist
        end
    end
    return fx, fy
end
```

**力的计算推导**:
```
方向向量:  (dx, dy) = 自己 - 邻居  (指向远离邻居的方向)
归一化:    (dx/dist, dy/dist)      (长度=1的方向)
距离衰减:  (dx/dist²)              (越近力越大, 反平方律)

合力:      对所有邻居的力向量求和
```

**反平方律 (1/r²)**: 与物理世界的库仑力、万有引力一样。距离减半 → 力增加 4 倍。这保证 boid 不会轻易重叠。

**`if dist > 0`**: 防止两个 boid 完全重叠时除以零。实际中不太可能发生，但浮点精度可能导致极小距离。

---

### alignment — 对齐力

```lua
function alignment(me, neighbors)
    local fx, fy = 0, 0
    local n = #neighbors
    if n == 0 then return 0, 0 end
    for _, other in ipairs(neighbors) do
        local vx, vy = get_vel(other)
        fx = fx + vx
        fy = fy + vy
    end
    return fx / n, fy / n
end
```

**算法**: 邻居速度的算术平均。结果 = "群体正在朝哪个方向飞"。

**数学**: 设邻居速度为 v₁, v₂, ..., vₙ，对齐力 = (Σvᵢ) / n。

**效果**: 个体逐渐调整方向与邻居一致 → 群体形成统一的飞行方向。

---

### cohesion — 聚合力

```lua
function cohesion(me, neighbors)
    local fx, fy = 0, 0
    local n = #neighbors
    if n == 0 then return 0, 0 end
    for _, other in ipairs(neighbors) do
        local ox, oy = get_pos(other)
        fx = fx + ox
        fy = fy + oy
    end
    local mx, my = get_pos(me)
    return fx / n - mx, fy / n - my
end
```

**算法**: 
1. 计算邻居位置的质心：`center = (Σpᵢ) / n`
2. 返回从自己指向质心的向量：`center - self`

**效果**: 个体向群体中心靠拢 → 群体保持紧凑而不散开。

**注意**: 返回值未归一化。距群心越远 → 力越大 → 越快回归。这是有意的——过远的个体需要更强的"回拉力"。

---

## 三规则合成

实际使用时，三种力加权合成后决定 boid 的最终速度:
```lua
local sx, sy = separation(boid, nbrs)
local ax, ay = alignment(boid, nbrs)
local cx, cy = cohesion(boid, nbrs)

local w_sep, w_ali, w_coh = 1.5, 1.0, 0.8
move(boid, sx*w_sep + ax*w_ali + cx*w_coh,
           sy*w_sep + ay*w_ali + cy*w_coh)
```

**权重调节效果**:
| 权重偏向 | 群体行为 |
|---------|---------|
| 高分离 | 松散，个体保持距离 |
| 高对齐 | 整齐编队，像军队 |
| 高聚合 | 紧密球形群体 |

---

## 文件级总结

| 设计决策 | 选择 | 理由 |
|---------|------|------|
| 邻居搜索 | 暴力 O(N²) | N 小，不需要空间索引 |
| 距离计算 | sqrt | 简洁，可优化为距离平方 |
| 分离力 | 反平方衰减 | 物理直觉，近距离强排斥 |
| 聚合力 | 未归一化 | 远处个体受更强回拉力 |
| 权重 | 未在此文件定义 | 设计灵活，由调用方决定 |
