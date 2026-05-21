# Project Rinn 零基础到掌控引擎教学方案

## 1. 课程定位

本方案面向刚上大学、几乎没有计算机基础的学生。教学目标不是让学生被动看懂若干源码片段，而是让学生逐步形成对 Project Rinn 的完整掌控能力：能运行、能观察、能解释、能修改、能扩展、能调试、能测试，最终能对局部模块做有依据的优化。

课程默认周期为 12 周。每周包含四个固定环节：

1. 概念课：建立本周需要的最小知识模型。
2. 源码导读：把概念对应到 Project Rinn 的真实文件。
3. 实验任务：完成一个可观察、可验证的改动或分析。
4. 复盘讲解：学生用自己的话解释本周机制。

核心能力路线：

```text
会运行 -> 会观察 -> 会解释主循环 -> 会理解 ECS -> 会修改 System
-> 会扩展 Lua/资源 -> 会测试调试 -> 会优化和重构
```

## 2. 教学原则

- 先建立引擎地图，再进入源码细节。
- 每个概念必须绑定到 Project Rinn 的真实文件。
- 每周必须产出可检查的证据，例如流程图、实验记录、测试结果、讲解稿或小改动。
- 初期少写代码，多观察、多定位、多解释。
- 中期开始做局部修改，训练学生判断功能应该放在哪一层。
- 后期要求学生独立提出修改方案、验证方案和风险说明。
- 不以代码量作为主要评价标准，重点评价解释能力、定位能力、验证能力和迁移能力。

## 3. 项目地图

Project Rinn 可按五层理解：

| 层级 | 主要路径 | 学生需要建立的理解 |
| --- | --- | --- |
| 应用层 | `scripts/main.lua` | 场景、实体放置、高层逻辑由 Lua 描述 |
| 调试层 | `src/UI/` | 运行时观察实体、组件、AI 和事件 |
| 资源与脚本层 | `src/Resources/`, `src/Scripting/` | C++ 引擎如何连接外部资源与 Lua |
| 系统层 | `src/Systems/` | 每帧更新物理、碰撞、渲染、音频、AI |
| 核心层 | `src/Core/`, `src/Components/` | ECS 的 Entity、Component、Registry、SparseSet |

学生最终应能从 `src/main.cpp` 讲清楚一次完整运行：

```text
初始化窗口/音频/UI -> 创建 Registry/ResourceManager -> 初始化 Lua
-> 加载 scripts/main.lua -> 进入主循环
-> Lua/on_update -> TimeControl -> Physics -> Collision
-> Emotion -> Decision -> Action -> Line -> EventBus
-> Render -> TextBubble -> Audio -> Debug UI -> EndFrame
```

## 4. 12 周执行计划

### 第 1 周：入门准备

目标：学生知道程序如何从源码变成运行中的窗口。

概念课：

- 文件、目录、路径、扩展名。
- 源码、编译、链接、运行、报错。
- Visual Studio、CMake、构建目录的基本关系。
- C++ 最小语法：变量、函数、结构体、条件、循环。
- 调试器基本操作：断点、单步、查看变量。

源码导读：

- `CMakeLists.txt`
- `src/main.cpp`

实验任务：

1. 成功构建并运行 Project Rinn。
2. 找到 `main()`。
3. 修改窗口标题。
4. 修改 FPS 显示位置。
5. 在主循环中设置断点，观察循环会反复执行。

提交证据：

- 一张运行截图。
- 一段简短说明：自己修改了哪一处，画面发生了什么变化。
- 一张调试器截图，显示程序停在主循环中。

验收问题：

```text
程序从哪里开始？
什么叫编译？
什么叫运行时？
为什么 while 循环会让画面持续更新？
```

### 第 2 周：引擎总地图

目标：理解 Project Rinn 的整体结构，而不是陷入单个函数。

概念课：

- 引擎和游戏内容的区别。
- 主循环的意义。
- Lua 与 C++ 的分工。
- “高层策略”和“底层机制”的区别。

源码导读：

- `src/main.cpp`
- `scripts/main.lua`
- `docs/thesis/03_architecture.md`

实验任务：

1. 给主循环各阶段标注注释。
2. 记录每帧调用顺序。
3. 画一张 Project Rinn 分层图。
4. 画一张一帧执行流程图。

提交证据：

- 一张分层架构图。
- 一张主循环流程图。
- 一段 3 分钟口头讲解稿。

