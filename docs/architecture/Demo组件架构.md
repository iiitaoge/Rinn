# Demo 组件架构（最终版）

## 核心哲学

### 意识流水线
```
信号(瞬时) → Sensors(过滤) → Memory(存储) → Needs(评估) → Action(执行)
```

### 三条原则
1. **信号是子弹**：瞬时数据，不是组件
2. **记忆是弹孔**：信号冻结后的持久痕迹
3. **关系是残渣**：记忆经 Needs 过滤后的评估缓存

---

## 组件清单（纯 POD）

### Tier 0: 物理层

| 组件 | 字节 | 字段 | 修改者 |
|------|------|------|--------|
| `Transform` | 12 | x, y, layer | PhysicsSystem |
| `Velocity` | 8 | vx, vy | InputSystem/AISystem |
| `Collider` | 8 | type, w, h | - |

### Tier 1: 渲染层

| 组件 | 字节 | 字段 | 修改者 |
|------|------|------|--------|
| `Sprite` | 8 | tex_id, w, h | - |
| `Animator` | 8 | anim_id, frame | AnimationSystem |

### Tier 2: 感知层（有意识实体专用）

| 组件 | 字节 | 字段 | 说明 |
|------|------|------|------|
| `Sensors` | 12 | vision_range, fov, hearing_threshold | 接收器参数 |

### Tier 3: 认知层

```cpp
struct Memory {
    // 情景记忆：最近发生的事（环形缓冲）
    WorldEvent events[16];
    uint8_t head, count;
    
    // 语义记忆：对他人的评估（社会关系）
    struct Opinion { uint32_t target; int8_t score; };
    Opinion opinions[8];  // 只存最重要的 8 个人
    uint8_t opinion_count;
};
// 完整关系表在 RelationSystem 全局存储
```

### Tier 4: 动机层

```cpp
struct Needs {
    uint8_t hunger;   // 0=饱, 255=饿死
    uint8_t energy;   // 0=累, 255=精神
    uint8_t safety;   // 0=恐惧, 255=安全
    uint8_t padding;
};
// 总计 4 字节，16 个 NPC 装入一个 Cache Line
```

### Tier 5: 资源层

```cpp
struct Inventory {
    uint16_t items[8];   // ItemID
    uint8_t amounts[8];
};
// 总计 24 字节
```

---

## 全局数据结构（System 管理）

| 结构 | 存储位置 | 用途 |
|------|----------|------|
| `WorldEvent[]` | EventLog（环形缓冲） | 最近 5 秒事实 |
| `RelationEdge[]` | RelationSystem | 完整社会关系 |
| `SpatialGrid` | SpatialSystem | 空间查询加速 |
| `StringPool` | ResourceManager | 名字字符串池 |

---

## 信号传播机制

### Signal（瞬时数据，非组件）

```cpp
struct Signal {
    enum Type { VISUAL, AUDIO, RADIO };
    Vector2 pos;
    float intensity;
    Entity source;
    WorldEvent payload;
};
```

### 传播流程

```
1. ActionSystem 产生 Signal
   ↓
2. SensorySystem 空间查询（SpatialGrid）
   ↓  
3. 遍历候选实体，读取 Sensors 组件
   ↓
4. 物理检查（距离/角度/遮挡）
   ↓
5. 通过 → 写入 Memory 组件
```

---

## 涌现机制

### 社会关系如何形成

```
B 给 A 苹果（Signal）
    ↓
A 的 Memory 写入：{ Event: GOT_FOOD, from: B }
    ↓
A 的 Needs.hunger 很高（快饿死）
    ↓
Lua 评估：苹果价值极高 → opinions[B].score += 50
    ↓
关系形成：A 认为 B 是朋友
```

### 关系特性

- **主观性**：A→B 和 B→A 独立存储
- **动态性**：随交互实时更新
- **可遗忘性**：Memory 容量限制，久不交互则淡忘

---

## Native API（暴露给 Lua）

| API | 内部操作 | 说明 |
|-----|----------|------|
| `move(e, dir, speed)` | 设置 Velocity | 移动意图 |
| `stop(e)` | Velocity = 0 | 停止 |
| `get_needs(e)` | 返回 Needs 拷贝 | 只读 |
| `get_position(e)` | 返回 Transform 拷贝 | 只读 |
| `get_opinions(e)` | 返回 opinions[] 拷贝 | 只读 |
| `modify_opinion(a, b, delta)` | 修改 Memory.opinions | 社交 |
| `spawn(prefab, x, y)` | 创建实体 | 实体 |
| `destroy(e)` | 销毁实体 | 实体 |

---

## 内存布局

| 组件 | 字节 | 说明 |
|------|------|------|
| Transform | 12 | |
| Velocity | 8 | |
| Sensors | 12 | |
| Memory | ~100 | 16 events + 8 opinions |
| Needs | 4 | |
| Inventory | 24 | |
| **总计** | **~160** | 每个有意识实体 |

L1 Cache (32KB) 可装入 **200+ 个 NPC** 核心数据。
