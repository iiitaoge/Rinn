

-- 测试组件操作
local e = create_entity()
print("创建实体:", e:index())
-- 测试 emplace
emplace_Transform(e, 100, 200)
emplace_Velocity(e, 1.5, -0.5)
print("组件添加成功！")
print("实体存活:", is_alive(e))
-- 清理
destroy_entity(e)
print("测试完成")