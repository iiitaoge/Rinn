# Project Rinn 毕业论文 — 主大纲

> 题目候选：**基于 ECS 架构的 HD-2D 游戏引擎设计与实现**
> 字数目标：正文 ≥ 20000 字（学校规范：计算机软件类）
> 参考文献：≥ 20 篇，外文 ≥ 5 篇
> 图：≥ 15 张（架构图、UML、benchmark 图、HD-2D 截图等）
> 表：≥ 5 张（组件清单、API 对照、性能数据等）

---

## 章节字数与素材分配

| 章节 | 标题 | 字数目标 | 主要源码引用 | 状态 |
|------|------|----------|---------------|------|
| 摘要 | 中英文摘要 + 关键词 | 300+300 字 | — | pending |
| 1 | 绪论 | ~2500 | — | pending |
| 2 | 相关技术与理论基础 | ~3500 | — | pending |
| 3 | 引擎总体架构与模块划分 | ~2000 | `main.cpp`, `CMakeLists.txt` | pending |
| 4 | ECS 核心设计与实现 | ~4000 | `Core/*.hpp`, `Components/Components.hpp` | pending |
| 5 | 系统模块的实现 | ~5000 | `Systems/*.hpp`, `src/UI/ComponentUI.hpp`, `src/UI/AIDebugUI.hpp` | pending |
| 6 | 资源管理与脚本系统 | ~2500 | `Resources/`, `Scripting/`, `scripts/*.lua` | pending |
| 7 | HD-2D 渲染原理验证 | ~2000 | `RenderSystem.hpp`, `assets/shaders/test.{vs,fs}` | pending |
| 8 | 性能测试与分析 | ~2500 | `tests/{cache,false_sharing,ecs}_*.cpp` | pending |
| 9 | 总结与展望 | ~1500 | — | pending |
| — | 参考文献 | ≥ 20 篇 | — | pending |
| — | 附录 | 按需 | 关键代码片段 | pending |
| — | 致谢 | 短 | — | pending |

**正文合计：~24000 字**（留 20% 缓冲）

---

## 可扩展性设计原则

当前已完成：HD-2D 原理验证、NPC AI 管线。未来待做：HD-2D 后处理完善、编辑器。论文骨架按以下原则避免后续重排：

1. **第 3 章架构图分层**：内核（ECS） / 子系统（Render/Physics/NPC AI/...） / 上层（脚本 / 编辑器）。未来 Editor 模块只需新增方框，原图不动。
2. **第 5 章子系统并列**：每个 System 一节。5.7 为 NPC AI 管线；未来新增 AnimationSystem / EditorSystem 各加一节即可，章节号不变。
3. **第 7 章 HD-2D 写"原理验证"而非"完整实现"**：明确标注当前实现 TBN 光照 + 环境光混合，bloom / tilt-shift 列入第 9 章展望。
4. **第 9 章展望分级**：短期（HD-2D 后处理）/ 中期（编辑器）/ 远期（AI 深化）。任一项做完后从展望"提级"为正章节。
5. **新章节插入位置**：固定在「第 7 章 HD-2D」之后、「性能测试」之前。例如做完编辑器后，可插入「第 8 章 编辑器设计与实现」，原 8/9 顺延为 9/10。前 7 章不动。

---

## 章节详细大纲

### 第 1 章 绪论 (~2500 字)

- **1.1 课题研究背景与意义** (~700)
  - 独立游戏与中小团队对自研引擎的需求
  - HD-2D 风格在 *Octopath Traveler* 等作品后的兴起
  - 数据驱动 ECS 架构在 Unity DOTS / Bevy 推动下的工业化
  - 本课题以"自下而上"的方式探索引擎核心机制，兼具学习价值与工程价值
- **1.2 国内外研究与实践现状** (~900)
  - 工业引擎：Unreal、Unity、Godot 的架构演进
  - ECS 框架：EnTT (C++)、Bevy ECS (Rust)、Flecs、Unity DOTS
  - HD-2D 公开技术分享：Square Enix 的 *Octopath Traveler 2* GDC 报告
  - 嵌入式脚本：Lua + sol2 在 World of Warcraft、Roblox 客户端等的运用
  - 学术：Adams 等人对 cache-aware 数据布局的研究、面向数据设计 (DOD)
- **1.3 主要研究内容与目标** (~600)
  - 实现自研 ECS（Entity 句柄 / SparseSet / Registry / View）
  - 集成基础子系统（渲染 / 物理 / 碰撞 / 输入 / 音频 / 调试 UI）
  - 实现 Lua 脚本子系统并完成 Tiled 地图驱动的小型 demo
  - 进行 HD-2D 风格的渲染原理验证（法线区分、TBN 光照）
  - 通过 cache / false-sharing benchmark 验证数据布局有效性
  - 实现 NPC AI 管线（Need/Emotion → EventBus → Appraisal → Utility AI Decision → ActionExecution → Line）
  - **明确不做**：完整 HD-2D 后处理（bloom/tilt-shift）、可视化编辑器（列入展望）
