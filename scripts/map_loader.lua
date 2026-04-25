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
        -- [做减法]：Tiled 里隐藏的图层直接跳过，不加载不渲染
        if layer.visible == false then
            goto continue_layer
        end
        
        if layer.type == "tilelayer" then
            for idx, gid in ipairs(layer.data) do
                -- [关键]：Tiled 会把翻转信息塞进 GID 的最高 4 位，必须先剥离才能匹配到正确图集
                -- 原始 GID 可能是 2147483706，低 28 位才是真实的图块 ID
                -- 用取模替代位运算 &，兼容 Lua 5.1/5.2/5.3/5.4 全版本
                gid = gid % (2^28)
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
                        
                        set(e, "Sprite", {
                            texture_id = ts_data.tex_id,
                            width = map.tilewidth,  
                            height = map.tileheight,
                            src_x = src_x,
                            src_y = src_y,
                            src_w = ts_data.tilewidth,
                            src_h = ts_data.tileheight,
                            is_ground = true
                        })
                    else
                        print("[WARN] GID=" .. gid .. " 找不到匹配的图集！请检查 tilesets 配置。")
                    end
                end
            end
        elseif layer.type == "objectgroup" then
            if layer.objects then
                for _, obj in ipairs(layer.objects) do
                    local obj_type = obj.type or ""
                    
                    if obj_type == "Wall" or obj_type == "Collision" then
                        -- [新] 对象层碰撞：纯物理实体，无 Sprite，只有 Transform + Collider
                        local wall = create_entity()
                        set(wall, "Transform", { x = obj.x, y = obj.y, layer = 0 })
                        set(wall, "Collider", {
                            width = obj.width,
                            height = obj.height,
                            layer = 0x0002,   -- 墙体碰撞层
                            mask  = 0x0001    -- 只和玩家碰撞
                        })
                    else
                        -- 原有逻辑：剧情触发器交给 main.lua 处理
                        local trigger_name = (obj.name and obj.name ~= "") and obj.name or obj_type
                        if trigger_name and trigger_name ~= "" then
                            table.insert(map_triggers, {
                                name = trigger_name,
                                type = obj_type,
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
        ::continue_layer::
    end

    print("Map loaded successfully!")
    -- 返回成功状态的同时，一并返回搜集到的 Tiled 触发器数据给 main
    return true, map_triggers
end
