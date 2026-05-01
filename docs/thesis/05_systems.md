# 第 5 章 系统模块的实现

第 4 章介绍的 ECS 核心提供了一套与具体游戏功能无关的"语法基底"。要让引擎真正"动起来"，还需在其之上构建一组系统（System）模块来分别承担渲染、物理、碰撞、输入、音频与调试等职责。本章对 Project Rinn 当前实现的六个子系统逐一展开。每个子系统都遵循相同的接口约定：以 `Registry&`（必要时加 `ResourceManager&`、`float dt` 等）为输入，对其中匹配组件组合的实体执行一次确定性的更新或绘制。系统之间不互相直接依赖，仅通过 `Registry` 中的组件数据隐式通信，这正是 ECS"行为归系统、数据归组件"原则的体现。

## 5.1 RenderSystem：三维场景下的精灵渲染

`RenderSystem` 是引擎中体量最大、技术涉及面最广的子系统。它在 raylib 的 `Camera3D` 之上，把 ECS 中的 `Transform` 与 `Sprite` 组件转化为屏幕上的可见像素，并负责中文字体加载、对话气泡、shader 绑定等附属职责。

### 5.1.1 命名空间式的状态承载

不同于将子系统封装为类的常见做法，`RenderSystem` 采取了"命名空间 + inline 全局"的实现：

```cpp
namespace Rinn::RenderSystem {
    inline Font chinese_font = {};
    inline Camera3D camera = { 0 };
    inline Shader test_shader = { 0 };
    constexpr float WORLD_SCALE = 0.01f;
    // ...
}
```

`inline` 关键字使得这些全局变量在多翻译单元（TU）之间唯一存在，避免链接冲突。这一选择的优点是 API 极薄、调用代码无需传递任何额外上下文（如 `RenderSystem::DrawText(...)` 直接可用）；其代价是不能在同一进程内持有第二个相机或第二张 shader——这一约束在毕业设计阶段尚可接受，未来若要支持多场景渲染或 RenderTexture，需要将其重构为类。

### 5.1.2 初始化：相机、字体与 shader

`Init` 函数在引擎启动时一次性完成所有渲染相关资源的加载：

```cpp
inline void Init(int width, int height, const char* title) {
    InitWindow(width, height, title);
    SetTargetFPS(144);
    camera.position = { 0.0f, 10.0f, 6.0f };
    camera.target   = { 0.0f, 0.0f, 0.0f };
    camera.up       = { 0.0f, 1.0f, 0.0f };
    camera.fovy     = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;
    // 加载字体与 shader……
}
```

相机配置为 60° 俯视的透视投影，目标点初始位于原点；后续每帧由 `UpdateCamera(target_x, target_z)` 接受脚本传来的玩家位置，让相机平滑跟随。`WORLD_SCALE = 0.01f` 表示"100 像素 = 1 米"，是脚本侧像素坐标与渲染侧米制坐标之间的统一换算系数。

中文字体加载是 raylib 在中文场景下需要特别处理的一环。引擎构造一个包含 ASCII、CJK 统一汉字（U+4E00–U+9FFF）、CJK 标点、全角符号、常用排版符号的 `codepoints` 数组，传入 `LoadFontEx` 一次性烘焙到字形图集。这种"全量加载"在简化业务的同时引入了较大的 GPU 内存占用，未来可优化为按需加载。

### 5.1.3 帧管理三段式

引擎将每帧绘制划分为三个阶段：

```cpp
inline void BeginFrame(Color clear) { BeginDrawing(); ClearBackground(clear); BeginMode3D(camera); }
inline void EndCameraMode()         { EndMode3D(); }
inline void EndFrame()              { EndDrawing(); }
```

- `BeginFrame` 进入绘制状态并启用 3D 模式；
- 在 `EndCameraMode` 之前提交的所有绘制都受 3D 相机变换；
- `EndCameraMode` 之后可以使用屏幕坐标绘制 UI（如对话气泡、FPS 文本），最后由 `EndFrame` 提交到 GPU。

这种显式分段使得渲染流程的"3D 世界"与"2D 覆盖层"职责泾渭分明，且与第 3 章主循环的执行顺序严格对应。

### 5.1.4 DrawSprite3D：四边形与法线的两种摆放

精灵的 3D 化绘制由 `DrawSprite3D` 负责。它接收纹理、法线贴图、源矩形（UV）、世界位置、尺寸与一个 `bool ground` 标志：

```cpp
inline void DrawSprite3D(Texture2D tex, Texture2D nor, Rectangle src,
                          Vector3 pos, Vector2 size, bool ground) {
    rlSetTexture(tex.id);
    int loc = GetShaderLocation(test_shader, "sprite_normal");
    SetShaderValueTexture(test_shader, loc, nor);
    rlBegin(RL_QUADS);
    rlColor4ub(255, 255, 255, 255);
    // ……根据 ground 计算四个顶点
}
```

