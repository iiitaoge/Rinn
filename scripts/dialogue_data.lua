-- 剧情数据：数据-驱动分支模型
-- 每个物件 → 优先级有序的分支数组，引擎从上到下找第一个 when 全满足的分支
-- 字段说明：
--   when    : string[] — 进入本分支所需的全部 flag（空表=无条件）
--   lines   : string[] — 对话行
--   on_line : int      — 在播放第几行时触发副作用（给 flag / 调 effect）
--   gives   : string   — on_line 触发时授予的 flag
--   effect  : fn(npc)  — on_line 触发时执行的任意 Lua 回调

return {
    ["Guard_Albedo.png"] = {
        { id = "passed",
          when    = { "guard_passed" },
          lines   = { "少废话，快进去，别让我改变主意！" } },

        { id = "with_pass",
          when    = { "has_pass" },
          lines   = { "嗯？这是... 通行证？！", "印章看起来没问题...", "算你走运。进去吧，别惹麻烦！" },
          on_line = 3, gives = "guard_passed" },

        { id = "default",
          when    = {},
          lines   = { "站住！前方的旧城区已被封锁。", "想过去？得有镇长签发的[通行证]！", "没证就滚远点，别在这碍眼！" } },
    },

    ["Blacksmith.png"] = {
        { id = "done",
          when    = { "has_pass" },
          lines   = { "还不赶紧滚？等卫兵发现证是假的，我们俩都得死！" } },

        { id = "with_hammer",
          when    = { "has_hammer" },
          lines   = { "老天！你真的从那个鬼地方把它拿上来了！", "这把锤子上可是沾着我祖父的血泪！", "给，这是你要的[通行证]！拿好别张扬！" },
          on_line = 3, gives = "has_pass" },

        { id = "waiting",
          when    = { "talked_blacksmith" },
          lines   = { "怎么样？那座【石像】是不是有什么古怪？" } },

        { id = "default",
          when    = {},
          lines   = { "想买通行证去旧城？不，我不做陌生人的生意。", "除非... 你能向我证明你的胆识。", "我不小心把打铁锤忘在了广场右侧的【石像】那里。", "那座神像最近透着古怪，你要是能把锤子拿回来，通行证好说。" },
          on_line = 4, gives = "talked_blacksmith" },
    },

    ["statue"] = {
        { id = "done",
          when    = { "has_hammer" },
          lines   = { "（石像静静地矗立着，冰冷的石头再也没有任何生息。）" } },

        { id = "with_berry",
          when    = { "has_berry" },
          lines   = { "（你将蓝浆果敬献在了石像的底座托盘上...）", "（一阵微风拂过，果实化为了光点消散，底座的暗格打开了）", "【石像】：你的虔诚得到了回应。这是上个无知的人遗落在此的东西，拿去吧。", "【获得关键道具：生锈的打铁锤】！" },
          on_line = 4, gives = "has_hammer" },

        { id = "waiting",
          when    = { "talked_statue" },
          lines   = { "【石像】：还在迟疑什么... 献上蓝浆果作为供奉..." } },

        { id = "default",
          when    = {},
          lines   = { "（一座岁月侵蚀的古老石像，隐约刻着未知的神祇...）", "【石像内传来低沉的回音】：献上供奉... 证明你的虔诚...", "【石像】：去广场左侧的灌木丛中，为我寻来一颗刚结出的[蓝浆果]...", "【石像】：凡人，完成试炼，你将得到你所求之物。" },
          on_line = 4, gives = "talked_statue" },
    },

    ["Bush"] = {
        { id = "empty",
          when    = { "has_berry" },
          lines   = { "（这片灌木丛里已经被你摘干净了，只剩几根枯枝。）" } },

        { id = "default",
          when    = {},
          lines   = { "（你跑到广场西侧，在浓密的灌木丛中仔细翻找...）", "（荆棘刺破了你的手背，但你发现了一颗饱满发亮的果实）", "【获得关键道具：蓝浆果】！" },
          on_line = 3, gives = "has_berry" },
    },

    ["Chest"] = {
        { id = "empty",
          when    = { "chest_opened" },
          lines   = { "（宝箱大敞着，里面已经空空如也，只有一点灰尘。）" } },

        { id = "default",
          when    = {},
          lines   = { "（这是一个古旧的宝箱，锁已经锈迹斑斑...）", "（你用力掰开了锁扣...）", "【获得物品：50 个生锈的银币】！" },
          on_line = 3, gives = "chest_opened" },
    },

    ["Door_closed.png"] = {
        { id = "opened",
          when    = { "door_opened" },
          lines   = { "（大门敞开着，木屑散落一地。）" } },

        { id = "default",
          when    = {},
          lines   = { "（你试着推了推门，门纹丝不动。）", "（伴随着你用力的一脚，门上的朽木断裂，门被粗暴地踹开了！）" },
          on_line = 2, gives = "door_opened",
          effect  = function(npc)
              remove(npc.id, "Collider")
              remove(npc.id, "Sprite")
              local new_tex = load_texture("../../../assets/texture/Door_open.png")
              set(npc.id, "Sprite", { texture_id = new_tex, width = npc.w, height = npc.h, src_x = 0, src_y = 0, src_w = 0, src_h = 0 })
          end },
    },
}
