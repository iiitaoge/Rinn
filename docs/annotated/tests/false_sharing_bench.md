# false_sharing_bench.cpp — 伪共享基准测试（深度注释版）

> 文件路径: `tests/false_sharing_bench.cpp`  
> 角色: 可控对照实验，用三种内存布局量化 False Sharing 的性能代价。

---

## 核心概念：False Sharing

**问题定义**: 两个线程修改**不同的变量**，但这两个变量恰好在**同一条 Cache Line**（64 字节）上。CPU 缓存一致性协议（MESI/MOESI）会在每次写入后使对方核心的缓存失效，导致两个核心不断从 L3/DRAM 重新加载数据。

```
     Core 0                    Core 1
     ┌────────────┐            ┌────────────┐
     │ L1 Cache   │            │ L1 Cache   │
     │ [x][y][layer]│          │ [x][y][layer]│
     └─────┬──────┘            └─────┬──────┘
           │ 写 x                     │ 写 layer
           ▼                          ▼
     MESI: "我改了 x！Cache Line 无效！"
                    ←→
     MESI: "我改了 layer！Cache Line 无效！"
     
     结果: 两核不断互相失效，数据在 L1↔L3 之间乒乓 (~40 cycles/次)
```

**True Sharing**: 两个线程修改**同一个变量** → 需要同步。
**False Sharing**: 两个线程修改**不同变量但同一 Cache Line** → 不该有竞争但有了。

---

## 三种布局对比

### 1. AoS (Array of Structs)

```cpp
struct Transform_AoS { float x, y; int layer; };  // 12 bytes
```

```
Cache Line 64 bytes:
[Transform_0: x|y|layer][Transform_1: x|y|layer]...
 ↑ 线程A写x,y              ↑ 线程B写layer
 → 同一 Cache Line → False Sharing!
```

一条 Cache Line 装 `64/12 ≈ 5` 个 Transform。两个线程写不同元素的不同字段，但极可能在同一 Line 上。

### 2. Padded (Cache Line 对齐)

```cpp
struct alignas(64) Transform_Padded { float x, y; int layer; };
```

> **语法知识 — `alignas(N)` (C++11)**:
>
> 强制类型/变量在 N 字节边界对齐。`alignas(64)` 意味着每个实例的地址一定是 64 的倍数 → 每个实例独占一条 Cache Line。
>
> ```
> sizeof(Transform_AoS)    = 12 bytes  (紧凑)
> sizeof(Transform_Padded) = 64 bytes  (填充52字节浪费)
> ```
> 内存膨胀 5.3 倍。但消除了 False Sharing。

### 3. SoA (Struct of Arrays)

```cpp
std::vector<Position> positions;  // [x0,y0, x1,y1, x2,y2, ...]
std::vector<int> layers;          // [l0, l1, l2, ...]
```

物理线程只访问 `positions`，渲染线程只访问 `layers`。两个数组在**完全不同的内存区域** → 零 Cache Line 共享 → 零竞争。

---

## 测量方法

### 原子同步启动

```cpp
std::atomic<bool> go{false};
auto work = [&] {
    while (!go.load(std::memory_order_acquire)) {}
    // ... 工作负载
};
std::thread t0(work_a);
std::thread t1(work_b);
go.store(true, std::memory_order_release);
```

> **语法知识 — `memory_order_acquire` / `memory_order_release`**:
>
> 这是一对配合使用的内存序:
>
> **`release`** (store 端): "在我写入 `true` 之前的所有操作，都对 `acquire` 方可见"。
> **`acquire`** (load 端): "在我读到 `true` 之后的所有操作，都能看到 `release` 方之前的写入"。
>
> 保证两个线程在 `go = true` 之后看到一致的共享状态。

**设计意图**: 两个线程先创建并 spin-wait，然后同时启动，确保竞争最大化。

---

## 预期结果与分析

```
场景 1 (Single Thread):        基准线 T
场景 2 (AoS Two Threads):      ~3-15x T  ← False Sharing!
场景 3 (Padded Two Threads):   ~1-1.2x T ← 消除竞争
场景 4 (SoA Two Threads):      ~0.9-1.1x T ← 最优，甚至可能更快（每线程工作集更小）
```

**AoS 为什么慢 3-15 倍？** 每次写入触发 MESI 协议:
1. 线程 A 写 `x` → Cache Line 标记为 Modified
2. 线程 B 写 `layer` (同一 Line) → 发现 Line 被 A Modified → 请求无效化 → A 写回 L3 → B 从 L3 加载 → B Modified
3. 线程 A 再写 `y` → 发现 Line 被 B Modified → 同样的流程...

**每次 L3 round-trip ≈ 40 cycles**。在紧密循环中，几乎每次写入都触发 → 吞吐量被 L3 延迟限制。

**SoA 为什么最优？** 两个线程操作的数据在不同 page 上，Cache 一致性协议**完全不介入**。而且每个线程的工作集更小（只有 `float` 数组），L1 命中率更高。

---

## 对 ECS 设计的启示

| 发现 | 对 ECS 的影响 |
|------|-------------|
| AoS 有 False Sharing | 如果将来 ECS 做多线程 System，相邻实体的不同组件不能放同一 Cache Line |
| SoA 消除竞争 | SparseSet 的 Dense 数组已经是 SoA 式的（每种组件独立 vector）→ 天然无 False Sharing |
| 对齐填充浪费大 | `alignas(64)` 是暴力方案，SoA 是优雅方案 |

---

## 文件级总结

| 设计决策 | 选择 | 理由 |
|---------|------|------|
| 对照组 | 3 种布局 (AoS/Padded/SoA) | 完整覆盖 False Sharing 的因果关系 |
| 同步方式 | atomic + spin-wait | 精确控制线程同时启动 |
| 计时 | chrono 纳秒 | rdtsc 更精确但受频率变化影响 |
