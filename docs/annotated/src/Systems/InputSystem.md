# InputSystem.hpp — 输入系统（深度注释版）

> 文件路径: `src/Systems/InputSystem.hpp`  
> 角色: 对 Raylib 输入 API 的**零开销封装**。

---

## 文件级设计意图

**为什么要封装 Raylib 的输入函数？** Raylib 全局函数（如 `IsKeyDown`）直接可用，封装层的价值在于：

| 理由 | 说明 |
|------|------|
| 命名空间隔离 | `InputSystem::is_key_down` 明确属于输入模块 |
| 未来抽象 | 如果将来换底层库（GLFW 替代 Raylib），只需改这个文件 |
| Lua 绑定友好 | 自由函数可以直接取地址传给 Sol2 |

**缺陷**: 目前是 1:1 转发，没有增加任何价值（如输入缓冲、按键映射、游戏手柄支持）。如果永远不换底层库，这层封装就是纯开销（心智负担，非性能负担——inline 后开销为零）。

---

## 逐行注释

### 键盘 API — 三种按键状态

```cpp
[[nodiscard]] inline bool is_key_down(int key) { return IsKeyDown(key); }
[[nodiscard]] inline bool is_key_pressed(int key) { return IsKeyPressed(key); }
[[nodiscard]] inline bool is_key_released(int key) { return IsKeyReleased(key); }
```

| 函数 | 触发帧 | 持续帧 | 适用场景 |
|------|--------|--------|---------|
| `is_key_down` | 每帧 | 只要按住 | **移动**、加速、持续射击 |
| `is_key_pressed` | 仅按下第一帧 | 单帧 | **跳跃**、切换武器、对话推进 |
| `is_key_released` | 仅松开第一帧 | 单帧 | 蓄力释放、弹弓效果 |

```
按键时间线:
帧 1  帧 2  帧 3  帧 4  帧 5  帧 6
 ↓按   持   持    持    ↑松   -
 
is_key_pressed:  T  F  F  F  F  F
is_key_down:     T  T  T  T  F  F
is_key_released: F  F  F  F  T  F
```

**键码**: Raylib 使用 ASCII 值作为键码。`KEY_W = 87`，`KEY_A = 65`，`KEY_S = 83`，`KEY_D = 68`。Lua 侧直接传数字 `is_key_down(87)` 代替符号常量。

**设计缺陷**: Lua 代码中硬编码键码数字 (`87, 65, ...`) 可读性差。改进方案是在 Lua 侧定义常量表：
```lua
KEY = { W = 87, A = 65, S = 83, D = 68, SPACE = 32 }
if is_key_down(KEY.W) then ... end
```

---

### 鼠标 API

```cpp
[[nodiscard]] inline float get_mouse_x() { return GetMousePosition().x; }
[[nodiscard]] inline float get_mouse_y() { return GetMousePosition().y; }
```

> **语法知识 — 临时对象成员访问**:
>
> `GetMousePosition()` 返回 `Vector2` 结构体（值类型，不是引用）。`.x` 直接访问临时对象的成员。
>
> 临时对象在语句结束后立即销毁。对 `Vector2`（8 字节 POD）来说完全没问题。

**设计缺陷**: 每次调用 `get_mouse_x()` 和 `get_mouse_y()` 分别调用一次 `GetMousePosition()`。如果同时需要 x 和 y，更高效的做法是返回整个 `Vector2`：
```cpp
auto pos = GetMousePosition();  // 一次调用
use(pos.x, pos.y);
```
但对 Lua 侧来说，分别取 x 和 y 更方便（Lua 不能直接接收 C 结构体，除非注册为 usertype）。
