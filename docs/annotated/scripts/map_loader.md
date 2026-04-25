# map_loader.lua — Tiled 地图加载器（深度注释版）

> 文件路径: `scripts/map_loader.lua`  
> 角色: 解析 Tiled 地图编辑器导出的 Lua 格式地图文件，将**瓦片数据**转化为 ECS 实体，并提取**对象层的逻辑触发器**供剧情系统使用。是连接"美术编辑器 → 游戏引擎"的桥梁。

---

## 文件级设计意图

**问题**: 游戏关卡的地形、障碍物、NPC 位置如何高效制作？

| 方案 | 创作方式 | 优势 | 缺陷 |
|------|---------|------|------|
| 代码硬编码 | 程序员在脚本中写坐标 | 零依赖 | 不可维护、无法可视化 |
| JSON/YAML 数据文件 | 手写数据结构 | 可编辑 | 仍不直观 |
| **Tiled + Lua 导出（当前）** | **可视化拖拽编辑** | **所见即所得** | 需要解析器 |
| 自定义编辑器 | 完全定制 | 最灵活 | 开发成本高 |

**Tiled** 是免费开源的 2D 地图编辑器，支持导出为多种格式。导出为 Lua 格式时，生成的 `.lua` 文件本身就是一个返回 table 的 Lua 脚本——可以直接 `dofile` 加载，零解析成本。

**本文件的职责**:
1. 加载 Tiled 导出的 `.lua` 地图
2. 解析图集 (Tileset) 信息，建立 GID → 纹理+UV 的映射
3. 遍历瓦片层 (Tile Layer)，为每个非空瓦片创建 ECS 实体
4. 遍历对象层 (Object Layer)，提取逻辑触发器给 `main.lua` 使用

---

## Tiled 数据结构概述

Tiled 导出的 Lua 文件返回一个 table，结构如下:

```lua
return {
    width = 20,          -- 地图宽度（瓦片数）
    height = 15,         -- 地图高度（瓦片数）
    tilewidth = 32,      -- 每个瓦片的像素宽度
    tileheight = 32,     -- 每个瓦片的像素高度
    
    tilesets = {          -- 图集列表
        { firstgid = 1, tilecount = 256, columns = 16,
          tilewidth = 32, tileheight = 32,
          image = "../texture/tileset.png" },
        { firstgid = 257, ... }      -- 第二个图集
    },
    
    layers = {            -- 层列表
        { type = "tilelayer", width = 20, height = 15,
          data = { 1, 2, 0, 3, ... } },   -- GID 数组
        { type = "objectgroup",
          objects = {
              { name = "Player", type = "Player", x = 100, y = 200, width = 32, height = 32 },
              { name = "Guard", type = "Npc", x = 300, y = 100, ... }
          }
        }
    }
}
```

**关键概念 — GID (Global Tile ID)**: Tiled 给地图中的每个瓦片分配一个全局 ID。第一个图集的 GID 从 `firstgid` 开始（通常是 1），第二个图集紧接着。GID=0 表示空瓦片。

---

## 逐行注释

### 函数签名

```lua
local ASSET_DIR = "../../../assets/texture/"

function load_tiled_map(map_path)
    local map = dofile(map_path)
    if not map then
        print("Failed to load map: " .. map_path)
        return false
    end
```

**全局函数 `load_tiled_map`**: 没有 `local` → 全局函数。`main.lua` 通过 `dofile` 加载本文件后可以直接调用。

> **语法知识 — `..` 字符串连接**:
>
> Lua 的字符串拼接运算符。等价于 C++ 的 `+`（`std::string`）或 Python 的 `+`:
> ```lua
> "Hello" .. " " .. "World"  → "Hello World"
> "count: " .. 42            → "count: 42" (数字自动转字符串)
> ```
>
> **性能注意**: 每次 `..` 创建一个新字符串。大量拼接应用 `table.concat`:
> ```lua
> -- 慢 (N 次分配):
> local s = ""
> for i = 1, 1000 do s = s .. "x" end
>
> -- 快 (1 次分配):
> local t = {}
> for i = 1, 1000 do t[i] = "x" end
> local s = table.concat(t)
> ```

---

### 第一步：解析图集 (Tileset)

```lua
local gid_info = {}
for _, ts in ipairs(map.tilesets) do
    local filename = ts.image:match("([^/\\]+)$") or ts.image
    local full_path = ASSET_DIR .. filename
    local tex_id = load_texture(full_path)

    table.insert(gid_info, {
        firstgid = ts.firstgid,
        lastgid = ts.firstgid + ts.tilecount - 1,
        tex_id = tex_id,
        columns = ts.columns,
        tilewidth = ts.tilewidth,
        tileheight = ts.tileheight
    })
end
```

