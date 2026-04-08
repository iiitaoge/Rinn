-- 地图数据：每一行 = 一个实体
-- type: "player" / "npc" / "static"
-- texture: assets 目录下的贴图文件名
-- x, y: 初始位置
-- w, h: 尺寸


-- 直接作为局部遍历引入 Python 切出来的数据字典
local tx_props = dofile("../../../assets/texture/TX Props_data.lua")
return {
    -- 正常的实体：没有传 src_x 意味着画整张图
    { type = "player", texture = "Player.png",       x = 400, y = 300, layer = 1, w = 128, h = 128 },
    { type = "npc",    texture = "Guard_Albedo.png",  x = 600, y = 400, layer = 1, w = 128, h = 128 },
    { type = "npc",    texture = "blacksmith.png",    x = 200, y = 500, layer = 1, w = 128, h = 128 },
    
    -- 使用图集中第 59 号纹理的实体：直接读表里对应的坐标！
    { type = "static", texture = "TX Props.png", 
      x = 800, y = 200, layer = 0, 
      w = tx_props[59].w, h = tx_props[59].h, 
      src_x = tx_props[59].x, src_y = tx_props[59].y, src_w = tx_props[59].w, src_h = tx_props[59].h }
}

