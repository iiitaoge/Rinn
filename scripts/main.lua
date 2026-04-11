-- 资源路径前缀
local ASSET_DIR = "../../../assets/texture/"

-- 新增：通过 Lua 脚本加载你用 Tiled 编辑的可视化地图组合
dofile("../../../scripts/map_loader.lua")
-- 动态加载瓦片，同时获取里面被美术划定的剧情包围盒坐标！
local _, map_triggers = load_tiled_map("../../../assets/texture/simple_map.lua")

-- 引入最新剧情数据
local dialogue_data = dofile("../../../scripts/dialogue_data.lua")

-- 遍历数据，自动创建所有实体
local player = nil
local npcs = {} -- 新增：保存所有可互动 NPC 列表
local progress = {} -- 记录每个 NPC 说到哪句话了

-- 叙事的绝对主角：“全局微状态机”
local global_state = {
    has_berry = false,
    has_hammer = false,
    has_pass = false,
    guard_passed = false,
    chest_opened = false
}

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
                cx = obj.x + (obj.w or 32) / 2,
                cy = obj.y + (obj.h or 32) / 2
            })
        end
    end
end
-- ===============================================================

-- 渲染模式切换：按 TAB 键可以在 纯2D 和 HD-2D 之间无缝切换
local is_hd2d = false
set_hd2d_mode(is_hd2d)

-- 每帧逻辑
function on_update()
    if not player then return end

    -- 热切换 2D / 3D
    if is_key_down(258) then -- 258 是 Raylib 的 TAB 键键码
        is_hd2d = not is_hd2d
        set_hd2d_mode(is_hd2d)
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

        -- 如果锁定了最近的目标，才进行状态机推演
        if target_valid_key and target_npc then
            local valid_key = target_valid_key
            local npc = target_npc
            local current_branch = "default"
            
            if valid_key == "Guard_Albedo.png" then
                if global_state.guard_passed then current_branch = "passed"
                elseif global_state.has_pass then current_branch = "with_pass"
                else current_branch = "default" end
            elseif valid_key == "blacksmith.png" then
                if global_state.has_pass then current_branch = "done"
                elseif global_state.has_hammer then current_branch = "with_hammer"
                elseif progress[valid_key] and progress[valid_key]["default"] == 5 then
                    current_branch = "waiting"
                else current_branch = "default" end
            elseif valid_key == "statue" then
                if global_state.has_hammer then current_branch = "done"
                elseif global_state.has_berry then current_branch = "with_berry"
                elseif progress[valid_key] and progress[valid_key]["default"] == 5 then
                    current_branch = "waiting"
                else current_branch = "default" end
            elseif valid_key == "Bush" then
                if global_state.has_berry or global_state.has_hammer then current_branch = "empty"
                else current_branch = "default" end
            elseif valid_key == "Chest" then
                if global_state.chest_opened then current_branch = "empty"
                else current_branch = "default" end
            end

            progress[valid_key] = progress[valid_key] or {}
            progress[valid_key][current_branch] = progress[valid_key][current_branch] or 1
            local cur_idx = progress[valid_key][current_branch]
            
            local lines = dialogue_data[valid_key][current_branch]
            
            if lines and cur_idx <= #lines then
                set(npc.id, "TextBubble", { text = lines[cur_idx], time = 3.0 })
                
                if valid_key == "Chest" and current_branch == "default" and cur_idx == 3 then
                    global_state.chest_opened = true
                end
                if valid_key == "Bush" and current_branch == "default" and cur_idx == 3 then
                    global_state.has_berry = true
                end
                if valid_key == "statue" and current_branch == "with_berry" and cur_idx == 4 then
                    global_state.has_hammer = true
                end
                if valid_key == "blacksmith.png" and current_branch == "with_hammer" and cur_idx == 3 then
                    global_state.has_pass = true
                end
                if valid_key == "Guard_Albedo.png" and current_branch == "with_pass" and cur_idx == 3 then
                    global_state.guard_passed = true
                end

                progress[valid_key][current_branch] = cur_idx + 1
            else
                remove(npc.id, "TextBubble")
                progress[valid_key][current_branch] = 1
                
                if valid_key == "blacksmith.png" and current_branch == "default" then
                     progress[valid_key]["default"] = 5
                end
                if valid_key == "statue" and current_branch == "default" then
                     progress[valid_key]["default"] = 5
                end
            end
        end
    end
end
