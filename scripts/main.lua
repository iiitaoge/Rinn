-- 资源路径前缀
local ASSET_DIR = "../../../assets/texture/"

-- 加载地图数据
local texture_data = dofile("../../../scripts/texture_data.lua")

-- 遍历数据，自动创建所有实体
local player = nil

for _, entry in ipairs(texture_data) do
    local tex = load_texture(ASSET_DIR .. entry.texture)
    local e = create_entity()
    set(e, "Transform", { x = entry.x, y = entry.y, layer = entry.layer})
    set(e, "Sprite", { 
        texture_id = tex, 
        width = entry.w, height = entry.h, 
    
        -- 如果有配过截取区域就用，如果没配（nil），意味着想要使用一整张图：
        -- 起点坐标默认为 0，截取的尺寸默认为贴图的全宽和全高。
        src_x = entry.src_x or 0, 
        src_y = entry.src_y or 0, 
        src_w = entry.src_w or 0,  -- 传 0 给 C++，让 C++ 自己去拿 tex.width
        src_h = entry.src_h or 0
    })


    if entry.type == "player" then
        set(e, "Velocity", { x = 0, y = 0 })
        set(e, "Collider", { width = entry.w, height = entry.h })
        player = e
    elseif entry.type == "npc" then
        set(e, "Velocity", { x = 0, y = 0 })
        set(e, "Collider", { width = entry.w, height = entry.h })
    end
    -- static 类型不需要 Velocity
end

-- 每帧逻辑
function on_update()
    if not player then return end
    local dx, dy = 0, 0
    if is_key_down(87) then dy = -1 end
    if is_key_down(83) then dy =  1 end
    if is_key_down(65) then dx = -1 end
    if is_key_down(68) then dx =  1 end
    move(player, dx, dy)

    -- HD-2D 新增：物理逻辑跑完之后，在只拥有 {x,y} 数据的状态下，通知 C++ 的 3D 摄像机聚焦到此
    local px, py = get_pos(player)
    set_camera_target(px, py)
end
