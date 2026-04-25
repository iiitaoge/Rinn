# main.lua — 游戏主脚本（深度注释版 v2）

> 文件路径: `scripts/main.lua`  
> 角色: 引擎的**策略层入口**。负责加载地图和资源、创建实体、驱动叙事状态机、处理玩家输入。自上次注释以来已从 40 行扩展到 247 行，新增剧情系统、Tiled 地图集成、HD-2D 模式切换。

---

## 文件级设计意图

**架构位置**: 本文件是"C++ 做机制，Lua 做策略"的策略层核心。C++ 引擎提供渲染、物理、碰撞能力；本文件决定**创建什么实体、如何响应输入、剧情如何推进**。

```
C++ 引擎层                        Lua 策略层 (本文件)
├─ Registry/SparseSet             ├─ 加载 Tiled 地图 → 创建瓦片实体
├─ PhysicSystem                   ├─ 创建玩家/NPC/触发器实体
├─ CollisionSystem                ├─ WASD 输入 → move()
├─ RenderSystem                   ├─ 距离检测 → 对话触发
├─ AudioSystem                    ├─ 状态机驱动分支剧情
└─ TextBubble/HD-2D               └─ 动态修改组件 (开门、移除碰撞)
```

**核心演进**: 从早期的"两个硬编码实体 + WASD 移动"进化为**数据驱动的完整游戏循环**——地图、NPC 位置、对话内容全部来自外部数据文件（Tiled 导出的 `.lua` 和 `dialogue_data.lua`），脚本只做胶水逻辑。

---

## 依赖关系

```lua
dofile("../../../scripts/map_loader.lua")
local dialogue_data = dofile("../../../scripts/dialogue_data.lua")
```

| 依赖 | 角色 |
|------|------|
| `map_loader.lua` | 解析 Tiled 导出的地图数据，创建瓦片实体，提取触发器坐标 |
| `dialogue_data.lua` | 返回一个嵌套 table，存储所有 NPC 的分支对话文本 |
| C++ 绑定函数 | `create_entity`, `set`, `remove`, `load_texture`, `play_bgm`, `move`, `get_pos`, `is_key_down`, `set_hd2d_mode`, `set_camera_target` |

> **语法知识 — `dofile(path)`**:
>
> Lua 内置函数。**立即执行**指定文件中的 Lua 代码，并返回该文件最后一个表达式的值。
>
> **与 `require` 的区别**:
> | 函数 | 缓存 | 搜索路径 | 多次调用 |
> |------|------|---------|---------|
> | `dofile(path)` | ✗ 每次重新执行 | 精确路径 | 每次都执行 |
> | `require(module)` | ✓ 只执行一次 | `package.path` 搜索 | 返回缓存结果 |
>
> **为什么用 `dofile` 而非 `require`？**
> 1. `require` 需要 `package.path` 配置正确，在嵌入式场景中路径管理复杂
> 2. `dialogue_data.lua` 返回一个 table，用 `dofile` 的返回值直接接收最自然
> 3. `map_loader.lua` 定义全局函数 `load_tiled_map`，`dofile` 后可直接调用
>
> **缺陷**: `dofile` 不缓存。如果多次调用同一文件会重复执行。对只加载一次的脚本无所谓。

---

## 第一部分：初始化阶段（加载期代码）

### 资源路径与地图加载

```lua
local ASSET_DIR = "../../../assets/texture/"

dofile("../../../scripts/map_loader.lua")
local _, map_triggers = load_tiled_map("../../../assets/texture/simple_map.lua")
```

**`local _, map_triggers`**: `load_tiled_map` 返回两个值 `(success_bool, triggers_table)`。`_` 丢弃第一个返回值（成功标志），只保留触发器数据。

> **语法知识 — Lua 多返回值**:
>
> Lua 函数可以返回任意多个值:
> ```lua
> function foo() return 1, 2, 3 end
> local a, b, c = foo()  -- a=1, b=2, c=3
> local x = foo()         -- x=1 (多余的丢弃)
> local _, y = foo()      -- y=2 (_ 惯用名，表示不需要)
> ```

---

### 背景音乐

```lua
play_bgm("../../../assets/audio/bgm.ogg")
```

调用 C++ 绑定的 `play_bgm` 函数。这是新增的 AudioSystem 功能——C++ 侧加载 OGG 音频流并在后台持续播放。只需调用一次。

---

### 全局状态结构

```lua
local player = nil
local npcs = {}
local progress = {}

local global_state = {
    has_berry = false,
    has_hammer = false,
    has_pass = false,
    guard_passed = false,
    chest_opened = false
}
```

**设计意图 — 微状态机**:

`global_state` 是一个轻量级的**全局叙事状态数据库**。每个布尔字段代表一个剧情里程碑:

