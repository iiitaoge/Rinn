-- 资源路径前缀
local ASSET_DIR = "../../../assets/"

-- 加载地图数据
local map = dofile("../../../scripts/map_data.lua")

-- 遍历数据，自动创建所有实体
local player = nil

for _, entry in ipairs(map) do
    local tex = load_texture(ASSET_DIR .. entry.texture)
    local e = create_entity()
    set(e, "Transform", { x = entry.x, y = entry.y, layer = entry.layer})
    set(e, "Sprite", { texture_id = tex, width = entry.w, height = entry.h })

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
end