验收问题：

```text
Lua/on_update 在主循环中的位置是什么？
物理为什么要在碰撞之前？
Debug UI 为什么在渲染后段绘制？
```

### 第 3 周：ECS 入门

目标：理解 Entity、Component、System、Registry 的基本分工。

概念课：

- Entity 是身份编号，不是对象本身。
- Component 是纯数据。
- System 是处理某类组件的逻辑。
- Registry 是统一管理入口。
- 为什么游戏引擎常用 ECS。

源码导读：

- `src/Core/Types.hpp`
- `src/Components/Components.hpp`
- `src/Core/Registry.hpp`

实验任务：

1. 找出 `Transform`、`Velocity`、`Sprite` 的字段。
2. 解释这些组件分别影响画面或行为的哪一部分。
3. 用 Debug UI 观察某个实体拥有的组件。
4. 修改一个组件字段，观察画面变化。

提交证据：

- 一张组件说明表。
- 一段说明：为什么玩家不是一个 `Player` 类。

验收问题：

```text
Entity 本身保存位置吗？
Transform 和 Velocity 的职责有什么不同？
System 为什么不应该把所有数据都存在自己内部？
```

### 第 4 周：使用 ECS 完成小改动

目标：学生能使用已有 ECS 接口理解和完成局部修改。

概念课：

- `create_entity`、`emplace`、`get`、`try_get`、`remove` 的意义。
- “实体拥有组件”和“组件池保存数据”的区别。
- 组件组合如何形成不同对象。

源码导读：

- `src/Core/Registry.hpp`
- `src/Components/Components.hpp`
- `scripts/main.lua`

实验任务：

1. 在 Lua 或 C++ 侧找到创建实体的位置。
2. 给一个实体增加或调整组件。
3. 在 Debug UI 中验证组件变化。
4. 写出实体和组件的关系图。

提交证据：

- 修改前后行为对比。
- 一个实体组件关系图。

验收问题：

```text
如果要让一个实体能移动，至少需要哪些组件或系统配合？
如果只改组件数据，不改 System，行为会发生什么变化？
```

### 第 5 周：EntityPool 与 Entity 句柄

目标：理解 Entity 为什么使用 index + generation，而不是简单整数。

概念课：

- 索引复用。
- 旧句柄失效问题。
- generation 的作用。
- 断言如何保护底层数据结构。

源码导读：

- `src/Core/Types.hpp`
- `src/Core/Registry.hpp`
- `tests/ecs_test.cpp`

实验任务：

1. 跟踪一次实体创建。
2. 跟踪一次实体销毁。
3. 观察同一个 index 被复用时 generation 如何变化。
4. 故意使用一个已销毁实体，观察断言或错误。

提交证据：

- Entity 生命周期图。
- 一段说明：旧句柄为什么危险。

验收问题：

```text
为什么 Entity 不能只保存 index？
generation 解决的是哪类错误？
is_alive 检查了什么？
```

### 第 6 周：SparseSet 与 Registry 深入

目标：理解组件如何被高效存储、删除和遍历。

概念课：

- sparse/dense 双数组。
- O(1) 查询与稠密遍历。
- swap-and-pop 删除。
- View 多组件遍历。
- 数据局部性初步概念。

源码导读：

- `src/Core/SparseSet.hpp`
- `src/Core/Registry.hpp`
- `tests/ecs_test.cpp`

实验任务：

1. 阅读并解释 `emplace`。
2. 阅读并解释 `remove`。
3. 通过测试观察删除中间元素后的 dense 顺序变化。
4. 画出 sparse、dense、dense_to_entity 的关系。

提交证据：

- SparseSet 数据结构图。
- 一个 ECS 测试用例说明。

验收问题：

```text
为什么组件删除不能直接 erase vector 中间元素？
swap-and-pop 牺牲了什么，换来了什么？
SparseSet 为什么适合 System 遍历？
```

### 第 7 周：基础 System 掌控

目标：读懂并修改简单系统。

概念课：

- System 是每帧处理组件的函数或模块。
- `dt` 的意义。
- 输入、物理、碰撞的执行顺序。
- 状态改变应该发生在哪一层。

源码导读：

- `src/Systems/PhysicSystem.hpp`
- `src/Systems/InputSystem.hpp`
- `src/Systems/CollisionSystem.hpp`
- `src/main.cpp`