```
游戏流程:
  Bush(采集浆果) → has_berry = true
    → statue(用浆果换锤子) → has_hammer = true
      → Blacksmith(用锤子换通行证) → has_pass = true
        → Guard(出示通行证) → guard_passed = true
```

| 变量 | 意义 |
|------|------|
| `player` | 玩家实体句柄，nil 表示尚未创建 |
| `npcs` | 所有可交互对象的列表（NPC、触发器等） |
| `progress` | 嵌套 table，记录每个 NPC 每个分支的对话进度（说到第几句） |
| `global_state` | 叙事里程碑。驱动对话分支选择 |

> **设计选择**: 用简单的布尔 table 而非正式的有限状态机（FSM）。
>
> | 方案 | 复杂度 | 适用场景 |
> |------|--------|---------|
> | **布尔 table（当前）** | 极低 | 5-10 个状态、线性任务链 |
> | 有限状态机 | 中 | 多状态+复杂转换 |
> | 行为树 | 高 | AI 决策 |
> | 任务/日志系统 | 高 | 开放世界多任务 |
>
> **优势**: 极简、零依赖、任何人一看就懂。  
> **缺陷**: 不可扩展。10 个以上的状态就会变成 if-else 地狱。状态之间的依赖关系没有显式建模——只存在于 `if` 条件的排列中。

---

### 实体创建 — Tiled 数据驱动

```lua
if map_triggers then
    for _, obj in ipairs(map_triggers) do
        if obj.type == "Player" then
            player = create_entity()
            local tex = load_texture(ASSET_DIR .. "Player.png")
            set(player, "Transform", { x = obj.x, y = obj.y, layer = 2 })
            set(player, "Sprite",    { texture_id = tex, width = obj.w, height = obj.h,
                                       src_x = 0, src_y = 0, src_w = 0, src_h = 0 })
            set(player, "Collider",  { width = obj.w, height = obj.h })
            set(player, "Velocity",  { x = 0, y = 0 })
```

**Tiled → ECS 的映射**:

| Tiled 对象字段 | 组件字段 | 说明 |
|---------------|---------|------|
| `obj.x, obj.y` | Transform.x, Transform.y | 世界坐标（像素） |
| `obj.w, obj.h` | Sprite.width/height, Collider.width/height | 尺寸（像素） |
| `obj.type` | 决定创建逻辑 | "Player"/"Npc"/其他 |
| `obj.name` | 纹理文件名 / NPC 标识 | 用于加载纹理和查找对话 |

**`src_x=0, src_y=0, src_w=0, src_h=0`**: Sprite 组件新增了源矩形字段（Spritesheet 裁剪）。全零表示"使用整张纹理"——C++ RenderSystem 将 `src_w=0` 解释为"宽度=纹理原始宽度"。

**为什么只有玩家有 `Velocity` 组件？**

```lua
-- NPC 部分:
set(e, "Collider", { width = obj.w, height = obj.h })
-- ↑ 注意: 没有 set(e, "Velocity")
```

> **设计技巧——"做减法"**: NPC 没有 `Velocity` 组件 → `PhysicSystem` 不遍历它们 → 碰撞后的推力只作用于有 `Velocity` 的玩家 → NPC"像山一样不可推动"。
>
> 这是 ECS 的优雅之处: **通过不挂载组件来定义行为**。比起添加 `is_static` 标志再在代码中 if-else，缺少组件就不参与系统逻辑更干净。

---

### NPC 中心坐标缓存

```lua
table.insert(npcs, {
    id = e,
    name = tex_name,
    w = obj.w,
    h = obj.h,
    cx = obj.x + (obj.w or 32) / 2,
    cy = obj.y + (obj.h or 32) / 2
})
```

**`cx, cy` — 预计算中心点**: 后面距离检测每帧对每个 NPC 都要算距离。预计算中心坐标避免每帧重复做 `x + w/2`。

> **语法知识 — `(obj.w or 32)`**:
>
> Lua 的 `or` 运算符返回**第一个真值**（不是 `false` 且不是 `nil` 的值）:
> ```lua
> nil or 32    → 32    (nil 是假值, 返回第二个)
> 0 or 32      → 0     (0 在 Lua 中是真值! 与 C/C++ 不同)
> false or 32  → 32    (false 是假值)
> 48 or 32     → 48    (48 是真值, 直接返回)
> ```
> 这里用作**默认值**: 如果 Tiled 对象没有定义宽度（`obj.w == nil`），使用 32 作为默认值。

---

### 触发器实体（隐形逻辑对象）

```lua
else
    local trigger_e = create_entity()
    set(trigger_e, "Transform", { x = obj.x, y = obj.y + (obj.h or 48) / 2, layer = 1 })
    set(trigger_e, "Collider", { width = obj.w or 48, height = (obj.h or 48) / 2 })
```

**设计技巧** — `y + h/2` 和 `height / 2`:

