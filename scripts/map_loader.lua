local ASSET_DIR = "../../../assets/texture/"

-- 解析并加载 Tiled 导出的 Lua 地图结构
function load_tiled_map(map_path)
    local map = dofile(map_path)
    if not map then 
        print("Failed to load map: " .. map_path)
        return false 
    end

    -- 第一步：解析地图中用到的所有图集 (Tilesets)
    -- Tiled 数据结构中给每个图集分配了一个起始 ID (firstgid)
    local gid_info = {}
    for _, ts in ipairs(map.tilesets) do
        -- Tiled 导出的路径可能含有乱七八糟的前缀（比如你的 ../asserts/）
        -- 这里使用正则仅提取文件名，重新拼接到我们可靠的 ASSET_DIR
        local filename = ts.image:match("([^/\\]+)$") or ts.image
        local full_path = ASSET_DIR .. filename
        local tex_id = load_texture(full_path)
        
        table.insert(gid_info, {
            firstgid = ts.firstgid,
            lastgid = ts.firstgid + ts.tilecount - 1,
            tex_id = tex_id,
            columns = ts.columns,
            tilewidth = ts.tilewidth,
            tileheight = ts.tileheight
        })
    end
    
    -- 第二步：逐层解析瓦片并转化为 ECS 实体
    local map_triggers = {} -- 专门收集 Tiled 里面的事件框
    
    for _, layer in ipairs(map.layers) do
        if layer.type == "tilelayer" then
            for idx, gid in ipairs(layer.data) do
                if gid > 0 then -- gid 为 0 时意味着空地，直接做减法跳过不渲染
                    
                    -- 匹配该 gid 属于哪个图集
                    local ts_data = nil
                    for _, ts in ipairs(gid_info) do
                        if gid >= ts.firstgid and gid <= ts.lastgid then
                            ts_data = ts
                            break
                        end
                    end
                    
                    if ts_data then
                        -- 计算从素材大图中切出的 uv
                        local local_id = gid - ts_data.firstgid
                        local col = local_id % ts_data.columns
                        local row = math.floor(local_id / ts_data.columns)
                        local src_x = col * ts_data.tilewidth
                        local src_y = row * ts_data.tileheight
                        
                        -- 计算在游戏世界里的像素坐标
                        local tile_x = (idx - 1) % layer.width
                        local tile_y = math.floor((idx - 1) / layer.width)
                        local world_x = tile_x * map.tilewidth
                        local world_y = tile_y * map.tileheight
                        
                        -- 创建瓦片实体
                        local e = create_entity()
                        set(e, "Transform", { 
                            x = world_x, 
                            y = world_y, 
                            layer = layer.id or 0 
                        })
                        
                        -- 第二层 (层级 2) 默认全是景物等障碍物，霸道地给它们全部注入实心刚体基因！
                        if layer.id == 2 then
                            set(e, "Collider", { width = map.tilewidth, height = map.tileheight })
                        end
                        
                        set(e, "Sprite", {
                            texture_id = ts_data.tex_id,
                            width = map.tilewidth,  
                            height = map.tileheight,
                            src_x = src_x,
                            src_y = src_y,
                            src_w = ts_data.tilewidth,
                            src_h = ts_data.tileheight
                        })
                    end
                end
            end
        elseif layer.type == "objectgroup" then
            -- Tiled 对象层：读取配置好的隐藏逻辑触发器
            if layer.objects then
                for _, obj in ipairs(layer.objects) do
                    -- 美术在 Tiled 里可能习惯填 Name，也可能习惯填 Type(Class)
                    local trigger_name = (obj.name and obj.name ~= "") and obj.name or obj.type
                    if trigger_name and trigger_name ~= "" then
                        table.insert(map_triggers, {
                            name = trigger_name,
                            type = obj.type,
                            x = obj.x,
                            y = obj.y,
                            w = obj.width,
                            h = obj.height
                        })
                    end
                end
            end
        end
    end

    print("Map loaded successfully!")
    -- 返回成功状态的同时，一并返回搜集到的 Tiled 触发器数据给 main
    return true, map_triggers
end
