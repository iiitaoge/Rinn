# Lua-C++ 意图驱动架构

## 核心原则

| 维度 | C++ (System) | Lua (Script) |
|------|--------------|--------------|
| **数据** | 定义组件结构 | 定义配置值/Prefab |
| **时间** | 积分器（每帧维持） | 脉冲（事件触发） |
| **权力** | How（能力边界） | What（意图声明） |

---

## API 设计

### Layer 1: 意图 API (Lua → C++)

Lua 声明意图，C++ 执行具体物理操作。

```lua
-- 移动意图（C++ 内部设置 Velocity）
move(entity, "up", 200)
move(entity, "down", 200)
move(entity, "left", 200)
move(entity, "right", 200)

-- 停止意图（C++ 将 Velocity 归零）
stop(entity)

-- 生成意图（C++ 读取 Prefab 并挂载所有组件）
local player = spawn("player", 400, 300)

-- 销毁意图
destroy(entity)
```

### Layer 2: 查询 API (只读)

Lua 只能获取数据拷贝，不能修改。

```lua
local pos = query_position(entity)  -- 返回 {x, y} 拷贝
local vel = query_velocity(entity)  -- 返回 {vx, vy} 拷贝
local alive = is_alive(entity)      -- 返回 bool
```

### Layer 3: 回调 API (C++ → Lua)

C++ 在事件发生时通知 Lua。

```lua
function on_collision(a, b)
    play_sound("hit")
end

function on_perceived(npc, signal)
    if signal.type == "threat" then
        flee_from(npc, signal.source)
    end
end
```

---

## 删除的 API

以下 API 不再暴露给 Lua：

```lua
-- ❌ 直接操作组件（不安全，导致穿墙/作弊）
set_Velocity(entity, {vx, vy})
set_Transform(entity, {x, y})
emplace_*(entity, {...})
get_*(entity)
remove_*(entity)
```

---

## 性能对比

| 场景 | 旧架构（细粒度） | 新架构（意图） | 提升 |
|------|------------------|----------------|------|
| 单次移动 | 510 ns | 231 ns | **2.2x** |
| table 创建 | 1 次/调用 | 0 次 | ∞ |
| GC 压力 | 高 | **零** | ∞ |

---

## 典型数据流

```
[按下 W]
    ↓ (Lua 轮询)
input.is_key_down(KEY.W) → true
    ↓
Lua: move(player, "up", 200)
    ↓ (边界穿越，无 table 创建)
C++ 设置 Velocity.vy = -200
    ↓ (每帧积分)
C++ PhysicsSystem: Transform.y += Velocity.vy * dt
    ↓
[松开 W]
    ↓ (Lua 轮询)
if not moving then stop(player) end
    ↓
C++ 设置 Velocity = {0, 0}
```

---

## 回调 vs 轮询

| 事件类型 | 方式 | 原因 |
|----------|------|------|
| **输入 (WASD)** | 轮询 | 需要持续按住状态 |
| **碰撞** | 回调 | 不是每帧都发生 |
| **感知** | 回调 | 推模式，信号驱动 |
| **死亡/生成** | 回调 | 稀有事件 |

---

## 哲学总结

1. **Lua 只知道 "能做什么"**：move, stop, spawn
2. **Lua 不知道 "怎么实现"**：不知道 Velocity 组件存在
3. **C++ 可以自由修改内部实现**：换成加速度、力，Lua 无感知