> **语法知识 — Lua 正则模式 `ts.image:match("([^/\\]+)$")`**:
>
> Lua 不使用标准正则表达式，而是一套简化的**模式匹配**系统:
>
> | 模式 | 含义 |
> |------|------|
> | `.` | 任意字符 |
> | `%a` | 字母 |
> | `%d` | 数字 |
> | `[^abc]` | 不在集合中的任意字符 |
> | `+` | 一个或多个 |
> | `*` | 零个或多个 |
> | `$` | 字符串末尾锚定 |
> | `(...)` | 捕获组 |
>
> **`([^/\\]+)$` 拆解**:
> | 部分 | 含义 |
> |------|------|
> | `[^/\\]` | 不是 `/` 也不是 `\` 的字符 |
> | `+` | 一个或多个连续这样的字符 |
> | `$` | 直到字符串结尾 |
> | `(...)` | 捕获匹配的部分作为返回值 |
>
> **效果**: 从路径 `"../texture/tileset.png"` 中提取文件名 `"tileset.png"`。
>
> **为什么需要这步？** Tiled 导出的路径是相对于 `.tmx/.lua` 文件的路径（如 `../asserts/texture/tileset.png`），但游戏运行时的工作目录不同。提取纯文件名后，用可靠的 `ASSET_DIR` 前缀重新拼接，避免路径错误。

> **语法知识 — `ts.image:match(pat)` 冒号调用**:
>
> Lua 的冒号语法是方法调用的语法糖:
> ```lua
> ts.image:match(pat)    -- 冒号语法 (隐式传 self)
> string.match(ts.image, pat)  -- 等价的点号语法
> ```
> 冒号版更简洁。它自动将 `ts.image` 作为 `string.match` 的第一个参数。

**GID 范围计算**: `lastgid = firstgid + tilecount - 1`

```
图集 A: firstgid=1, tilecount=256 → GID 范围 [1, 256]
图集 B: firstgid=257, tilecount=128 → GID 范围 [257, 384]
```

---

### 第二步：遍历瓦片层

```lua
for _, layer in ipairs(map.layers) do
    if layer.type == "tilelayer" then
        for idx, gid in ipairs(layer.data) do
            if gid > 0 then
```

**`layer.data`**: 一维数组，长度 = `width × height`。按行优先存储（从左到右、从上到下）。`gid > 0` 过滤空瓦片（GID=0）。

---

#### GID → 图集匹配

```lua
local ts_data = nil
for _, ts in ipairs(gid_info) do
    if gid >= ts.firstgid and gid <= ts.lastgid then
        ts_data = ts
        break
    end
end
```

**线性搜索**: 遍历图集列表找到 GID 所属的图集。通常只有 1-3 个图集，O(n) 无压力。

---

#### UV 坐标计算

```lua
local local_id = gid - ts_data.firstgid
local col = local_id % ts_data.columns
local row = math.floor(local_id / ts_data.columns)
local src_x = col * ts_data.tilewidth
local src_y = row * ts_data.tileheight
```

**从 GID 到纹理像素坐标的映射**:

```
图集纹理 (Spritesheet):
  columns = 4, tilewidth = 32

  [GID 0][GID 1][GID 2][GID 3]    ← row 0
  [GID 4][GID 5][GID 6][GID 7]    ← row 1
  
  local_id = gid - firstgid
  
  例: local_id = 5
  col = 5 % 4 = 1
  row = floor(5 / 4) = 1
  src_x = 1 * 32 = 32
  src_y = 1 * 32 = 32
  → 从纹理的 (32, 32) 位置裁剪 32×32 像素
```

> **语法知识 — `%` 取模和 `math.floor`**:
>
> Lua 的 `%` 对浮点数也有效（不像 C++ 中 `%` 只用于整数）。`math.floor` 向下取整。
>
> 这两个操作的组合实现了"一维索引 → 二维行列"的经典转换:
> ```
> col = index % width    (列 = 索引对宽度取模)
> row = index / width    (行 = 索引除以宽度取整)
> ```
> 这在任何网格/瓦片系统中都会用到。

---

#### 世界坐标计算

```lua
local tile_x = (idx - 1) % layer.width
local tile_y = math.floor((idx - 1) / layer.width)
local world_x = tile_x * map.tilewidth
local world_y = tile_y * map.tileheight
```

**`idx - 1`**: Lua 数组从 1 开始，但行列索引应该从 0 开始。减 1 修正偏移。

```
layer.data 示例 (width=3):
  idx:  1    2    3    4    5    6
  gid:  1    2    3    4    5    6
  
  idx=4 → tile_x = (4-1) % 3 = 0, tile_y = floor(3/3) = 1
  → 世界坐标 = (0 × 32, 1 × 32) = (0, 32)
