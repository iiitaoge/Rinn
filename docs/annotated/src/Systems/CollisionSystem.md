# CollisionSystem.hpp — 碰撞系统（深度注释版）

> 文件路径: `src/Systems/CollisionSystem.hpp`  
> 角色: AABB 碰撞检测和最小穿透轴分离响应。

---

## 文件级设计意图

**当前方案**: 暴力 O(n²) AABB 碰撞检测 + 位置修正响应。

**碰撞检测算法对比**:

| 算法 | 复杂度 | 适用场景 | 实现难度 |
|------|--------|---------|---------|
| **暴力 N²（当前）** | **O(n²)** | **n < 100** | **极低** |
| 空间哈希/网格 | O(n × k) | 均匀分布 | 低 |
| 四叉树 | O(n log n) | 不均匀分布 | 中 |
| SAT (Separating Axis) | O(n) per pair | 凸多边形 | 中 |
| GJK | O(n) per pair | 任意凸体 | 高 |

**为什么选暴力？** 当前实体数 < 10。N² = 100 次比较。空间分区的数据结构维护成本 >> 100 次简单比较。直到实体数过百，暴力都更快。

**AABB vs 其他碰撞形状**:

| 形状 | 检测复杂度 | 贴合度 | 旋转支持 |
|------|-----------|--------|---------|
| **AABB（当前）** | **4 次比较** | **中** | **✗** |
| 圆形 | 距离平方+比较 | 中 | ✓ |
| OBB | SAT (8 次比较) | 高 | ✓ |
| 凸多边形 | SAT (n 次) | 最高 | ✓ |

AABB 最快但不支持旋转——旋转后包围盒失效。当前 Transform 没有旋转字段，AABB 完全够用。

---

## 逐行注释

### overlaps — AABB 重叠检测

```cpp
inline bool overlaps(const Transform& ta, const Collider& ca,
                     const Transform& tb, const Collider& cb) {
    float ax = ta.x + ca.offset_x, ay = ta.y + ca.offset_y;
    float bx = tb.x + cb.offset_x, by = tb.y + cb.offset_y;
    return ax < bx + cb.width  && ax + ca.width  > bx
        && ay < by + cb.height && ay + ca.height > by;
}
```

**AABB 碰撞原理（分离轴定理的简化版）**:

两个矩形**不碰撞**的条件（只要满足任一条即分离）:
```
A 在 B 的右边: A.left > B.right    →  ax > bx + cb.width
A 在 B 的左边: A.right < B.left    →  ax + ca.width < bx
A 在 B 的下面: A.top > B.bottom    →  ay > by + cb.height
A 在 B 的上面: A.bottom < B.top    →  ay + ca.height < by
```

碰撞 = 以上条件**全不满足** = 取反 = 两个轴都重叠:
```
X 轴重叠: A.left < B.right  AND  A.right > B.left
Y 轴重叠: A.top < B.bottom  AND  A.bottom > B.top
```

**`ca.offset_x/y` 的作用**: 碰撞盒可以相对于实体位置偏移。例如角色精灵 128×128，但碰撞盒只覆盖下半身 128×64，偏移 y=64。

---

### detect — 暴力检测

```cpp
inline std::vector<Hit> detect(Registry& reg) {
    std::vector<Hit> hits;
    std::vector<Entity> entities;
    for (Entity e : reg.view<Transform, Collider>())
        entities.push_back(e);

    for (size_t i = 0; i < entities.size(); ++i) {
        for (size_t j = i + 1; j < entities.size(); ++j) {
```

**`j = i + 1`**: 避免重复检测。`(A,B)` 和 `(B,A)` 只检查一次。总检测次数 = `n*(n-1)/2`。

**为什么先收集到 vector 再双重循环？** View 的迭代器不支持随机访问（不能 `view[i]`），需要先线性收集。

**设计缺陷**: 
1. 没有利用 `is_static` 标志跳过静态-静态对
2. 没有利用 `is_trigger` 标志区别触发器和实体碰撞
3. 每帧重新收集实体列表（可以缓存直到实体增删）

---

### resolve — 最小穿透轴分离

```cpp
inline void resolve(Registry& reg, const std::vector<Hit>& hits) {
    for (auto& [a, b] : hits) {
```

> **语法知识 — 结构化绑定 `auto& [a, b]`** (C++17):
>
> 将聚合类型的成员直接绑定到变量:
> ```cpp
> Hit h{entity1, entity2};
> auto& [a, b] = h;  // a = h.a, b = h.b (引用，非拷贝)
> ```
> 等价于:
> ```cpp
> Entity& a = h.a;
> Entity& b = h.b;
> ```

---

```cpp
        float ox = std::min(ta.x + ca.width - tb.x, tb.x + cb.width - ta.x);
        float oy = std::min(ta.y + ca.height - tb.y, tb.y + cb.height - ta.y);
```

**穿透深度计算**:
```
            ┌──────────┐ B
            │    ┌─────┼──┐ A
            │    │ooooo│  │ ← ox = 重叠区域的宽度
            └────┼─────┘  │
                 └────────┘

ox = min(A.right - B.left, B.right - A.left)
   = 取两个方向重叠的较小者 = 最小推开距离
```

---

```cpp
        if (ox < oy) {
            float sign = (ta.x < tb.x) ? -1.0f : 1.0f;
            ta.x += sign * ox * 0.5f;
            tb.x -= sign * ox * 0.5f;
        }
        else {
            float sign = (ta.y < tb.y) ? -1.0f : 1.0f;
            ta.y += sign * oy * 0.5f;
            tb.y -= sign * oy * 0.5f;
        }
```

**最小穿透轴分离 (Minimum Translation Vector)**:

1. 选择穿透更浅的轴 (`min(ox, oy)`)——沿该轴推开移动量最小
2. `sign` 决定推开方向: A 在 B 左边则 A 往左退、B 往右退
3. 各退一半 (`× 0.5f`) 保持对称性

> **语法知识 — 三元运算符 `? :`**:
>
> ```cpp
> float sign = (ta.x < tb.x) ? -1.0f : 1.0f;
> ```
> 等价于:
> ```cpp
> float sign;
> if (ta.x < tb.x) sign = -1.0f;
> else sign = 1.0f;
> ```
> 但更紧凑，且三元表达式是**表达式**（有值），可以用在赋值、函数参数等位置。

**缺陷**:
1. 没有考虑 `is_static` 标志：静态物体不应被推开，只能移动非静态方
2. 多物体同时碰撞时，依次解决可能导致抖动（A 被推向 B，B 被推向 C，C 推回 A）
3. 没有碰撞回调（无法触发"碰到敌人掉血"等逻辑）

---

## 文件级总结

| 设计决策 | 选择 | 理由 |
|---------|------|------|
| 碰撞形状 | AABB | 无旋转，4 次比较最快 |
| 检测算法 | 暴力 N² | 实体少，比空间分区更快 |
| 碰撞响应 | 位置修正（各退一半） | 简单直觉 |
| 碰撞回调 | 无 | 缺陷，需要事件系统支持 |