- **1.4 论文组织结构** (~300)
  - 各章简介 + 阅读路径建议

---

### 第 2 章 相关技术与理论基础 (~3500 字)

- **2.1 实体-组件-系统 (ECS) 架构** (~900)
  - OOP 继承层级在游戏中的痛点（菱形继承、组件膨胀）
  - 组合优于继承的思想
  - ECS 三要素：Entity / Component / System
  - 与 Component-Based / Actor 模型的区别
  - 工业实践：Bitsquid 早期文章、EnTT、Unity DOTS、Bevy
- **2.2 面向数据设计与 Cache 局部性** (~800)
  - Cache 层级（L1/L2/L3）与延迟（5/12/40 cycles）
  - AoS vs SoA 布局
  - SparseSet 的 O(1) 增删查与 Dense 数组的连续遍历
  - 软件预取 (`_mm_prefetch`) 的作用
- **2.3 Lua 嵌入式脚本与 sol2 绑定** (~600)
  - 数据-逻辑分离的工程价值
  - Lua 5.4 + sol2 的双向绑定原理
  - usertype / set_function / sol::table 用法
- **2.4 raylib 渲染管线与 OpenGL 基础** (~700)
  - raylib 的 rlgl 抽象层（rlBegin / rlVertex3f）
  - 顶点着色器 / 片段着色器的输入输出
  - 透视投影 + Camera3D 在 2.5D 场景的应用
- **2.5 HD-2D 视觉风格综述** (~500)
  - HD-2D 概念：2D 精灵 + 3D 场景 + 现代后处理
  - 关键技术点：法线贴图、深度排序、bloom、tilt-shift 景深
  - *Octopath Traveler 2* 的渲染流程公开资料
- *(预留 2.6) 行为树 / GOAP 等 AI 理论 — 后续填入*

---

### 第 3 章 引擎总体架构与模块划分 (~2000 字)

- **3.1 设计目标与约束** (~300)
  - 单线程优先、单平台 (Windows) 起步、可读性优先
  - 容量上限：16384 实体、64 组件类型
  - 编译期约束：组件需 `is_aggregate_v` + `is_trivially_copyable_v`
- **3.2 分层架构图** (~700)
  - 图 3.1：分层架构图（内核 / 子系统 / 资源 / 脚本 / 调试 / 上层应用）
  - 各层职责
  - **可扩展性说明**：编辑器 / AnimationSystem 在图中以虚线框预留位置；NPC AI 管线已实现（步骤 6–10）
- **3.3 模块清单与文件结构** (~400)
  - 表 3.1：源码目录与对应模块
  - 引言级 `CMakeLists.txt` 解读
- **3.4 主循环与数据流** (~600)
  - 图 3.2：每帧执行流时序图
  - `main.cpp` 主循环逐行解析
  - `Lua on_update` → C++ Physics → Collision → Render → Audio 的执行顺序
  - **可扩展性说明**：调度方式当前硬编码，未来可演化为 System Schedule

---

### 第 4 章 ECS 核心设计与实现 (~4000 字)

- **4.1 Entity 句柄设计** (~500)
  - 32 位布局：`[Generation 16 | Index 16]`
  - "ABA 问题"与版本号（generation）的作用
  - 代码引用：`Core/Types.hpp:21-61` (Entity)
- **4.2 EntityPool：环形缓冲 + 版本号自增** (~700)
  - 容量必为 2 的幂、`MASK = CAPACITY - 1` 替代取模
  - acquire / release 双路径（复用 vs 开荒）
  - 代码引用：`Core/Registry.hpp:16-110` (EntityPool)
- **4.3 SparseSet 双数组结构** (~900)
  - Sparse[16384] 数组 + Dense `vector<T>` + dense_to_entity 反向映射
  - emplace / get / remove (swap-and-pop) 三大操作
  - "重复 emplace 返回旧值"的语义抉择
  - 软件预取 prefetch / prefetch_dense 的两阶段设计
  - 代码引用：`Core/SparseSet.hpp`
- **4.4 ComponentID 的编译期分配** (~400)
  - 模板特化 + 静态局部变量的"一次性分配"惯用法
  - 原子计数器 `ComponentCounter` 与跨 TU 共享
  - 代码引用：`Core/ComponentID.hpp`