当 `ground = true` 时，四个顶点位于同一 Y 坐标的水平面（XZ 平面），表示地面瓦片；当 `ground = false` 时，顶点在 XY 立面上排列，且 X 方向以 `pos.x` 为中心居中，表示立式精灵（角色、树木等）。两种摆放共用同一套 `(u1, v1, u2, v2)` 纹理坐标计算，但顶点提交顺序略有差异——这一顺序直接影响法线方向，是后续光照能够"正面亮、背面暗"的几何基础。

### 5.1.5 DrawSprites：两趟渲染与排序

`DrawSprites(reg, res)` 是 RenderSystem 的对外主入口。它分两趟处理 `Transform + Sprite` 的实体：

1. **Pass 1：地面瓦片**。从 ECS 中筛选 `is_ground = true` 的实体，统一将 shader 的 `facenormal` 设为 `(0, 1, 0)`（朝上），不做排序，逐个 `DrawSprite3D(..., true)`。
2. **Pass 2：立式精灵**。剩余实体按 `(layer, y_bottom)` 升序排序——`y_bottom = transform.y + sprite.height`，将屏幕下方的精灵排在后面绘制，符合"近处遮远处"的视觉直觉。设 `facenormal = (0, 0, 1)`（朝向相机），逐个 `DrawSprite3D(..., false)`。

排序的实现使用 `std::sort` 与按 `Registry` 反查的 lambda：

```cpp
std::sort(sprites.begin(), sprites.end(),
    [&reg](Entity a, Entity b) {
        const auto& ta = reg.get<Transform>(a);
        const auto& tb = reg.get<Transform>(b);
        const auto& sa = reg.get<Sprite>(a);
        const auto& sb = reg.get<Sprite>(b);
        if (ta.layer != tb.layer) return ta.layer < tb.layer;
        return (ta.y + sa.height) < (tb.y + sb.height);
    });
```

每次比较都涉及 4 次 `Registry::get`，每次 get 需要一次 sparse 寻址 + 一次 dense 访问。当精灵规模到达千级时，比较成本不可忽略。第 9 章的不足之处中讨论了将"排序键预投影为 `uint64_t`"的优化方向；本章保持当前实现以突出基础流程的清晰。

### 5.1.6 屏幕空间 UI：对话气泡

`DrawTextBubbles(reg)` 在 `EndCameraMode` 之后调用，对所有持有 `Transform + TextBubble` 的实体执行：

```cpp
Vector3 pos3D     = { t.x * WORLD_SCALE, 0.0f, t.y * WORLD_SCALE };
Vector2 screen_pos = GetWorldToScreen(pos3D, camera);
int text_width   = MeasureTextEx(chinese_font, tb.text, 24, 2).x;
int draw_x       = screen_pos.x - text_width / 2;
int draw_y       = screen_pos.y - 70;
DrawRectangle(...);
DrawRectangleLines(...);
DrawTextCN(tb.text, draw_x, draw_y, 24, BLACK);
```

通过 `GetWorldToScreen` 将 3D 世界位置投影到屏幕空间，再以中心对齐的方式绘制黑边白底的圆角矩形与中文文本，使对话气泡稳定悬浮于角色头顶。

> 代码引用：`src/Systems/RenderSystem.hpp`

## 5.2 PhysicSystem：纯位置积分

`PhysicSystem` 是引擎中代码量最少、职责最纯粹的子系统：

```cpp
inline void update(Registry& reg, float dt) {
    for (Entity e : reg.view<Transform, Velocity>()) {
        auto& t = reg.get<Transform>(e);
        auto& v = reg.get<Velocity>(e);
        t.x += v.vx * dt;
        t.y += v.vy * dt;
    }
}
```

设计上做了两点显式取舍：第一，**仅做位置积分，不做边界裁剪与碰撞响应**——边界由 Lua 端按场景需要决定，碰撞响应由 `CollisionSystem::resolve` 承担；第二，**不做任何加速度、阻力或弹簧约束**。这种"职责单一到极致"的写法是面向数据设计的典型实践：System 越纯粹，越容易被并行化、被复用、被替换。如果未来要引入更复杂的物理模型（例如基于约束求解器的关节系统），可以新增 `ConstraintSystem` 而保持 `PhysicSystem` 不变。

> 代码引用：`src/Systems/PhysicSystem.hpp`

## 5.3 CollisionSystem：空间哈希加 AABB 的二阶段检测

碰撞检测是开发负担最容易爆炸的子系统之一。Project Rinn 选用了"空间哈希（broad phase）+ 轴对齐包围盒（narrow phase）+ 最小穿透向量解算"这套行业惯用方案，在保持代码量可控的同时支持百级以上的动态实体。

