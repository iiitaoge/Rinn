# 第 6 章 资源管理与脚本系统

ECS 与子系统共同决定了引擎"能做什么"，但要让引擎真正运行起一个游戏 demo，还需要打通两条通道：与磁盘资源的 I/O 通道，以及与策略层（脚本）的指令通道。本章先介绍 `ResourceManager` 如何用 ID 句柄替代裸指针来管理纹理资源；再展开 Lua 状态初始化与 sol2 双向绑定的实现细节；之后以 Tiled 地图加载为例，说明引擎如何从外部编辑器导出的 Lua 数据中重建 ECS 场景；最后介绍以 Lua 表声明、引擎不感知具体业务的对话分支系统。

## 6.1 ResourceManager：纹理 ID 化

游戏运行时需要频繁引用同一份资源（例如多个瓦片共享同一张图集），如果在每个组件中保存裸指针 `Texture2D*`，会带来三方面问题：一是组件丧失 `is_trivially_copyable_v` 性质，破坏 ECS 的内存约束；二是资源生命周期与组件耦合，难以集中管理；三是序列化时指针不可移植。ID 化是工业引擎的通行解法。

### 6.1.1 数据布局与对外接口

`ResourceManager` 内部仅持有两个容器：

```cpp
class ResourceManager {
    std::vector<Texture2D> textures;
    std::unordered_map<std::string, uint16_t> path_to_id;
public:
    uint16_t  load_texture(const std::string& path);
    Texture2D& get_texture(uint16_t id);
    void      unload_all();
    ~ResourceManager() { if (!textures.empty()) unload_all(); }
};
```

`textures` 以连续向量存放所有已加载的 `Texture2D` 句柄，下标即 ID；`path_to_id` 提供"路径 → ID"的反向查询，用于命中检测。`Sprite::texture_id` 与 `Sprite::normal_id` 在组件中均以 ID 而非指针保存，使得 `Sprite` 仍是聚合类型与可平凡拷贝。

### 6.1.2 load_texture：去重 + 单点加载

```cpp
inline uint16_t ResourceManager::load_texture(const std::string& path) {
    auto it = path_to_id.find(path);
    if (it != path_to_id.end()) return it->second;        // 已加载
    Texture2D tex = LoadTexture(path.c_str());
    uint16_t  id  = static_cast<uint16_t>(textures.size());
    textures.push_back(tex);
    path_to_id[path] = id;
    return id;
}
```

去重逻辑非常关键：当 Tiled 地图层中数百个瓦片实体引用同一图集时，路径哈希命中后直接返回旧 ID，避免重复加载。`get_texture(id)` 使用 `assert` 校验越界，然后以 O(1) 时间返回引用。`unload_all` 在程序退出时统一调用 `UnloadTexture`，析构函数对此再做兜底，保证不会泄漏 GPU 资源。

### 6.1.3 ID 类型契约

值得一提的是当前实现存在一处类型契约的小不一致：`load_texture` 返回 `uint16_t`，而 `Sprite::texture_id` 与 `normal_id` 字段均为 `size_t`。两端依靠隐式转换互通，并未在类型层面被锁住。第 9 章的不足之处中将其列出，建议未来引入 `using TextureHandle = uint16_t;` 来统一两端。

> 代码引用：`src/Resources/ResourceManager.hpp`

## 6.2 Lua 状态初始化与 sol2 绑定

脚本子系统是 ECS 引擎与外部"内容"之间的桥梁。Project Rinn 选择 Lua 5.4 作为嵌入式脚本语言，借助 sol2 实现 C++ 与 Lua 的双向绑定。

### 6.2.1 Lua 状态的初始化

`ScriptContext.hpp` 仅做一件事——开启 Lua 的最基本标准库：

```cpp
inline void Init_lua(sol::state& lua) {
    lua.open_libraries(sol::lib::base, sol::lib::math);
}
```

`sol::lib::base` 提供 `print`、`type`、`pairs` 等核心函数；`sol::lib::math` 提供 `math.floor`、`math.random` 等数学函数。其余库（`string`、`table`）在 `bind` 时再开启，按需引入以最小化暴露面。`sol::state` 是 sol2 对 `lua_State*` 的 RAII 封装，自动管理 Lua 解释器的生命周期。