- **4.5 Registry 的统一接口与签名** (~800)
  - `array<Signature, MAX_ENTITIES>` 与 `array<unique_ptr<ISparseSet>, MAX_COMPONENTS>` 的双层映射
  - emplace / has / get / remove 的对外 API
  - destroy_entity 的"位扫描" (`std::countr_zero`) 加速
  - 代码引用：`Core/Registry.hpp:112-271` (Registry)
- **4.6 View：变长模板与最小池驱动** (~700)
  - 多组件查询的"最小池驱动法"
  - 缓存 entity_data 指针、消除虚函数
  - 惰性求值的 viewIterator
  - 代码引用：`Core/Registry.hpp:274-391` (View)
- *(预留 4.7) 系统调度器 — 后续填入*

---

### 第 5 章 系统模块的实现 (~5000 字)

- **5.1 RenderSystem：三维场景下的精灵渲染** (~800)
  - Camera3D 45° 透视（fovy = 45.0f）
  - WORLD_SCALE 像素↔米换算
  - 中文字体加载与 CJK 字符集
  - 两趟渲染：地面瓦片 → 立式精灵 + 排序
  - 屏幕空间对话气泡 (`GetWorldToScreen`)
  - 代码引用：`Systems/RenderSystem.hpp`
- **5.2 PhysicSystem：纯位置积分** (~250)
  - 单职责：仅做 `t.x += v.vx * dt`
  - 边界裁剪交给 Lua（设计取舍）
  - 代码引用：`Systems/PhysicSystem.hpp`
- **5.3 CollisionSystem：空间哈希 + AABB** (~800)
  - 宽相 (broad phase) + 窄相 (narrow phase) 二阶段
  - 64 像素单元的 unordered_map 网格
  - 碰撞层 / 掩码 (layer / mask) 过滤
  - 双轴 MTV (Minimum Translation Vector) 解算
  - 代码引用：`Systems/CollisionSystem.hpp`
- **5.4 InputSystem：raylib 极薄封装** (~250)
  - 设计哲学：键码常量在 Lua 加载期映射，运行时零开销
  - 代码引用：`Systems/InputSystem.hpp`
- **5.5 AudioSystem：单 BGM 流** (~250)
  - 做减法：全生命周期一首背景音乐
  - 代码引用：`Systems/AudioSystem.hpp`
- **5.6 DebugUI：基于 Dear ImGui 的实体检视器** (~600)
  - rlImGui 桥接 raylib
  - 直接遍历 `pool<T>().raw_data() / raw_entity_data()` 实现零拷贝可视化
  - 代码引用：`src/UI/ComponentUI.hpp`
- **5.7 NPC AI 子系统：需求-情绪-决策-执行管线** (~1500)
  - EventBus：FIFO 发布-订阅，确定性 Drain
  - NeedComponent / EmotionComponent：动机与情绪状态基底
  - AppraisalSystem：主观解读因子 + KnowledgeFact bitset 信息差
  - DecisionSystem：Utility AI（效用 = 增益 × 需求紧迫度 × 情绪调制因子）
  - ActionExecutionSystem：执行、satisfaction 补偿、发布完成事件
  - LineSystem：34 条 LineDef catalog、softmax 采样、立场一致性 stance_factor
  - 代码引用：`Systems/{EventSystem,AppraisalSystem,DecisionSystem,ActionExecutionSystem,LineSystem}.hpp`
- *(预留 5.8) 动画系统 — 后续填入*

---

### 第 6 章 资源管理与脚本系统 (~2500 字)

- **6.1 ResourceManager：纹理 ID 化** (~400)
  - 路径 → uint16 ID 的哈希映射
  - vector 连续存储 Texture2D
  - 自动卸载与析构
  - 代码引用：`Resources/ResourceManager.hpp`
- **6.2 Lua 状态初始化与 sol2 绑定** (~700)
  - sol::state 与 open_libraries
  - usertype 黑盒导出 Entity
  - set_function 注册 C++ 闭包
  - 字符串调度 vs 类型化绑定的取舍
  - 代码引用：`Scripting/{ScriptContext, LuaBinder}.hpp`
- **6.3 Tiled 地图驱动的关卡构造** (~900)
  - Tiled 编辑器与导出 Lua 格式
  - 图块层 (tilelayer) → ECS 实体的转换
  - 对象层 (objectgroup) → 触发器与碰撞墙
  - GID 翻转位剥离 `gid % 2^28`
  - 代码引用：`scripts/map_loader.lua`
- **6.4 数据驱动剧情：分支 + flag 系统** (~500)
  - `dialogue_data.lua` 的纯数据声明
  - 主程序 `resolve_branch` 的引擎无关推演
  - effect 回调实现"开门"等场景变化
  - 代码引用：`scripts/{dialogue_data, main}.lua`

---

### 第 7 章 HD-2D 渲染原理验证 (~2000 字)

