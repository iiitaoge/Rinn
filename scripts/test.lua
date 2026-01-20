

-- 测试组件操作
local entities = {}

for i = 1,10 do
	entities[i] = create_entity()
	emplace_Transform(entities[i], i * 50, i * 50 + 50)
end
