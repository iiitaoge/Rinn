-- ============================================
-- Lua 精灵渲染测试
-- ============================================

print("=== Lua 精灵渲染测试 ===")

-- 1. 加载贴图资源
local tex_shop = load_texture("D:/cs/vs/Project_Rinn/assets/blacksmith_shop.png")
local tex_pub = load_texture("D:/cs/vs/Project_Rinn/assets/pub.png")
print("加载贴图: blacksmith_shop = " .. tex_shop .. ", pub = " .. tex_pub)

-- 2. 创建实体并添加组件
local e1 = create_entity()
emplace_Transform(e1, {x = 100, y = 100})
emplace_Sprite(e1, {texture_id = tex_shop, width = 128, height = 128})
print("创建实体 e1: blacksmith_shop @ (100, 100)")

local e2 = create_entity()
emplace_Transform(e2, {x = 400, y = 200})
emplace_Sprite(e2, {texture_id = tex_pub, width = 256, height = 256})
print("创建实体 e2: pub @ (400, 200)")

print("=== 初始化完成，等待渲染 ===")