- **7.1 HD-2D 的视觉构成** (~400)
  - 像素美术 + 透视相机 + 体积光 + 法线感
  - 与传统 2D / 3D 的视觉差异（图示）
- **7.2 立式精灵 vs 瓦片精灵的区分** (~500)
  - `Sprite::is_ground` 字段的引入
  - DrawSprite3D 的两种顶点摆放（XZ 平面 vs XY 立面）
  - 顶点顺序与法线朝向的关系
  - 代码引用：`RenderSystem.hpp:131-163` (DrawSprite3D)
- **7.3 Shader：法线 + 朗伯光照** (~600)
  - 顶点 shader：仅做 MVP 变换
  - 片段 shader：采样 albedo + 法线贴图，N·L 光照
  - facenormal uniform 切换立式 / 地面
  - 代码引用：`assets/shaders/test.fs`
- **7.4 当前局限与改进方向** (~500)
  - 光源方向硬编码、未做 gamma 校正
  - 立式精灵的法线非真法线（仅整体朝向）
  - 缺失：深度排序的 Z-buffer 修正、bloom、tilt-shift、阴影
  - **明确将 7.4 后半段对应到第 9 章展望的"短期工作"**

---

### 第 8 章 性能测试与分析 (~2500 字)

- **8.1 单元测试：ECS 核心的正确性保证** (~600)
  - GoogleTest 的引入与 BUILD_TESTS 选项
  - 四类测试：SparseSet 边界、Entity 生命周期、View 遍历、System 确定性
  - 表 8.1：测试用例汇总（70+ 个 EXPECT）
  - 代码引用：`tests/ecs_test.cpp`
- **8.2 Cache 局部性实测** (~700)
  - 实验设计：4 个场景（顺序/随机 索引、双池交替、rdtsc 单次延迟）
  - 表 8.2：ns/get 数据
  - 分析：连续 index → L1 命中、分散 index → L2/L3 退化
  - 代码引用：`tests/cache_benchmark.cpp`
- **8.3 False Sharing 实测** (~700)
  - 实验设计：单线程 baseline → AoS 双线程 → padded → SoA
  - 表 8.3：4 组数据
  - 分析：3-15× 减速来自 cache line 失效
  - 代码引用：`tests/false_sharing_bench.cpp`
- **8.4 端到端帧率与稳定性** (~500)
  - 144 FPS 目标、`SetTargetFPS` 设置
  - DebugUI 显示实时 FPS
  - 当前 demo（Tiled 地图 + 单玩家 + 多 NPC）实测帧率
  - 已知瓶颈（来自 `Project_Audit.md`）：每帧两 vector 分配、CollisionSystem 哈希重建

---

### 第 9 章 总结与展望 (~1500 字)

- **9.1 工作总结** (~500)
  - 完成的 ECS 核心、子系统、Lua 脚本、HD-2D 雏形、性能测试
  - 主要技术贡献：版本号化 Entity、SparseSet + 软件预取、最小池 View、Tiled 数据驱动
- **9.2 不足之处** (~400)
  - 命名空间级全局造成的隐式单例
  - Lua 字符串 dispatch 的扩展性瓶颈
  - HD-2D 仅雏形
  - System 调度硬编码
- **9.3 未来工作** (~600)
  - **短期**（已规划）：HD-2D 完善（深度排序、bloom、tilt-shift）
  - **中期**：可视化编辑器（基于 Dear ImGui，复用 DebugUI 模式）
  - **远期**：AI 驱动模块（行为树 / GOAP / LLM-based）
  - 工程清理：参照 `Project_Audit.md` 的 P0/P1 项

---

### 参考文献候选（≥ 20，外文 ≥ 5）

- ECS 与面向数据设计：Adams、Acton、Bitsquid 文章
- EnTT、Bevy、Flecs 官方文档
- *Game Engine Architecture* — Jason Gregory
- *Game Programming Patterns* — Robert Nystrom
- raylib 官方手册、OpenGL Programming Guide (Red Book)
- HD-2D：Square Enix GDC 演讲、相关 SIGGRAPH 资料
- Lua 5.4 Reference Manual、sol2 文档
- 软件预取与 cache 局部性：Drepper "What Every Programmer Should Know About Memory"
- 中文：游戏引擎架构 (中译本)、3D 数学基础

---

### 附录建议内容

- 附录 A：核心代码节选（SparseSet、Registry::View、shader）
- 附录 B：组件清单与字段表
- 附录 C：Lua 绑定 API 速查
- 附录 D：性能测试原始数据
- 附录 E：缩略语表（ECS、AABB、AoS、SoA、MVP、CJK 等）

---

## 写作进度跟踪

进度由 TaskList 管理（任务 #1 — #14）。每写完一章 → 标 completed。
