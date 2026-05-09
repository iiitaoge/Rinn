local ASSET_DIR = "../../../assets/texture/"

local function basename(path)
    return path and (path:match("([^/\\]+)$") or path) or nil
end

local function push_gid_info(gid_info, entry)
    if entry and entry.firstgid then
        gid_info[#gid_info + 1] = entry
    end
end

-- 解析并加载 Tiled 导出的 Lua 地图结构
function load_tiled_map(map_path)
    local map = dofile(map_path)
    if not map then 
        print("Failed to load map: " .. map_path)
        return false 
    end

    -- 第一步：解析地图中用到的 tileset。
    -- new_map.lua 使用 image collection：tileset 自己没有 image，每个 tile 才有 image。
    local gid_info = {}
    for _, ts in ipairs(map.tilesets) do
        if ts.tiles and #ts.tiles > 0 then
            for _, tile in ipairs(ts.tiles) do
                local filename = basename(tile.image)
                local tex_id = filename and load_texture(ASSET_DIR .. filename) or 0
                push_gid_info(gid_info, {
                    firstgid = ts.firstgid + tile.id,
                    lastgid = ts.firstgid + tile.id,
                    tex_id = tex_id,
                    filename = filename,
                    tilewidth = tile.width or ts.tilewidth or map.tilewidth,
                    tileheight = tile.height or ts.tileheight or map.tileheight,
                    src_x = 0,
                    src_y = 0,
                    src_w = tile.width or ts.tilewidth or map.tilewidth,
                    src_h = tile.height or ts.tileheight or map.tileheight,
                    image_collection = true
                })
            end
        elseif ts.image then
            -- 兼容旧地图：整张图集 + columns。
            local filename = basename(ts.image)
            local full_path = ASSET_DIR .. filename
            local tex_id = load_texture(full_path)

            push_gid_info(gid_info, {
                firstgid = ts.firstgid,
                lastgid = ts.firstgid + ts.tilecount - 1,
                tex_id = tex_id,
                filename = filename,
                columns = ts.columns,
                tilewidth = ts.tilewidth,
                tileheight = ts.tileheight,
                image_collection = false
            })
        end
    end

    local function find_gid(gid)
        gid = gid % (2^28)
        for _, ts in ipairs(gid_info) do
            if gid >= ts.firstgid and gid <= ts.lastgid then
                return ts, gid
            end
        end
        return nil, gid
    end
    
    -- 第二步：逐层解析瓦片并转化为 ECS 实体
    local map_objects = {} -- 收集 Tiled 对象层，交给 main.lua 装配逻辑实体
    local occupied_ground_cells = {}

    local function ground_cell_key(tile_x, tile_y)
        return tostring(tile_x) .. ":" .. tostring(tile_y)
    end

    local function create_ground_tile(tex_id, world_x, world_y, width, height, src_x, src_y, src_w, src_h, layer_id)
        local e = create_entity()
        set(e, "Transform", {
            x = world_x,
            y = world_y,
            layer = layer_id or 0
        })
        set(e, "Sprite", {
            texture_id = tex_id,
            width = width,
            height = height,
            src_x = src_x,
            src_y = src_y,
            src_w = src_w,
            src_h = src_h,
            is_ground = true
        })
    end
    
    for _, layer in ipairs(map.layers) do
        -- [做减法]：Tiled 里隐藏的图层直接跳过，不加载不渲染
        if layer.visible == false then
            goto continue_layer
        end
        
        if layer.type == "tilelayer" then
            for idx, gid in ipairs(layer.data) do
                gid = gid % (2^28)
                if gid > 0 then -- gid 为 0 时意味着空地，直接做减法跳过不渲染
                    local ts_data = find_gid(gid)
                    
                    if ts_data then
                        local src_x = ts_data.src_x or 0
                        local src_y = ts_data.src_y or 0
                        local src_w = ts_data.src_w or ts_data.tilewidth
                        local src_h = ts_data.src_h or ts_data.tileheight

                        if not ts_data.image_collection then
                            local local_id = gid - ts_data.firstgid
                            local col = local_id % ts_data.columns
                            local row = math.floor(local_id / ts_data.columns)
                            src_x = col * ts_data.tilewidth
                            src_y = row * ts_data.tileheight
                            src_w = ts_data.tilewidth
                            src_h = ts_data.tileheight
                        end
                        
                        -- 计算在游戏世界里的像素坐标
                        local tile_x = (idx - 1) % layer.width
                        local tile_y = math.floor((idx - 1) / layer.width)
                        local world_x = tile_x * map.tilewidth
                        local world_y = tile_y * map.tileheight
                        
                        if ts_data.image_collection then
                            -- 大图块允许在 Tiled 里重叠摆放；这里切回地图格，避免共面重叠导致 z-fighting 波纹。
                            local cols = math.ceil(ts_data.tilewidth / map.tilewidth)
                            local rows = math.ceil(ts_data.tileheight / map.tileheight)
                            for oy = 0, rows - 1 do
                                for ox = 0, cols - 1 do
                                    local cell_x = tile_x + ox
                                    local cell_y = tile_y + oy
                                    if cell_x >= 0 and cell_x < map.width and cell_y >= 0 and cell_y < map.height then
                                        local key = ground_cell_key(cell_x, cell_y)
                                        if not occupied_ground_cells[key] then
                                            occupied_ground_cells[key] = true
                                            local chunk_w = math.min(map.tilewidth, ts_data.tilewidth - ox * map.tilewidth)
                                            local chunk_h = math.min(map.tileheight, ts_data.tileheight - oy * map.tileheight)
                                            create_ground_tile(
                                                ts_data.tex_id,
                                                world_x + ox * map.tilewidth,
                                                world_y + oy * map.tileheight,
                                                chunk_w,
                                                chunk_h,
                                                src_x + ox * map.tilewidth,
                                                src_y + oy * map.tileheight,
                                                chunk_w,
                                                chunk_h,
                                                layer.id
                                            )
                                        end
                                    end
                                end
                            end
                        else
                            create_ground_tile(
                                ts_data.tex_id,
                                world_x,
                                world_y,
                                map.tilewidth,
                                map.tileheight,
                                src_x,
                                src_y,
                                src_w,
                                src_h,
                                layer.id
                            )
                        end
                    else
                        print("[WARN] GID=" .. gid .. " 找不到匹配的图集！请检查 tilesets 配置。")
                    end
                end
            end
        elseif layer.type == "objectgroup" then
            if layer.objects then
                for _, obj in ipairs(layer.objects) do
                    local obj_type = obj.type or ""
                    local tile_info = nil
                    if obj.gid and obj.gid > 0 then
                        tile_info = find_gid(obj.gid)
                    end
                    
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
                            table.insert(map_objects, {
                                name = trigger_name,
                                type = obj_type,
                                x = obj.x,
                                y = obj.y,
                                w = obj.width,
                                h = obj.height,
                                gid = obj.gid or 0,
                                texture = tile_info and tile_info.filename or nil
                            })
                        end
                    end
                end
            end
        end
        ::continue_layer::
    end

    print("Map loaded successfully!")
    -- 返回成功状态的同时，一并返回搜集到的 Tiled 对象数据给 main
    return true, map_objects
end
