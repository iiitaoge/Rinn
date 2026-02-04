-- ============================================
-- Project Rinn 意图驱动 API 测试
-- ============================================
-- 原则：
--   Lua 声明意图，C++ 执行机制
--   Lua 不知道 Velocity 组件的存在
-- ============================================

print("=== Lua 意图 API 测试 ===")

-- ============================================
-- 1. 加载资源
-- ============================================
local tex_id = load_texture("D:/cs/vs/Project_Rinn/assets/blacksmith_shop.png")
print("加载贴图 ID: " .. tex_id)

-- ============================================
-- 2. 通过 Prefab 创建实体
-- ============================================
-- spawn() 内部由 C++ 挂载所有组件
-- Lua 不需要知道有哪些组件
local player = spawn("player", 400, 300)
print("创建玩家实体 @ (400, 300)")

-- 设置精灵纹理（这是安全的，不破坏物理）
set_sprite_texture(player, tex_id)

-- ============================================
-- 3. 定义移动速度
-- ============================================
local SPEED = 200  -- 像素/秒

-- ============================================
-- 4. 每帧更新函数
-- ============================================
-- 职责：
--   - 检测输入（轮询，因为需要持续按住状态）
--   - 声明移动意图（move/stop）
--   - C++ PhysicsSystem 执行实际位移
-- ============================================

function on_update(dt)
    -- 检测方向输入
    local moving = false
    
    if input.is_key_down(KEY.W) or input.is_key_down(KEY.UP) then
        move(player, "up", SPEED)
        moving = true
    end
    if input.is_key_down(KEY.S) or input.is_key_down(KEY.DOWN) then
        move(player, "down", SPEED)
        moving = true
    end
    if input.is_key_down(KEY.A) or input.is_key_down(KEY.LEFT) then
        move(player, "left", SPEED)
        moving = true
    end
    if input.is_key_down(KEY.D) or input.is_key_down(KEY.RIGHT) then
        move(player, "right", SPEED)
        moving = true
    end
    
    -- 没有按键时停止
    if not moving then
        stop(player)
    end

    -- 空格键：查询并打印位置
    if input.is_key_pressed(KEY.SPACE) then
        local pos = query_position(player)
        print("当前位置: (" .. pos.x .. ", " .. pos.y .. ")")
    end
    
    -- 鼠标点击测试
    if input.is_mouse_pressed(MOUSE.LEFT) then
        local mx = input.get_mouse_x()
        local my = input.get_mouse_y()
        print("鼠标点击 @ (" .. mx .. ", " .. my .. ")")
    end
end

print("=== 初始化完成 ===")
print("操作说明:")
print("  WASD / 方向键 - 移动")
print("  SPACE - 打印位置")
print("  鼠标左键 - 点击测试")