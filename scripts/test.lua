-- ============================================
-- Lua 绑定功能测试脚本
-- ============================================

print("========== Lua 脚本测试开始 ==========")

-- 1. 测试实体创建
print("\n[Test 1] 创建实体...")
local entities = {}
for i = 1, 5 do
    entities[i] = create_entity()
    print("  创建实体 #" .. i .. ", index = " .. entities[i]:index() .. ", generation = " .. entities[i]:generation())
end

-- 2. 测试组件添加 (使用 table 语法)
print("\n[Test 2] 添加 Transform 组件...")
for i = 1, 5 do
    emplace_Transform(entities[i], {x = i * 100.0, y = i * 50.0})
    print("  实体 #" .. i .. " 添加 Transform: x=" .. (i * 100) .. ", y=" .. (i * 50))
end

-- 3. 测试组件添加 - Velocity
print("\n[Test 3] 添加 Velocity 组件...")
for i = 1, 5 do
    emplace_Velocity(entities[i], {vx = i * 2.0, vy = i * 1.0})
    print("  实体 #" .. i .. " 添加 Velocity: vx=" .. (i * 2) .. ", vy=" .. i)
end

-- 4. 测试 has 检查
print("\n[Test 4] 检查组件存在性...")
for i = 1, 5 do
    local hasT = has_Transform(entities[i])
    local hasV = has_Velocity(entities[i])
    print("  实体 #" .. i .. ": has_Transform=" .. tostring(hasT) .. ", has_Velocity=" .. tostring(hasV))
end

-- 5. 测试 get 获取组件
print("\n[Test 5] 获取组件数据...")
for i = 1, 3 do
    local t = get_Transform(entities[i])
    local v = get_Velocity(entities[i])
    print("  实体 #" .. i .. ": Transform(" .. t.x .. ", " .. t.y .. "), Velocity(" .. v.vx .. ", " .. v.vy .. ")")
end

-- 6. 测试 remove 删除组件
print("\n[Test 6] 删除组件测试...")
remove_Velocity(entities[1])
print("  已删除实体 #1 的 Velocity")
print("  实体 #1 has_Velocity = " .. tostring(has_Velocity(entities[1])))
print("  实体 #2 has_Velocity = " .. tostring(has_Velocity(entities[2])))

-- 7. 测试实体销毁
print("\n[Test 7] 销毁实体测试...")
destroy_entity(entities[5])
print("  已销毁实体 #5")
print("  实体 #5 is_alive = " .. tostring(is_alive(entities[5])))
print("  实体 #4 is_alive = " .. tostring(is_alive(entities[4])))

print("\n========== Lua 脚本测试完成 ==========\n")