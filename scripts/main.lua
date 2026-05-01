-- 资源路径前缀
local ASSET_DIR = "../../../assets/texture/"

-- 新增：通过 Lua 脚本加载你用 Tiled 编辑的可视化地图组合
dofile("../../../scripts/map_loader.lua")
-- 动态加载瓦片，同时获取里面被美术划定的剧情包围盒坐标！
local map_ok, map_triggers = load_tiled_map("../../../assets/texture/complex_map.lua")

-- 缓存地图尺寸，用于无玩家时摄像机兜底聚焦
local map_data = dofile("../../../assets/texture/complex_map.lua")
local map_center_x = (map_data.width * map_data.tilewidth) / 2
local map_center_y = (map_data.height * map_data.tileheight) / 2

-- 引入最新剧情数据
local dialogue_data = dofile("../../../scripts/dialogue_data.lua")

-- ===============================================================
-- 播放背景音乐 (只需让引擎启动时播放一次，AudioSystem 会在后台持续刷新流)
-- 请确保这个文件在你的电脑上存在！(如果你改了名字，就在这里修改它)
play_bgm("../../../assets/audio/bgm.ogg")
-- ===============================================================

-- 遍历数据，自动创建所有实体
local player = nil
local npcs = {} -- 新增：保存所有可互动 NPC 列表
local progress = {} -- 记录每个 NPC 说到哪句话了

-- 全局 flag set：string -> true，引擎不感知任何具体业务语义
local flags = {}

-- 通用分支选择：返回第一个 when 条件全部满足的 branch
local function resolve_branch(branches)
    for _, b in ipairs(branches) do
        local ok = true
        for _, f in ipairs(b.when) do
            if not flags[f] then ok = false; break end
        end
        if ok then return b end
    end
end

-- ===============================================================
-- Tiled 驱动剧情时代：接收美术侧画下的对象区域数据！取代原有的硬坐标
-- ===============================================================
if map_triggers then
    for _, obj in ipairs(map_triggers) do
        
        if obj.type == "Player" then
            -- 装配主角实体
            player = create_entity()
            local tex = load_texture(ASSET_DIR .. "Player.png")
            set(player, "Transform", { x = obj.x, y = obj.y, layer = 2 })
            set(player, "Sprite", { texture_id = tex, width = obj.w, height = obj.h, src_x = 0, src_y = 0, src_w = 0, src_h = 0 })
            set(player, "Collider", { width = obj.w, height = obj.h })
            set(player, "Velocity", { x = 0, y = 0 })
            
        elseif obj.type == "Npc" then
            -- 装配可见的 NPC 实体
            local e = create_entity()
            
            -- Tiled 里面填入的名字 (比如 Guard_Albedo) 加上后缀自动映射
            local tex_name = obj.name
            if not tex_name:match("%.png$") then tex_name = tex_name .. ".png" end
            local tex = load_texture(ASSET_DIR .. tex_name)
            
            set(e, "Transform", { x = obj.x, y = obj.y, layer = 2 })
            set(e, "Sprite", { texture_id = tex, width = obj.w, height = obj.h, src_x = 0, src_y = 0, src_w = 0, src_h = 0 })
            -- [做减法]：不再给它挂载 Velocity 组件，这样它在物理系统里就像一座山一样，主角绝对推不动！
            set(e, "Collider", { width = obj.w, height = obj.h })
            
            table.insert(npcs, { 
                id = e, 
                name = tex_name, 
                w = obj.w,
                h = obj.h,
                cx = obj.x + (obj.w or 32) / 2,
                cy = obj.y + (obj.h or 32) / 2
            })
            
        else
            -- 装配隐形逻辑触发器 (Well, Bush, Chest)
            local trigger_e = create_entity()
            -- 将 Transform 往下挪半截。好处一：文字气泡不会再因为体积太高而飘到外太空；好处二：实现 2.5D 半透视碰撞（可以绕到物体后面半穿模）
            set(trigger_e, "Transform", { x = obj.x, y = obj.y + (obj.h or 48) / 2, layer = 1 })
            set(trigger_e, "Collider", { width = obj.w or 48, height = (obj.h or 48) / 2 })
            
            -- npc表新增 type 保存，作为查字典不到时的 fallback
            table.insert(npcs, { 
                id = trigger_e, 
                name = obj.name, 
                type = obj.type,
                w = obj.w,
                h = obj.h,
                cx = obj.x + (obj.w or 32) / 2,
                cy = obj.y + (obj.h or 32) / 2
            })
        end
    end
end

-- [临时测试] Tiled 对象层为空时，手动创建 Player
if not player then
    player = create_entity()
    local tex = load_texture(ASSET_DIR .. "Player.png")
    local nor = load_texture(ASSET_DIR .. "Player_n.png")
    set(player, "Transform", { x = map_center_x, y = map_center_y, layer = 2 })
    set(player, "Sprite", { texture_id = tex, normal_id = nor, width = 256, height = 256, src_x = 0, src_y = 0, src_w = 0, src_h = 0 })
    set(player, "Velocity", { x = 0, y = 0 })
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
    
    -- ===================
    -- 纯 Lua 的距离驱动的极简叙事系统
    -- ===================
    local space_is_down = is_key_down(32) -- 32 是空格键
    local space_pressed = space_is_down and not last_space_down
    last_space_down = space_is_down

    if space_pressed then
        local closest_dist = 100 * 100
        local target_npc = nil
        local target_valid_key = nil
        
        -- 核心逻辑进阶：先遍历一圈，找出离主角绝对距离【最近】的那个合法互动物体
        for _, npc in ipairs(npcs) do
            local nx_center = npc.cx
            local ny_center = npc.cy
            local px_center = px + 16
            local py_center = py + 16

            local dist2 = (nx_center - px_center) * (nx_center - px_center) + (ny_center - py_center) * (ny_center - py_center)
            
            if dist2 < closest_dist then
                
                local valid_key = nil
                local function find_key(key)
                    if not key then return nil end
                    local lk = key:lower()
                    for k, v in pairs(dialogue_data) do
                        local clk = k:lower()
                        if clk == lk or clk == lk .. ".png" then return k end
                    end
                    return nil
                end

                valid_key = find_key(npc.name)
                if not valid_key then valid_key = find_key(npc.type) end
                
                if valid_key then
                    -- 记录当前最近的候选人，收紧筛选半径
                    closest_dist = dist2
                    target_npc = npc
                    target_valid_key = valid_key
                end
            end
        end

        -- 锁定最近目标后，用通用引擎推演，不认识任何具体物件
        if target_valid_key and target_npc then
            local key = target_valid_key
            local npc = target_npc

            local branch = resolve_branch(dialogue_data[key])
            if branch then
                local bid = branch.id
                progress[key] = progress[key] or {}
                progress[key][bid] = progress[key][bid] or 1
                local cur_idx = progress[key][bid]

                if cur_idx <= #branch.lines then
                    set(npc.id, "TextBubble", { text = branch.lines[cur_idx], time = 3.0 })
                    -- 副作用：由数据声明，引擎统一触发
                    if branch.on_line and cur_idx == branch.on_line then
                        if branch.gives  then flags[branch.gives] = true end
                        if branch.effect then branch.effect(npc) end
                    end
                    progress[key][bid] = cur_idx + 1
                else
                    remove(npc.id, "TextBubble")
                    progress[key][bid] = 1
                end
            end
        end
    end
end