不可见触发器（如水井、灌木）的碰撞盒只占下半部分。

```
Tiled 定义的完整区域:
  ┌──────────────┐ ← y = obj.y
  │  (视觉上方)   │
  │  无碰撞      │
  ├──────────────┤ ← y + h/2 (Transform 起点)
  │  有碰撞      │
  │  半高碰撞盒  │
  └──────────────┘ ← y + h
```

**效果**: 玩家可以从上方"走到物体后面"（视觉上物体挡住玩家下半身）→ 产生 2.5D 的半透视效果。同时文字气泡从 Transform 位置向上浮现，不会飘到画面外。

---

## 第二部分：每帧更新 — `on_update()`

### HD-2D 模式切换

```lua
if is_key_down(258) then
    is_hd2d = not is_hd2d
    set_hd2d_mode(is_hd2d)
end
```

`258` 是 Raylib 的 `KEY_TAB`。按 TAB 在纯 2D 和 HD-2D（3D 透视+后处理）之间切换。

> **语法知识 — `not` 运算符**:
>
> Lua 的逻辑非。`not true → false`，`not false → true`。这里实现了"每次按键切换状态"的 toggle 效果。
>
> **注意**: `is_key_down` 每帧返回 true → 按住 TAB 会每帧切换 → 闪烁。应该用 `is_key_pressed`（仅按下那一帧触发）。这是一个轻微的设计缺陷。

---

### 摄像机跟随

```lua
local px, py = get_pos(player)
set_camera_target(px, py)
```

物理系统更新位置后，通知 C++ 的 3D 摄像机聚焦到玩家位置。HD-2D 模式下摄像机有透视投影，需要实时跟踪。

---

### 空格键边沿检测 (Edge Detection)

```lua
local space_is_down = is_key_down(32)
local space_pressed = space_is_down and not last_space_down
last_space_down = space_is_down
```

**问题**: `is_key_down` 每帧都返回 true → 按住空格会每帧推进对话。需要只在**按下的那一帧**触发。

> **软件边沿检测原理**:
>
> ```
> 帧:     1     2     3     4     5     6
> 按键:   ↓按   持    持    ↑松   -     ↓按
> 
> is_down:     T     T     T     F     F     T
> last_down:   F     T     T     T     F     F
> pressed:     T     F     F     F     F     T
>              ↑ 上升沿              ↑ 上升沿
> ```
>
> `pressed = current AND NOT previous` 只在"当前帧按着 + 上一帧没按"时为 true → 上升沿（按下瞬间）。

> **语法知识 — `last_space_down` 是全局变量**:
>
> 没有 `local` 前缀 → 全局变量 `_G["last_space_down"]`。第一帧 `last_space_down == nil`，而 `not nil → true`，所以 `space_pressed = space_is_down and true`，如果按着空格就触发。这是正确的——首次按下确实应该触发。
>
> **缺陷**: 全局变量污染。更好的做法是在文件顶部用 `local last_space_down = false` 声明。

---

### 最近 NPC 搜索

```lua
local closest_dist = 100 * 100
local target_npc = nil

for _, npc in ipairs(npcs) do
    local dist2 = (nx_center - px_center)^2 + (ny_center - py_center)^2
    if dist2 < closest_dist then
        -- 查找有效对话键 ...
        if valid_key then
            closest_dist = dist2
            target_npc = npc
        end
    end
end
```

**设计意图**: 在 100 像素半径内找到**最近**的、**有对话数据**的 NPC。

| 设计选择 | 说明 |
|---------|------|
| 距离平方 `dist2` | 避免 `math.sqrt`（~10ns/次），比较 `dist2 < r2` 与 `dist < r` 等价 |
| `100 * 100 = 10000` | 交互半径 = 100 像素 |
| 不断收紧半径 | `closest_dist = dist2` — 每找到更近的就缩小搜索范围 |

> **语法知识 — 闭包中的局部函数 `find_key`**:
>
> ```lua
> local function find_key(key)
>     if not key then return nil end
>     local lk = key:lower()
>     for k, v in pairs(dialogue_data) do
>         local clk = k:lower()
>         if clk == lk or clk == lk .. ".png" then return k end
>     end
>     return nil
> end
> ```
>
> 在循环体内定义局部函数。每次循环迭代**重新创建**一个闭包对象。
>
> **`key:lower()`**: Lua 冒号语法的方法调用。等价于 `string.lower(key)`。冒号自动把对象作为第一个参数传入。
>
> **模糊匹配**: 同时尝试 `name` 和 `name.png`，兼容 Tiled 中填不填后缀的情况。
>
> **缺陷**: 每次空格按下 × 每个 NPC 都重新创建此函数 + 遍历 `dialogue_data` 全部键。优化方案: 在加载时预建"名称 → 键"的索引 table。

---

### 分支对话状态机

