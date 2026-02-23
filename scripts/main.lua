

print("Entity start")
local player = create_entity()

set(player, "Transform", {x = 100, y = 200})
set(player, "Velocity", {x = 0, y = 0})
set(player, "Collider", {width = 50, height = 50})



-- 定义每帧逻辑（被 C++ 每帧调用）
function on_update()
    local dx, dy = 0, 0
    if is_key_down(87) then dy = -1 end
    if is_key_down(83) then dy =  1 end
    if is_key_down(65) then dx = -1 end
    if is_key_down(68) then dx =  1 end
    move(player, dx, dy)
end
	