### 6.2.2 黑盒导出 Entity

引擎将 `Entity` 类型导出给 Lua，但不暴露任何字段：

```cpp
lua.new_usertype<Entity>("Entity");
```

这种"黑盒导出"使得 Lua 端只能将 Entity 视作不透明句柄传递，不能直接读取其 `index / generation / id`，符合"句柄的内部布局是 C++ 的实现细节，脚本不应依赖"的封装原则。

### 6.2.3 通用组件操作：set / remove / has

最具代表性的绑定是 `set` 函数，它接受实体、组件名字符串与 Lua 表，根据字符串分发到对应的 `emplace<T>`：

```cpp
lua.set_function("set", [&reg](Entity e, const std::string& name, sol::table data) {
    if (name == "Transform") {
        reg.emplace<Transform>(e,
            data.get<float>("x"), data.get<float>("y"),
            data["layer"].get_or(0));
    }
    else if (name == "Velocity") { /* ... */ }
    else if (name == "Sprite")   { /* ... */ }
    else if (name == "Collider") { /* ... */ }
    else if (name == "TextBubble") { /* ... */ }
});
```

`data.get<T>("key")` 在 Lua 表中以类型安全方式取出字段，必填项缺失时立即抛出错误；`data["k"].get_or(default)` 提供"可选字段+默认值"的便捷写法。这种"字符串 dispatch"的设计有两个优点：Lua 端使用直观（`set(e, "Sprite", {texture_id=tex, width=32, ...})`），并且新增组件只需在 C++ 端追加一个 `else if`、不修改 Lua 已有调用。其代价是字段名硬编码、运行时字符串比较开销、以及编译期无法校验字段名拼写——这些被列入第 9 章的不足之处。

`remove` 函数的实现方式相同，按字符串选择要从哪个池中移除组件。引擎当前为 `TextBubble`、`Collider`、`Sprite` 三类组件实现了 remove，对其他组件可按需追加。

### 6.2.4 资源、输入、相机的 facade 接口

除了组件操作，绑定层还把以下能力以最薄的 facade 形式暴露给 Lua：

| Lua 函数 | C++ 实现 | 用途 |
|----------|---------|------|
| `load_texture(path)` | `res.load_texture(path)` | 返回纹理 ID |
| `play_bgm(path)` | `AudioSystem::PlayBGM(path)` | 播放背景音乐 |
| `create_entity()` | `reg.create_entity()` | 新建实体 |
| `get_pos(e) / get_vel(e)` | `reg.get<Transform/Velocity>(e)` | 读取位置 / 速度 |
| `is_key_down(key)` | `InputSystem::is_key_down(key)` | 输入查询 |
| `move(e, dx, dy)` | 设置 Velocity，含速度常数 200 | 角色移动 |
| `get_collisions()` | `CollisionSystem::detect(reg)` | 获取碰撞对 |
| `set_camera_target(x, y)` | `RenderSystem::UpdateCamera(x, y)` | 相机跟随 |

需要特别注意的是 `move` 的实现：

```cpp
lua.set_function("move", [&reg](Entity e, float dx, float dy) {
    auto& v = reg.get<Velocity>(e);
    constexpr float speed = 200.0f;
    v.vx = dx * speed;
    v.vy = dy * speed;
});
```

物理常数 `speed = 200.0f` 被故意保留在 C++ 侧。这一选择体现了"机制归机制、策略归策略"的边界划分：Lua 决定"按下 W 键就让玩家向上移动"，C++ 决定"具体每秒走多少像素"。如此一来，调整角色速度无需改动脚本，调整角色操控逻辑无需重新编译。

> 代码引用：`src/Scripting/{ScriptContext, LuaBinder}.hpp`

## 6.3 Tiled 地图驱动的关卡构造

将关卡布局以 Lua 数组的方式硬编码在脚本里，对程序员来说勉强可行，对美术与策划来说则完全不可接受。Tiled 是一款开源、跨平台的瓦片地图编辑器，能够将像素美术绘制的图层、对象框、属性数据一并导出为 Lua 文件，是独立游戏圈层中的事实标准。Project Rinn 编写了 `scripts/map_loader.lua`，实现 Tiled Lua 格式到 ECS 实体的自动转换。

