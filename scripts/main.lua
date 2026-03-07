

print("Entity start")

local tex = load_texture("../../../assets/Guard_Albedo.png")
local Guard_Albedo = create_entity()

set(Guard_Albedo, "Transform", {x = 100, y = 200})
set(Guard_Albedo, "Velocity", {x = 0, y = 0})
set(Guard_Albedo, "Collider", {width = 128, height = 128})
set(Guard_Albedo, "Sprite", { texture_id = tex, width = 128, height = 128 })



local tex_blacksmith = load_texture("../../../assets/blacksmith.png")
local blacksmith = create_entity()
set(blacksmith, "Transform", {x = 100, y = 200})
set(blacksmith, "Velocity", {x = 0, y = 0})
set(blacksmith, "Collider", {width = 128, height = 128})
set(blacksmith, "Sprite", { texture_id = tex_blacksmith, width = 128, height = 128 })






-- 定义每帧逻辑（被 C++ 每帧调用）
function on_update()
    local dx, dy = 0, 0
    if is_key_down(87) then dy = -1 end
    if is_key_down(83) then dy =  1 end
    if is_key_down(65) then dx = -1 end
    if is_key_down(68) then dx =  1 end
    move(Guard_Albedo, dx, dy)
end
	