```lua
local current_branch = "default"

if valid_key == "Guard_Albedo.png" then
    if global_state.guard_passed then current_branch = "passed"
    elseif global_state.has_pass then current_branch = "with_pass"
    else current_branch = "default" end
elseif valid_key == "Blacksmith.png" then
    if global_state.has_pass then current_branch = "done"
    elseif global_state.has_hammer then current_branch = "with_hammer"
    elseif progress[valid_key] and progress[valid_key]["default"] == 5 then
        current_branch = "waiting"
    else current_branch = "default" end
-- ... (类似模式)
```

**设计意图**: 每个 NPC 有多个对话分支，由 `global_state` 决定走哪个分支。

```
Guard_Albedo 的分支树:
  global_state.guard_passed → "passed" (已通过)
  global_state.has_pass     → "with_pass" (出示通行证)
  否则                       → "default" (初始对话)
```

**`progress[key]["default"] == 5`**: 对话播放完毕后（idx 重置前），标记为 5 表示"已听完所有默认对话"。这是一种简陋的"任务完成"标记。

> **设计缺陷**: 每个 NPC 的分支逻辑硬编码在 `if-elseif` 中。添加新 NPC 需要手动添加新的条件块。
>
> **改进方案**: 将分支选择逻辑放入 `dialogue_data.lua`——每个 NPC 的对话数据附带一个"分支选择函数"或"前置条件表"：
> ```lua
> ["Guard_Albedo.png"] = {
>     branches = {
>         { key = "passed", condition = function(s) return s.guard_passed end },
>         { key = "with_pass", condition = function(s) return s.has_pass end },
>         { key = "default", condition = function() return true end }
>     }
> }
> ```
> 这样新增 NPC 只需编辑数据文件，不需要改逻辑代码。

---

### 对话推进与剧情触发

```lua
progress[valid_key] = progress[valid_key] or {}
progress[valid_key][current_branch] = progress[valid_key][current_branch] or 1
local cur_idx = progress[valid_key][current_branch]

local lines = dialogue_data[valid_key][current_branch]

if lines and cur_idx <= #lines then
    set(npc.id, "TextBubble", { text = lines[cur_idx], time = 3.0 })
```

> **语法知识 — `x = x or default` 惰性初始化**:
>
> ```lua
> progress[valid_key] = progress[valid_key] or {}
> ```
> 如果 `progress[valid_key]` 是 `nil`（首次访问），`nil or {}` → 创建空 table。如果已存在则保留原值。这是 Lua 中最常见的**默认值/惰性初始化**模式。

**`set(npc.id, "TextBubble", { text = ..., time = 3.0 })`**: C++ 侧的 `TextBubble` 组件——渲染系统会在实体头顶显示文字气泡，3 秒后自动消失。

---

### 剧情里程碑触发

```lua
if valid_key == "Bush" and current_branch == "default" and cur_idx == 3 then
    global_state.has_berry = true
end
if valid_key == "statue" and current_branch == "with_berry" and cur_idx == 4 then
    global_state.has_hammer = true
end
```

**设计模式**: 在对话的特定句（`cur_idx`）触发状态变更。这将叙事节点嵌入到对话流中。

**任务链**:
```
Bush default #3         → has_berry
statue with_berry #4    → has_hammer
Blacksmith with_hammer #3 → has_pass
Guard with_pass #3      → guard_passed
```

---

### 动态组件修改（开门）

```lua
if valid_key == "Door_closed.png" and current_branch == "default" and cur_idx == 2 then
    global_state.door_opened = true
    remove(npc.id, "Collider")
    remove(npc.id, "Sprite")
    local new_tex = load_texture("../../../assets/texture/Door_open.png")
    set(npc.id, "Sprite", { texture_id = new_tex, ... })
end
```

**ECS 动态性的展示**: 运行时移除 `Collider` → 玩家可以穿过；移除并替换 `Sprite` → 视觉上门打开了。

**这正是 ECS 的强项**: 组件可以在运行时任意增删，实体的行为随之动态变化——无需继承层次或状态标志。

---

## 文件级总结

| 设计决策 | 选择 | 优势 | 缺陷 |
|---------|------|------|------|
| 实体数据来源 | Tiled 导出 | 美术可视化编辑，脚本不硬编码坐标 | 路径硬编码，无热重载 |
| 叙事架构 | 布尔 table + if-else 分支 | 极简，对话→状态 直觉 | 不可扩展，新 NPC 必须改代码 |
| NPC 搜索 | 暴力距离遍历 | 实现简单 | O(n) 每帧（但 n<20，不影响） |
| 边沿检测 | 手动 `prev AND NOT curr` | 无需额外库 | 依赖全局变量 |
| 动态开门 | `remove` + `set` 组件 | ECS 原生能力，无需额外系统 | 硬编码在脚本中 |
