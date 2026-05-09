-- 资源路径前缀
local ASSET_DIR = "../../../assets/texture/"

-- 新增：通过 Lua 脚本加载你用 Tiled 编辑的可视化地图组合
dofile("../../../scripts/map_loader.lua")
-- 动态加载瓦片，同时获取里面被美术划定的剧情包围盒坐标！
local map_ok, map_triggers = load_tiled_map("../../../assets/texture/new_map.lua")

-- 缓存地图尺寸，用于无玩家时摄像机兜底聚焦
local map_data = dofile("../../../assets/texture/new_map.lua")
local map_center_x = (map_data.width * map_data.tilewidth) / 2
local map_center_y = (map_data.height * map_data.tileheight) / 2

-- ===============================================================
-- 播放背景音乐 (只需让引擎启动时播放一次，AudioSystem 会在后台持续刷新流)
-- 请确保这个文件在你的电脑上存在！(如果你改了名字，就在这里修改它)
play_bgm("../../../assets/audio/bgm.ogg")
-- ===============================================================

-- 遍历数据，自动创建所有实体
local player = nil
local npcs = {} -- 新增：保存所有可互动 NPC 列表
local npc_by_name = {}

local COLLISION_PLAYER = 0x0001
local COLLISION_STATIC = 0x0002

local function png_name(name)
    if not name then return nil end
    if name:match("%.png$") then return name end
    return name .. ".png"
end

local function normal_name(tex_name)
    if not tex_name then return nil end
    return tex_name:gsub("%.png$", "_n.png")
end

local NPC_PROFILES = {
    Village_Head = {
        display = "村长",
        weights = {3, 2, 3, 5, 2, 1},
        satisfaction = {5, 4, 4, 3, 4, 2},
        expectation = {5, 4, 4, 5, 4, 2},
        tablet_online = true
    },
    Blacksmith = {
        display = "铁匠",
        weights = {5, 3, 4, 2, 1, 2},
        satisfaction = {3, 3, 4, 3, 1, 3},
        expectation = {5, 3, 4, 3, 1, 3},
        tablet_online = true
    },
    Father = {
        display = "神父",
        weights = {2, 4, 2, 2, 5, 2},
        satisfaction = {3, 3, 3, 3, 3, 3},
        expectation = {3, 3, 3, 3, 5, 3},
        tablet_online = true
    },
    Bartender = {
        display = "酒保",
        weights = {3, 5, 2, 2, 1, 2},
        satisfaction = {3, 4, 3, 3, 2, 3},
        expectation = {3, 5, 3, 3, 2, 3},
        tablet_online = true
    }
}

local function object_center(obj)
    local w = obj.w or 32
    local h = obj.h or 32
    return obj.x + w / 2, obj.y + h / 2
end

local function add_interactable(e, obj, tex_name, fallback_type)
    local cx, cy = object_center(obj)
    table.insert(npcs, {
        id = e,
        name = tex_name or obj.name,
        type = obj.type ~= "" and obj.type or fallback_type,
        w = obj.w,
        h = obj.h,
        cx = cx,
        cy = cy
    })
end

local function attach_identity(e, name, display_name)
    set(e, "Identity", {
        name = name,
        display_name = display_name or name
    })
end

local function attach_ai(e, obj)
    local profile = NPC_PROFILES[obj.name]
    if not profile then return end

    attach_identity(e, obj.name, profile.display)
    set(e, "Need", {
        weights = profile.weights,
        satisfaction = profile.satisfaction,
        expectation = profile.expectation
    })
    set(e, "Emotion", {
        intensity = {0, 0, 0, 0, 0},
        target = {nil, nil, nil, nil, nil},
        decay_rate = {0.10, 0.10, 0.20, 0.05, 0.05}
    })
    set(e, "StoneTablet", {
        online = profile.tablet_online,
        owner = e,
        broadcast_range = 100
    })
    set(e, "Decision", {
        target = nil,
        action_id = 0,
        progress = 1.0,
        next_tick = 0
    })
    npc_by_name[obj.name] = e
end

local function create_relation(from_name, to_name, affinity, power_diff)
    local from = npc_by_name[from_name]
    local to = npc_by_name[to_name]
    if not from or not to then return end
    local edge = create_entity()
    set(edge, "Relation", {
        from = from,
        to = to,
        affinity = affinity,
        power_diff = power_diff
    })
end

