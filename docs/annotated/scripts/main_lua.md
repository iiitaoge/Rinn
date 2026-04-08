# main.lua — 游戏主脚本（深度注释版）

> 文件路径: `scripts/main.lua`  
> 角色: 由 C++ `lua.script_file()` 加载执行。创建实体、加载资源、定义每帧逻辑。

---

## 文件级设计意图

**Lua 脚本在架构中的位置**:
```
C++ 引擎 (机制层)          Lua 脚本 (策略层)
├─ Registry                ├─ 创建哪些实体
├─ SparseSet               ├─ 挂什么组件、参数是多少
├─ PhysicSystem            ├─ 键盘映射 (WASD → 方向)
├─ RenderSystem            └─ 游戏逻辑 (AI、关卡)
└─ CollisionSystem
```

**修改游戏行为只需改 Lua 脚本，无需重新编译 C++**。这是脚本化引擎的核心价值。

---

## Lua 语法系统讲解

> **Lua 基础对比 C++**:
>
> | 概念 | Lua | C++ |
> |------|-----|-----|
> | 变量声明 | `local x = 5` | `int x = 5;` |
> | 函数定义 | `function f() end` | `void f() {}` |
> | 条件 | `if x then ... end` | `if (x) { ... }` |
> | 循环 | `for i=1,10 do end` | `for (int i=1; i<=10; i++)` |
> | 注释 | `-- 单行` / `--[[ 块 ]]` | `// 单行` / `/* 块 */` |
> | 数组下标 | **从 1 开始** | 从 0 开始 |
> | 不等于 | `~=` | `!=` |
> | 幂运算 | `^` | `pow()` |
> | 唯一数据结构 | `table` | 多种（array, map, set...） |

---

## 逐行注释

### 脚本加载阶段（顶层代码）

```lua
print("Entity start")
```

> **Lua `print`**: 内置函数（需要 `sol::lib::base`）。输出到 C++ 的 `stdout`。每帧调用会极慢（IO 阻塞），但这行只在加载时执行一次。
>
> **Lua 不需要 `main()`**: 顶层代码（函数定义之外的语句）在 `script_file()` 时立即执行。

---

### 创建实体

```lua
local tex = load_texture("../../../assets/Guard_Albedo.png")
local Guard_Albedo = create_entity()
```

> **语法知识 — `local` 的重要性**:
>
> Lua 变量默认是**全局**的。不加 `local` → 变量存入全局表 `_G` → 任何地方都能访问和修改 → Bug 风险极高。
>
> ```lua
> x = 5          -- 全局变量: _G["x"] = 5
> local y = 10   -- 局部变量: 只在当前作用域可见
> ```
>
> **性能**: 局部变量存在 Lua 虚拟寄存器中（数组下标访问）。全局变量需要哈希表查找 `_G[name]`。局部变量**快约 30%**。

---

```lua
set(Guard_Albedo, "Transform", {x = 100, y = 200})
set(Guard_Albedo, "Velocity", {x = 0, y = 0})
set(Guard_Albedo, "Collider", {width = 128, height = 128})
set(Guard_Albedo, "Sprite", { texture_id = tex, width = 128, height = 128 })
```

> **语法知识 — Table 构造器 `{}`**:
>
> ```lua
> {x = 100, y = 200}
> ```
> 创建一个 table，键为字符串 `"x"` 和 `"y"`，值为数字。在 C++ 侧通过 `sol::table` 读取:
> ```cpp
> data.get<float>("x")  → 100.0f
> ```
>
> **Table 是 Lua 的万能数据结构**:
> ```lua
> -- 数组
> local arr = {10, 20, 30}       -- arr[1]=10, arr[2]=20, arr[3]=30
> -- 字典
> local dict = {name="Rinn", hp=100}
> -- 混合
> local mixed = {1, 2, name="foo"}  -- mixed[1]=1, mixed[2]=2, mixed.name="foo"
> ```

---

### 每帧更新函数

```lua
function on_update()
    local dx, dy = 0, 0
    if is_key_down(87) then dy = -1 end  -- W
    if is_key_down(83) then dy =  1 end  -- S
    if is_key_down(65) then dx = -1 end  -- A
    if is_key_down(68) then dx =  1 end  -- D
    move(Guard_Albedo, dx, dy)
end
```

> **语法知识 — `function name() ... end`**:
>
> 定义全局函数。等价于:
> ```lua
> on_update = function()  -- 将匿名函数赋给全局变量 on_update
>     ...
> end
> ```
>
> C++ 侧通过 `lua["on_update"]()` 调用。如果 Lua 只定义了 `local function on_update()`（局部函数），C++ 侧将无法访问（局部变量不在全局表中）。

> **语法知识 — 多变量赋值**:
>
> `local dx, dy = 0, 0` 一次声明两个变量。Lua 允许右侧值少于左侧变量（多余的为 `nil`）或多于左侧（多余的丢弃）。

**键码硬编码 `87, 83, 65, 68`**: 对应 WASD 的 ASCII 值。Raylib 的键码常量在 Lua 中不可用，所以直接用数字。

**`dy = -1` 表示向上**: 屏幕坐标系 Y 轴**向下**为正。物理直觉的"向上"在屏幕上是 Y 减小 → `dy = -1`。

**对角线移动速度问题**: 同时按 W+A 时 `dx=-1, dy=-1`，`move` 设置 `vx=-200, vy=-200`，实际速度 = √(200²+200²) ≈ 283 > 200。应该归一化方向向量使对角线速度与直线一致。

---

## 文件级总结

| 设计决策 | 选择 | 理由 |
|---------|------|------|
| 脚本角色 | 创建实体+输入逻辑 | "Lua 做策略" |
| 全局函数 `on_update` | C++ 每帧调用 | 最简的跨语言回调 |
| 键码 | 硬编码 ASCII | 缺陷: 可读性差 |
| 方向速度 | 未归一化 | 缺陷: 对角线超速 |