```

---

#### 创建瓦片实体

```lua
local e = create_entity()
set(e, "Transform", {
    x = world_x,
    y = world_y,
    layer = layer.id or 0
})

if layer.id == 2 then
    set(e, "Collider", { width = map.tilewidth, height = map.tileheight })
end

set(e, "Sprite", {
    texture_id = ts_data.tex_id,
    width = map.tilewidth,
    height = map.tileheight,
    src_x = src_x, src_y = src_y,
    src_w = ts_data.tilewidth, src_h = ts_data.tileheight
})
```

**`layer.id` 作为渲染层级**: Tiled 的层 ID 直接映射为 `Transform.layer`。层级高的绘制在层级低的上面。

**`layer.id == 2` 自动添加碰撞**: 约定第 2 层的瓦片全部是障碍物（树木、墙壁、栅栏等）。

> **设计选择 — 层级约定 vs 瓦片属性**:
>
> | 方案 | 方式 | 优势 | 缺陷 |
> |------|------|------|------|
> | **层级约定（当前）** | 整个层统一加碰撞 | 简单，对美术友好 | 不灵活（同层不能混合可/不可通行） |
> | Tiled 瓦片属性 | 每个瓦片标记 collision=true | 精确控制 | Tiled 导出数据更复杂 |
> | 碰撞地图 | 单独画一层布尔碰撞网格 | 直观 | 需要额外层 |

**Sprite 的 `src_x/y/w/h`**: 这是 Spritesheet 裁剪参数。C++ 的 `RenderSystem` 用这些值从大纹理中切出对应的瓦片区域来渲染。

---

### 第三步：遍历对象层

```lua
elseif layer.type == "objectgroup" then
    if layer.objects then
        for _, obj in ipairs(layer.objects) do
            local trigger_name = (obj.name and obj.name ~= "") and obj.name or obj.type
            if trigger_name and trigger_name ~= "" then
                table.insert(map_triggers, {
                    name = trigger_name,
                    type = obj.type,
                    x = obj.x, y = obj.y,
                    w = obj.width, h = obj.height
                })
            end
        end
    end
end
```

**对象层 vs 瓦片层**: Tiled 有两种层类型:
- **瓦片层**: 网格化的图块数据（地面、墙壁）
- **对象层**: 自由放置的矩形/多边形/点（NPC 出生点、触发区域、摄像机边界）

**`trigger_name` 优先级**: 优先用 `obj.name`（美术在 Tiled 中手动填的名字），如果为空则用 `obj.type`（Tiled 的 Class 字段）。

> **语法知识 — Lua 三元表达式模拟**:
>
> ```lua
> local trigger_name = (obj.name and obj.name ~= "") and obj.name or obj.type
> ```
>
> Lua 没有 C 的 `?:` 三元运算符。用 `and/or` 模拟:
> ```lua
> condition and value_if_true or value_if_false
> ```
>
> **原理**: 
> - `A and B`: A 真 → 返回 B；A 假 → 返回 A
> - `A or B`: A 真 → 返回 A；A 假 → 返回 B
>
> 组合: `(true_cond) and val1 or val2` → `val1`  
> 组合: `(false_cond) and val1 or val2` → `val2`
>
> **陷阱**: 如果 `val1` 本身是 `false` 或 `nil`，整个表达式会返回 `val2` 而非 `val1`。这里 `obj.name` 是字符串，不会是 false，所以安全。

**返回值**: 函数最终返回 `(true, map_triggers)` — 成功标志 + 触发器列表，供 `main.lua` 使用来创建玩家、NPC 和逻辑触发器实体。

---

## 文件级总结

| 设计决策 | 选择 | 优势 | 缺陷 |
|---------|------|------|------|
| 地图格式 | Tiled Lua 导出 | dofile 直接加载，零解析 | 依赖 Tiled 工具链 |
| 路径处理 | 正则提取文件名+重拼 | 兼容 Tiled 的奇怪相对路径 | 假设所有资源在同一目录 |
| 碰撞标记 | 按层级约定 (layer 2) | 简单粗暴对美术友好 | 不精确，同层无法混合 |
| GID 搜索 | 线性遍历图集列表 | 图集数少(1-3)，够快 | 图集多时可用二分 |
| 实体创建 | 每瓦片一个实体 | 统一 ECS 模型 | 20×15 地图=300 实体，大地图需要合批/分块 |