local function create_player(obj)
    local e = create_entity()
    local tex = load_texture(ASSET_DIR .. "Player.png")
    local nor = load_texture(ASSET_DIR .. "Player_n.png")
    local w = obj.w or 64
    local h = obj.h or 64

    set(e, "Transform", { x = obj.x, y = obj.y, layer = 2 })
    set(e, "Sprite", { texture_id = tex, normal_id = nor, width = w, height = h, src_x = 0, src_y = 0, src_w = 0, src_h = 0 })
    set(e, "Collider", {
        width = w * 0.45,
        height = h * 0.25,
        offset_x = w * 0.275,
        offset_y = h * 0.70,
        layer = COLLISION_PLAYER,
        mask = COLLISION_STATIC
    })
    set(e, "Velocity", { x = 0, y = 0 })
    attach_identity(e, "Player", "主角")
    set(e, "Need", {
        weights = { 4, 3, 5, 2, 1, 2 },
        satisfaction = { 4, 3, 0, 2, 1, 2 },
        expectation = { 2, 1, 0, 1, 1, 2 }
    })
    return e
end

local function create_static_sprite(obj, fallback_type)
    local tex_name = png_name(obj.texture or obj.name)
    local tex = load_texture(ASSET_DIR .. tex_name)
    local nor_name = normal_name(tex_name)
    local nor = nor_name and load_texture(ASSET_DIR .. nor_name) or 0
    local w = obj.w or 128
    local h = obj.h or 128
    local e = create_entity()

    set(e, "Transform", { x = obj.x, y = obj.y, layer = 2 })
    set(e, "Sprite", { texture_id = tex, normal_id = nor, width = w, height = h, src_x = 0, src_y = 0, src_w = 0, src_h = 0 })
    attach_identity(e, obj.name, obj.name)
    -- 只有 Collider，没有 Velocity：物理/碰撞系统会把它当静态实体。
    set(e, "Collider", {
        width = w * 0.55,
        height = h * 0.28,
        offset_x = w * 0.225,
        offset_y = h * 0.68,
        layer = COLLISION_STATIC,
        mask = COLLISION_PLAYER
    })
    add_interactable(e, obj, tex_name, fallback_type)
    if fallback_type == "Npc" then
        attach_ai(e, obj)
    end
    return e
end

-- ===============================================================
-- Tiled 驱动剧情时代：接收美术侧画下的对象区域数据！取代原有的硬坐标
-- ===============================================================
if map_triggers then
    for _, obj in ipairs(map_triggers) do
        
        if obj.type == "Player" then
            -- 装配主角实体
            player = create_player(obj)
            
        elseif obj.type == "Npc" then
            -- 装配可见的 NPC 实体
            create_static_sprite(obj, "Npc")
            
        elseif obj.gid and obj.gid > 0 then
            -- new_map 里石碑 type 为空，但有 gid/texture；它应该是可见、可碰撞、不可推动的静态物。
            create_static_sprite(obj, "Static")

        else
            -- 装配隐形逻辑触发器 (Well, Bush, Chest)
            local trigger_e = create_entity()
            -- 将 Transform 往下挪半截。好处一：文字气泡不会再因为体积太高而飘到外太空；好处二：实现 2.5D 半透视碰撞（可以绕到物体后面半穿模）
            set(trigger_e, "Transform", { x = obj.x, y = obj.y + (obj.h or 48) / 2, layer = 1 })
            set(trigger_e, "Collider", { width = obj.w or 48, height = (obj.h or 48) / 2 })
            
            -- npc表新增 type 保存，作为查字典不到时的 fallback
            add_interactable(trigger_e, obj, obj.name, obj.type)
        end
    end
end

create_relation("Blacksmith", "Village_Head", -40, -50)
create_relation("Village_Head", "Blacksmith", 0, 50)
create_relation("Blacksmith", "Father", 30, 0)
create_relation("Father", "Village_Head", 20, -10)
create_relation("Father", "Blacksmith", 30, 0)
create_relation("Village_Head", "Father", 10, 10)
create_relation("Bartender", "Blacksmith", 25, 0)
create_relation("Blacksmith", "Bartender", 25, 0)
create_relation("Bartender", "Village_Head", 10, -20)

-- [临时测试] Tiled 对象层为空时，手动创建 Player
if not player then
    player = create_player({ x = map_center_x, y = map_center_y, w = 128, h = 128 })
    set(player, "Emotion",{
        intensity = { 4, 3, 2, 3, 2 },
        target = { nil, nil, nil, nil, nil },
        decay_rate = { 0.8, 0.3, 1.2, 0.3, 1.7 }
    })
    print("[TEST] Manual player created at map center: " .. map_center_x .. ", " .. map_center_y)
end
-- ===============================================================



-- 每帧逻辑
function on_update()
    if not player then
        -- 没有玩家实体时，摄像机兜底聚焦到地图中央，防止白屏
        set_camera_target(map_center_x, map_center_y)
        return
    end



    local dx, dy = 0, 0
    if is_key_down(87) then dy = -1 end
    if is_key_down(83) then dy =  1 end
    if is_key_down(65) then dx = -1 end
    if is_key_down(68) then dx =  1 end
    move(player, dx, dy)

    -- HD-2D 新增：物理逻辑跑完之后，通知 C++ 的 3D 摄像机聚焦到此
    local px, py = get_pos(player)
    set_camera_target(px, py)
end
