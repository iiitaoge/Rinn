-- 找邻居：遍历所有 boid，距离 < radius 的就是邻居
function get_neighbors(me, boids, radius)
    local result = {}
    local mx, my = get_pos(me)
    for _, other in ipairs(boids) do
        if other ~= me then
            local ox, oy = get_pos(other)
            local dist = math.sqrt((mx - ox)^2 + (my - oy)^2)
            if dist < radius then
                result[#result + 1] = other
            end
        end
    end
    return result
end

-- 与邻居分离
function separation(me, neighbors)
    local fx, fy = 0, 0
    local mx, my = get_pos(me)
    for _, other in ipairs(neighbors) do
        local ox, oy = get_pos(other)
        local dx, dy = mx - ox, my - oy       -- 从邻居指向我的方向
        local dist = math.sqrt(dx*dx + dy*dy)
        if dist > 0 then
            fx = fx + dx / dist / dist         -- 除以 dist² = 方向归一化 + 越近越强
            fy = fy + dy / dist / dist
        end
    end
    return fx, fy
end


-- 跟队 朝邻居的平均方向飞
function alignment(me, neighbors)
    local fx, fy = 0, 0
    local n = #neighbors
    if n == 0 then return 0, 0 end
    for _, other in ipairs(neighbors) do
        local vx, vy = get_vel(other)  -- 需要新绑定
        fx = fx + vx
        fy = fy + vy
    end
    return fx / n, fy / n
end

-- 聚拢
function cohesion(me, neighbors)
    local fx, fy = 0, 0
    local n = #neighbors
    if n == 0 then return 0, 0 end
    for _, other in ipairs(neighbors) do
        local ox, oy = get_pos(other)
        fx = fx + ox
        fy = fy + oy
    end
    local mx, my = get_pos(me)
    return fx / n - mx, fy / n - my   -- 中心位置 - 我的位置 = 朝中心的方向
end