### 6.3.1 Tiled 数据模型与 firstgid 机制

Tiled 导出的 Lua 表包含若干图层（`layers`）与图块集（`tilesets`）。每个图块集声明自己的 `firstgid`——该图集在全局 GID 编号空间中的起始位置。图层中每个瓦片以 GID 索引，加载器需要根据 `firstgid` 查表反推出"GID 属于哪个图集、在该图集内的局部 ID 是多少"。`map_loader.lua` 第一步即遍历 `tilesets` 构造 `gid_info` 表：

```lua
for _, ts in ipairs(map.tilesets) do
    local filename = ts.image:match("([^/\\]+)$") or ts.image
    local full_path = ASSET_DIR .. filename
    local tex_id = load_texture(full_path)
    table.insert(gid_info, {
        firstgid  = ts.firstgid,
        lastgid   = ts.firstgid + ts.tilecount - 1,
        tex_id    = tex_id,
        columns   = ts.columns,
        tilewidth = ts.tilewidth,
        tileheight = ts.tileheight
    })
end
```

`ts.image:match("([^/\\]+)$")` 这一正则用于剥离 Tiled 导出的"乱七八糟前缀"（例如 `../asserts/`），仅保留文件名再拼接到引擎可控的 `ASSET_DIR`，避免不同操作系统、不同工作目录下的路径漂移。

### 6.3.2 翻转位剥离与图集匹配

Tiled 在 GID 的最高 4 位中编码了"水平翻转、垂直翻转、对角翻转"等信息，一个原始 GID 可能形如 `2147483706`，其中低 28 位才是真实的图块编号。加载器使用 `gid = gid % (2^28)` 剥离翻转位（取模相比按位与的优点是可在 Lua 5.1/5.2/5.3/5.4 全版本兼容），随后线性扫描 `gid_info` 找到匹配的图集：

```lua
gid = gid % (2^28)
if gid > 0 then
    local ts_data = nil
    for _, ts in ipairs(gid_info) do
        if gid >= ts.firstgid and gid <= ts.lastgid then ts_data = ts; break end
    end
    -- 计算 src_x / src_y / world_x / world_y …… 创建实体
end
```

`gid > 0` 用作"该格为空"的兜底跳过——这一减法一举省下对地面层中大量 0 值瓦片的实体创建。

### 6.3.3 图块层 → ECS 实体

图块层中每个非零 GID 都对应一个 `Transform + Sprite` 实体，`Sprite::is_ground = true` 用于在 RenderSystem 中走"地面渲染"分支。加载器同时计算了从图集中切出的 UV 矩形：

```lua
local local_id = gid - ts_data.firstgid
local col = local_id % ts_data.columns
local row = math.floor(local_id / ts_data.columns)
local src_x = col * ts_data.tilewidth
local src_y = row * ts_data.tileheight
local tile_x = (idx - 1) % layer.width
local tile_y = math.floor((idx - 1) / layer.width)
local world_x = tile_x * map.tilewidth
local world_y = tile_y * map.tileheight
```

一旦 `set` 调用完成，整张地图所有可见瓦片便以 ECS 实体的形式存在于 Registry 中，后续渲染、碰撞、检视都可以无差别访问它们——这正是数据驱动的最大魅力。

### 6.3.4 对象层：墙体与触发器

Tiled 的对象层（`objectgroup`）允许美术绘制非网格对齐的矩形或多边形，并赋予类型与名称。引擎将对象层分两类处理：

```lua
if obj_type == "Wall" or obj_type == "Collision" then
    local wall = create_entity()
    set(wall, "Transform", { x = obj.x, y = obj.y, layer = 0 })
    set(wall, "Collider", {
        width = obj.width, height = obj.height,
        layer = 0x0002, mask = 0x0001
    })
else
    -- 剧情触发器收集到 map_triggers 表，留给 main.lua 处理
    table.insert(map_triggers, { name = ..., type = ..., x = ..., ... })
end
```