### 5.3.1 空间哈希作为宽相

宽相的目标是快速排除"显然不可能相撞"的实体对。引擎以 64 像素为单元（约为两个瓦片宽，覆盖玩家最多跨越的 2×2 格），将世界划分为虚拟网格，并以 64 位整数 `cell_key(cx, cy) = (uint32_t(cx) << 32) | uint32_t(cy)` 作为哈希键，用 `std::unordered_map<uint64_t, std::vector<Entity>>` 存储每格中的实体列表。每帧 `detect` 开始时清空网格，再遍历所有 `Transform + Collider` 实体，将其插入到所覆盖的全部格子中。

### 5.3.2 AABB 窄相

窄相由 `overlaps(ta, ca, tb, cb)` 函数承担，对两实体的轴对齐包围盒做经典的"两轴分离测试"：

```cpp
inline bool overlaps(const Transform& ta, const Collider& ca,
                      const Transform& tb, const Collider& cb) {
    float ax = ta.x + ca.offset_x, ay = ta.y + ca.offset_y;
    float bx = tb.x + cb.offset_x, by = tb.y + cb.offset_y;
    return ax < bx + cb.width  && ax + ca.width  > bx
        && ay < by + cb.height && ay + ca.height > by;
}
```

`Collider` 组件除了 `width / height` 外还包含 `offset_x / offset_y`（包围盒中心相对 Transform 的偏移）以及 `layer / mask` 两个 16 位掩码用于碰撞过滤，使得同一类碰撞器可以根据所属阵营（玩家、敌人、墙体、触发器）选择性地与其他类碰撞。

### 5.3.3 detect：二阶段串联

```cpp
inline std::vector<Hit> detect(Registry& reg) {
    grid.clear();
    for (Entity e : reg.view<Transform, Collider>()) insert_to_grid(...);

    std::vector<Hit> hits;
    for (Entity a : reg.view<Transform, Collider, Velocity>()) {
        // 计算 a 覆盖格子 + 邻格 [x0..x1] x [y0..y1]
        for (int cx = x0; cx <= x1; ++cx) for (int cy = y0; cy <= y1; ++cy) {
            auto it = grid.find(cell_key(cx, cy));
            if (it == grid.end()) continue;
            for (Entity b : it->second) {
                if (a.index() >= b.index()) continue;       // 去重 + 自碰撞
                if (!layers_match(ca, cb)) continue;        // 掩码过滤
                if (overlaps(ta, ca, tb, cb))
                    hits.push_back({a, b});
            }
        }
    }
    return hits;
}
```

只有持有 `Velocity` 的"动态"实体才会发起查询，避免了静态实体之间的无效检测。`a.index() >= b.index()` 这个条件同时实现了"避免重复对"与"避免自碰撞"两件事——经典且高效的去重技巧。

### 5.3.4 resolve：最小穿透向量解算

```cpp
inline void resolve(Registry& reg, const std::vector<Hit>& hits) {
    for (auto& [a, b] : hits) {
        // 二次校验：前一次 resolve 推开后，当前可能已不再重叠
        if (!overlaps(ta, ca, tb, cb)) continue;
        // 计算 X / Y 穿透深度，沿小者方向推开
        float ox = std::min(ta.x + ca.width  - tb.x, tb.x + cb.width  - ta.x);
        float oy = std::min(ta.y + ca.height - tb.y, tb.y + cb.height - ta.y);
        if (ox < oy) { /* 沿 X 推开 */ } else { /* 沿 Y 推开 */ }
    }
}
```

按"较小穿透轴"方向推开是 AABB 碰撞响应的常用启发式：方向选择基于"沿哪个轴推得最少最自然"。当二者均可移动时，平摊穿透量；只有一方可动时，由可动方独自承担。`二次校验` 保证当多个 hit 涉及同一实体时，前一次 resolve 的位移会被纳入下一次判定，避免抖动。

### 5.3.5 暴露给 Lua 的查询接口

`LuaBinder` 将 `detect` 暴露为 `get_collisions()` 函数，返回 Lua table 形式的碰撞对，便于脚本端按业务需求做后续处理（如触发剧情、扣血、收集道具）。这一抽象使得 C++ 仅承担"找到所有碰撞对"的机制，Lua 决定"碰到了要做什么"的策略。

> 代码引用：`src/Systems/CollisionSystem.hpp`

## 5.4 InputSystem：raylib 极薄封装

`InputSystem` 仅是 raylib 输入 API 的一层薄包装：

```cpp
namespace Rinn::InputSystem {
    [[nodiscard]] inline bool is_key_down(int key)     { return IsKeyDown(key); }
    [[nodiscard]] inline bool is_key_pressed(int key)  { return IsKeyPressed(key); }
    [[nodiscard]] inline bool is_key_released(int key) { return IsKeyReleased(key); }
    [[nodiscard]] inline float get_mouse_x()           { return GetMousePosition().x; }
    // ……
}
```