实验任务：

1. 解释 `PhysicSystem` 如何更新位置。
2. 修改移动速度或位移计算参数。
3. 修改一个碰撞修正行为，并说明影响。
4. 判断某个功能应该写在 Lua、Component 还是 System。

提交证据：

- 修改前后行为说明。
- 一张 System 输入输出表。

验收问题：

```text
PhysicSystem 读取哪些组件，修改哪些组件？
碰撞系统为什么要在物理系统之后？
如果把输入逻辑写进物理系统，会有什么问题？
```

### 第 8 周：渲染、UI 与 AI 管线

目标：理解画面和 AI 行为如何从组件与事件中产生。

概念课：

- 渲染系统如何把组件变成画面。
- UI 和世界渲染的区别。
- 事件流的意义。
- AI 管线：Emotion、Decision、Action、Line、EventBus。

源码导读：

- `src/Systems/RenderSystem.hpp`
- `src/Systems/DecisionSystem.hpp`
- `src/Systems/ActionExecutionSystem.hpp`
- `src/Systems/EventSystem.hpp`
- `src/UI/DebugPanelUI.hpp`

实验任务：

1. 找到精灵绘制入口。
2. 找到 Debug UI 绘制入口。
3. 追踪一次 NPC 决策到动作执行的路径。
4. 给 NPC 行为增加一个简单条件或调试显示。

提交证据：

- 渲染数据流图。
- AI 管线流程图。
- 一个 NPC 行为修改说明。

验收问题：

```text
渲染系统为什么不应该决定 NPC 做什么？
事件系统解决了哪些直接调用难以处理的问题？
Debug UI 观察的是哪些运行时数据？
```

### 第 9 周：Lua 与资源层

目标：理解 C++ 引擎和 Lua 应用层之间的边界。

概念课：

- Lua 适合写场景、配置、高层逻辑。
- C++ 适合写底层机制、高频逻辑、性能敏感代码。
- `LuaBinder` 的作用。
- `ResourceManager` 的作用。

源码导读：

- `src/Scripting/LuaBinder.hpp`
- `src/Scripting/ScriptContext.hpp`
- `src/Resources/ResourceManager.hpp`
- `scripts/main.lua`

实验任务：

1. 在 Lua 中创建一个新 NPC。
2. 添加一张新贴图并显示。
3. 给 Lua 暴露一个新的 C++ 查询函数。
4. 只修改 Lua，不修改 C++，完成一次场景层变化。

提交证据：

- 新实体或新资源运行截图。
- Lua/C++ 边界说明表。

验收问题：

```text
哪些修改适合写在 Lua？
哪些修改必须进入 C++？
Lua 调 C++ 的入口在哪里？
```

### 第 10 周：测试、调试与工程能力

目标：从“能改”变成“改得可靠”。

概念课：

- GoogleTest 基本结构。
- assert 的意义。
- Debug UI 与日志如何帮助定位问题。
- benchmark 能说明什么，不能说明什么。
- 修改前后的风险说明。

源码导读：

- `tests/ecs_test.cpp`
- `tests/systems_test.cpp`
- `tests/pipeline_benchmark.cpp`
- `src/UI/DebugPanelUI.hpp`

实验任务：

1. 为一个 ECS 操作写测试。
2. 为一个 System 行为写测试。
3. 用 Debug UI 定位一次状态错误。
4. 对一次修改写风险与验证说明。

提交证据：

- 测试通过截图或日志。
- 一份风险与验证说明。

验收问题：

```text
你这次修改可能破坏什么？
你用什么证据说明它没有破坏旧功能？
测试和 Debug UI 分别适合发现什么问题？
```

### 第 11 周：优化与架构改造

目标：进入真正的掌控阶段，能解释优化动机、代价和验证方式。

概念课：

- 数据局部性。
- cache 友好结构。
- SparseSet 遍历优化。
- 减少重复查找。
- 减少不必要分配。
- System 顺序对行为和性能的影响。
- Lua/C++ 边界的性能成本。

源码导读：

- `src/Core/SparseSet.hpp`
- `src/Core/Registry.hpp`
- `tests/pipeline_benchmark.cpp`
- `tests/cache_benchmark.cpp`

实验任务：

1. 阅读一次 benchmark 结果。
2. 找出一个可能的重复查询点。
3. 提出一个小优化方案。
4. 实现或模拟该优化。
5. 写出优化前后行为和性能对比。

