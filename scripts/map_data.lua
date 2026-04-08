-- 地图数据：每一行 = 一个实体
-- type: "player" / "npc" / "static"
-- texture: assets 目录下的贴图文件名
-- x, y: 初始位置
-- w, h: 尺寸
return {
    { type = "player", texture = "Player.png",       x = 400, y = 300, layer = 1, w = 128, h = 128 },
    { type = "npc",    texture = "Guard_Albedo.png",  x = 600, y = 400, layer = 1, w = 128, h = 128 },
    { type = "npc",    texture = "blacksmith.png",    x = 200, y = 500, layer = 1, w = 128, h = 128 },
    { type = "static", texture = "House.png",         x = 800, y = 200, layer = 0, w = 1024, h = 1024 },
}
