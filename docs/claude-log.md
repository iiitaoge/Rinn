# HD-2D 学习对话日志

## 概念锚点

- **图片没有顶点**。顶点属于几何（quad 4 个顶点），图片只有像素。UV 是桥梁。
- **VRAM = GPU 的专属内存**。"传给 GPU" = "上传到 VRAM"，是同一件事。
- **rlVertex3f 已经是"算顶点+传顶点"**。raylib 立即模式包了底层 VBO + draw call。
- **Attribute 是 GPU 端概念**。C++ 端只上传数据，raylib 通过命名约定（`vertexPosition`/`vertexTexCoord`/`mvp`）自动接到 shader 的 `in` 变量。命名是宏定义，绑定靠 OpenGL 的 attribute location。
- **albedo 和法线图共享 UV**。两张图描述同一表面的不同属性（颜色 vs 微观朝向），UV 错位 = 鼻梁高光打在耳朵上。
- **N·L 永远是世界空间**。L 在世界空间，N 必须也在世界空间才能点乘。

## 设计原则（D1/D2/D3 抽出）

- **D1 延迟抽象**：1 个 shader 用 inline 全局即可。出现"第二个/按名查找/热重载"信号再抽 ShaderManager。
- **D2 强制不能漏的事，放进签名**：法线图是必需的 → 改 DrawSprite3D 签名让编译器强制传。可选效果反过来。
- **D3 状态切换按业务边界开关**：BeginShaderMode 包 sprite 的两个 pass，文字气泡用 raylib 默认 shader。每 quad 切换浪费，整帧切换会污染其他绘制。

## 关键洞察：facenormal 不是临时脚手架

错误心智模型："facenormal 硬编码 → 有了法线图就丢"。
正确：**facenormal 和采样法线是合作关系**：

- 采样法线 = 切线空间下的"微观凸起"（相对表面自身）
- facenormal = 世界空间下的"表面整体朝向"
- `N_world = transform(N_tangent, facenormal)` → 然后 `N·L`

这样一张 Laigter 法线图能服务任意朝向的 quad（地面/墙/旋转/动画）。否则需要 N×M 张专属法线图。

## 轴对齐 quad 的简化变换

- 立式（facenormal=+Z）：切线 +Z 对齐世界 +Z → **不用变换**
- 地面（facenormal=+Y）：切线 +Z 应映射到世界 +Y → **swizzle (x,y,z) → (x,z,y)**

立式精灵"看起来对"是巧合：切线空间和世界空间方向恰好一致。

完整通用版需要 TBN 矩阵；HD-2D 全是轴对齐 quad 可走 if-else 快路径。

## 当前代码状态（RenderSystem.hpp + test.fs/.vs）

✓ Shader 管线接通（LoadShader/Init）
✓ Sprite 加 normal_id
✓ DrawSprite3D 签名加 normal 参数
✓ facenormal uniform 设置接口（每 pass 设一次）
✓ 三级目标 Level 1-3 全跑通

## 已知 bug（按严重度）

🔴 **facenormal uniform 在 fragment shader 里声明但从未使用**。直接拿 `N = sample*2-1` 当世界法线，地面/立式都按"朝相机"算光照。空间变换陷阱。

🔴 **Tiled 加载的 sprite 没设 normal_id**（main.lua 53/67 行），但 shader 无脑 `texture(sprite_normal, uv)`。未绑定纹理 sampler 返回 (0,0,0,1) → N=(-1,-1,-1) → N·L<0 → **地面全黑**。这就是"立式亮、地面黑"的真因。

🟡 `GetShaderLocation` 每帧每 sprite 调用（DrawSprite3D 和 Facenormal 函数内）。应在 Init 缓存成 inline int location。

🟡 `Facenormal()` 命名是名词，应为 `UploadFaceNormal()`。

🟡 `BeginShaderMode` 实际调用位置不明（RenderSystem.hpp 内未见）。需确认 shader 是否真在跑：把 finalColor 改成 `vec4(1,0,0,1)` 验证。

## HD-2D 完成度 ~15%

剩余清单：
- 法线空间变换（修 facenormal 使用）
- 环境光（避免 N·L<0 全黑过硬）
- 光源方向 uniform 化（移出 shader 硬编码）
- 后处理：Bloom / 移轴景深（高斯模糊）/ 色彩分级（需 RenderTexture + 多 pass）
- fake shadow（精灵下椭圆暗斑）

不做（YAGNI）：多光源、点光源、真实阴影。

## 用户待答验证题（学习模式约束）

1. shader 怎么知道当前像素属于地面还是立式？哪个 uniform 携带这个信息？
2. 30° 斜坡的 facenormal 长啥样？分量 swizzle 还能用吗？为什么？
3. normal_id 缺失时 fragment shader 应拿到什么"假法线图"才能表现成"平面无凹凸"？颜色是？（提示：解码后 N=(0,0,1) 对应的 RGB）

## 用户档案（学习模式）

- 默认学习模式（CLAUDE.md 强约束）：不直接写完整代码，引导推理，验证理解
- 工作模式触发词："工作模式"
- 当前在 raylib + 自研 ECS 上做 HD-2D 渲染管线
- 概念吸收快，但容易把"临时脚手架"和"永久接口"混淆（facenormal 误判即典型）