提交证据：

- benchmark 对比结果。
- 优化说明：优化了什么、代价是什么、如何验证正确。

验收问题：

```text
这个优化优化了什么？
它牺牲了什么？
如何证明它没有改变正确行为？
如果数据规模变小，这个优化还值得吗？
```

### 第 12 周：最终项目

目标：学生独立完成一次小型引擎扩展或优化。

可选题目：

- 新增 `AnimationSystem`。
- 新增最小版 `EditorSystem`。
- 扩展 NPC 行为链。
- 优化 `CollisionSystem`。
- 扩展 `LuaBinder`。
- 改造 `DebugPanel`。
- 为 ECS 核心补充测试和 benchmark。

最终提交：

- 功能说明。
- 修改文件清单。
- 架构影响分析。
- 测试结果。
- 性能或稳定性说明。
- 口头讲解：从主循环讲到自己的改动位置。

最终验收标准：

```text
学生能独立定位相关模块。
学生能解释为什么这样改。
学生能实现修改。
学生能验证正确性。
学生能说明是否影响性能和架构。
```

## 5. 每周固定课堂模板

教师每周按同一节奏组织：

```text
1. 定位：这个功能在引擎地图中的位置是什么？
2. 阅读：对应源码入口在哪里？
3. 观察：运行时能看到什么现象或数据？
4. 修改：做一个最小可观察改动。
5. 验证：证明修改生效且没有破坏旧行为。
6. 复述：学生用自己的话解释机制。
```

每周复盘固定问题：

```text
如果现在没有老师，你能独立解决一个类似问题吗？
如果不能，卡点是概念、源码定位、工具，还是调试？
下一次你准备先从哪里下手？
```

## 6. 学生产出物清单

每名学生最终应积累以下材料：

- Project Rinn 分层架构图。
- 主循环流程图。
- ECS 概念表。
- Entity 生命周期图。
- SparseSet 数据结构图。
- 至少 3 次源码修改记录。
- 至少 2 个测试或测试说明。
- 至少 1 次 benchmark 或性能分析记录。
- 最终项目说明文档。
- 最终口头讲解稿。

## 7. 评分标准

总分 100：

| 项目 | 分值 | 评价重点 |
| --- | ---: | --- |
| 运行与工具链能力 | 10 | 能构建、运行、调试、定位入口 |
| 主循环与架构理解 | 15 | 能解释分层结构和每帧流程 |
| ECS 理解与使用 | 20 | 能解释 Entity/Component/System/Registry/SparseSet |
| System 修改能力 | 20 | 能读懂并修改至少一个系统 |
| Lua/资源扩展能力 | 10 | 能说明并使用 Lua/C++ 边界 |
| 测试与调试能力 | 15 | 能用测试、断点、Debug UI 验证修改 |
| 优化与最终项目表达 | 10 | 能说明优化动机、代价、证据 |

扣分原则：

- 只展示结果但不能解释实现，扣重分。
- 只改代码但没有验证，扣重分。
- 不能说清楚修改影响范围，扣重分。
- 复制代码但不能迁移到相似问题，扣重分。

## 8. 教师使用建议

第一轮教学不建议压缩前 4 周。零基础学生真正困难的地方通常不是某个 C++ 语法，而是不知道“自己正在看的是哪一层”。如果过早进入模板、内存布局和优化，学生容易把工具代码、核心代码、业务代码混在一起。

建议教师始终追问三个问题：

```text
这段代码属于哪一层？
它读了什么数据，改了什么数据？
你如何证明自己的理解是对的？
```

对于能力较强的学生，可以增加挑战任务：

- 给新组件补充 Debug UI 展示。
- 给 LuaBinder 添加一个只读查询接口。
- 为 SparseSet 增加边界测试。
- 对 CollisionSystem 做一次小型性能分析。

对于基础较弱的学生，可以降低初期代码要求：

- 先只画流程图。
- 先只改常量或配置。
- 先只使用断点观察变量。
- 先复述别人写好的小改动。

## 9. 课程完成标准

课程结束时，学生至少应能独立完成以下闭环：

```text
提出一个小功能
-> 判断它属于哪一层
-> 找到相关源码
-> 做出最小修改
-> 运行观察
-> 写测试或验证说明
-> 解释它对架构和性能的影响
```

这就是“掌控引擎”的最低可用标准。