`Wall / Collision` 类型直接转化为纯物理实体——只有 `Transform + Collider`，没有 `Sprite`，永远参与碰撞但不参与渲染。其他类型（`Player / Npc / Bush / Chest` 等）作为"剧情触发器"返回给 `main.lua`，由后者根据具体类型决定如何装配实体。这种"加载器只关心几何与碰撞、装配交给业务"的分工，使加载器对未来新增的对象类型保持开放。

### 6.3.5 visible 字段与做减法

Tiled 中被设为隐藏的图层会带 `layer.visible == false`。加载器对其直接 `goto continue_layer` 跳过，不加载任何瓦片。这条小小的"减法"使得美术可以方便地保留草稿层而不影响发布版本。

> 代码引用：`scripts/map_loader.lua`、`scripts/main.lua`

## 6.4 数据驱动剧情：分支 + flag 系统

Project Rinn demo 中的对话与剧情推进，全部以 Lua 表的方式声明在 `scripts/dialogue_data.lua` 中，引擎本身不感知任何具体业务语义。

### 6.4.1 数据结构

```lua
return {
    ["Guard_Albedo.png"] = {
        { id = "passed",    when = { "guard_passed" },
          lines = { "少废话，快进去……" } },
        { id = "with_pass", when = { "has_pass" },
          lines = { "嗯？这是…通行证？！", "印章看起来没问题……", "算你走运，进去吧……" },
          on_line = 3, gives = "guard_passed" },
        { id = "default",   when = { },
          lines = { "站住！前方的旧城区已被封锁。", "想过去？得有镇长签发的[通行证]！" } },
    },
    -- 其他 NPC / 触发器 ……
}
```

每个 NPC 或触发器对应一组按优先级排列的"分支"：每个分支声明若干个进入条件 flag（`when`）、若干行对话（`lines`），可选地在播放到第几行（`on_line`）时给予某个 flag（`gives`）或执行任意 Lua 回调（`effect`）。

### 6.4.2 通用分支推演引擎

`main.lua` 中实现了引擎不依赖任何具体内容的分支选择函数：

```lua
local function resolve_branch(branches)
    for _, b in ipairs(branches) do
        local ok = true
        for _, f in ipairs(b.when) do
            if not flags[f] then ok = false; break end
        end
        if ok then return b end
    end
end
```

按声明顺序找到第一个 `when` 全部满足的分支即为当前应展示分支，符合"特殊条件优先于默认条件"的剧本写作直觉。

### 6.4.3 距离驱动的"按空格交互"

主循环中，按下空格键时遍历所有 NPC 找到距离最近的合法目标，调用 `resolve_branch` 取出当前分支并播放下一行：

```lua
if cur_idx <= #branch.lines then
    set(npc.id, "TextBubble", { text = branch.lines[cur_idx], time = 3.0 })
    if branch.on_line and cur_idx == branch.on_line then
        if branch.gives  then flags[branch.gives] = true end
        if branch.effect then branch.effect(npc) end
    end
    progress[key][bid] = cur_idx + 1
else
    remove(npc.id, "TextBubble"); progress[key][bid] = 1
end
```

副作用（赠予 flag、执行 effect 回调）由数据声明、引擎统一触发——这是数据驱动的核心特征。`effect` 回调可以是任意 Lua 函数，例如踹开木门时切换纹理与移除 Collider：

```lua
effect = function(npc)
    remove(npc.id, "Collider")
    remove(npc.id, "Sprite")
    local new_tex = load_texture("../../../assets/texture/Door_open.png")
    set(npc.id, "Sprite", { texture_id = new_tex, width = npc.w, height = npc.h, ... })
end
```

如此一来，新增剧情场景或新增 NPC 完全不需要改动 C++ 代码：策划与脚本作者编辑 `dialogue_data.lua` 即可上线新内容，并且引擎对所有 NPC 一视同仁地处理。

> 代码引用：`scripts/dialogue_data.lua`、`scripts/main.lua`

---

资源管理与脚本子系统至此完整。引擎已经具备从磁盘加载资源、由 Lua 驱动场景与剧情、由 ECS 与子系统执行运行时行为的全部能力。下一章将转向视觉风格：HD-2D 渲染原理验证。