设计哲学如其文件注释所言："把字符串映射移到 Lua 加载期，运行时直接传 int 键码，零转换开销"。具体而言，Lua 端可以在脚本启动时以 `local KEY_W = 87` 这样的常量声明键码，运行时调用 `is_key_down(KEY_W)`，完全避免在每帧热路径上执行字符串比较。这种"成本前移"是面向数据设计在 API 层面的体现。

> 代码引用：`src/Systems/InputSystem.hpp`

## 5.5 AudioSystem：单 BGM 流

音频子系统遵循极简主义：全生命周期只维护一首背景音乐，避免引入资源池、混音器等沉重抽象。

```cpp
namespace Rinn::AudioSystem {
    static Music current_bgm   = { 0 };
    static bool  is_bgm_loaded = false;

    inline void Init()       { InitAudioDevice(); }
    inline void PlayBGM(const std::string& path) { /* 卸载旧、加载新、播放 */ }
    inline void Update()     { if (is_bgm_loaded) UpdateMusicStream(current_bgm); }
    inline void Shutdown()   { /* 卸载 + CloseAudioDevice */ }
}
```

`PlayBGM` 在每次切换音乐时执行"先卸载旧、再加载新"的标准流程；`Update` 每帧由主循环调用以推流数据到硬件缓冲；`Shutdown` 在程序退出时释放资源。需要说明的是，命名空间内的 `static` 变量在 C++ 中具有内部链接，若 `AudioSystem.hpp` 被多个 TU 包含会产生独立副本——当前引擎仅 `main.cpp` 一个 TU 引用，故未触发问题。第 9 章将这一点列为待修复的工程债务。

> 代码引用：`src/Systems/AudioSystem.hpp`

## 5.6 DebugUI：基于 Dear ImGui 的实体检视器

调试 UI 的存在是教学型引擎与玩具引擎的关键差异之一。Project Rinn 借助 Dear ImGui + rlImGui 桥接，提供了一个可在运行时勾选展开任意组件类型并查看其全部实例的实体检视器。

### 5.6.1 桥接初始化

```cpp
inline void Init()     { rlImGuiSetup(true); }
inline void Shutdown() { rlImGuiShutdown(); }
inline void Draw(Registry& reg) {
    rlImGuiBegin();
    DrawEntityInspector(reg);
    rlImGuiEnd();
}
```

rlImGui 在 ImGui 与 raylib 之间建立了输入、渲染、字体的桥接，使 ImGui 可以与 raylib 共享同一窗口与帧时序。`rlImGuiBegin / End` 必须严格成对出现，并被夹在每帧绘制的尾部，以保证 UI 显示在游戏内容之上。

### 5.6.2 实体检视器：直接遍历 raw 数据

实体检视器以"按组件类型分类、可勾选展开"的方式呈现：

```cpp
if (show_transform) {
    auto& pool = reg.pool<Transform>();
    if (ImGui::TreeNode("Transform", "Transform (%zu)", pool.size())) {
        for (size_t i = 0; i < pool.size(); ++i) {
            Entity e   = pool.raw_entity_data()[i];
            auto& t    = pool.raw_data()[i];
            ImGui::Text("  [%d] (%.1f, %.1f)", e.index(), t.x, t.y);
        }
        ImGui::TreePop();
    }
}
```

注意这里没有使用 `View<Transform>`，而是通过 `Registry::pool<T>()` 直接拿到 SparseSet，再用 `raw_data()` 与 `raw_entity_data()` 做线性遍历。这种零间接寻址的访问方式与 4.3.5 节描述的"raw 指针接口"用途完全一致：调试 UI 不会增删组件，因此可以在保证安全的前提下绕过 sparse 的两次寻址，最大限度降低对游戏帧率的影响。

### 5.6.3 可扩展的勾选式架构

每个组件类型的展示由一个 `static bool` 控制，加新组件只需复制一个 `if (show_xxx)` 块即可。这种最简单的"复制-粘贴"扩展方式虽然不优雅，但与本章一贯的"单一职责、易于添加"原则一致；未来若组件数量爆炸，可以引入"组件类型 → 检视器函数指针"的注册表来实现真正的可插拔。

> 代码引用：`src/DebugUI/DebugUI.hpp`

---

至此，引擎当前实现的全部子系统已经介绍完毕。它们共享一致的"以 Registry 为输入、对组件做计算"的接口约定，彼此独立，便于教学拆解，也便于未来插入新系统（例如动画、AI、编辑器、后处理）。下一章将转向资源管理与脚本子系统，介绍引擎如何从外部世界获取数据并将控制权部分交还给 Lua 脚本。
