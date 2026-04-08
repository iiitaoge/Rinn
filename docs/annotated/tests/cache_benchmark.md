# cache_benchmark.cpp — 缓存性能基准测试（深度注释版）

> 文件路径: `tests/cache_benchmark.cpp`  
> 角色: 实测 SparseSet 的 `Sparse[idx] → Dense[pos]` 两次间接寻址在不同访问模式下的延迟。

---

## 文件级设计意图

**目标**: 用数据验证 SparseSet 的 Cache 设计是否真的有效。理论分析说"Sparse 32KB 适配 L1"，但实测结果可能不同（因为 OS 调度、TLB、预取器行为等因素）。

**CPU 缓存层级回顾**:

| 层级 | 大小 (典型 Intel) | 延迟 | 带宽 |
|------|-------------------|------|------|
| L1 数据 | 32-48 KB | ~1ns (4 cycles) | ~1TB/s |
| L2 | 256KB-1MB | ~4ns (12 cycles) | ~400GB/s |
| L3 | 8-32 MB (共享) | ~10ns (40 cycles) | ~200GB/s |
| 主存 DRAM | 16-64 GB | ~50-100ns (200 cycles) | ~50GB/s |

**L1 命中 vs DRAM 未命中**: 50-100 倍的延迟差距。Cache 友好的数据布局可以让程序快两个数量级。

---

## 核心实验方法

### flush_cache — 刷新缓存

```cpp
static volatile char flush_buffer[4 * 1024 * 1024]; // 4MB
void flush_cache() {
    for (size_t i = 0; i < sizeof(flush_buffer); i += 64) {
        flush_buffer[i] = static_cast<char>(i);
    }
}
```

> **语法知识 — `volatile` 关键字**:
>
> `volatile` 告诉编译器**不要优化掉**对该变量的读写。编译器见到没人使用的数组写入通常直接删除（dead store elimination）。`volatile` 防止这种优化。
>
> **`volatile` ≠ 线程安全**: `volatile` 不提供原子性或内存序保证。它只是"每次访问都去内存读/写"。多线程同步应该用 `std::atomic`。

**4MB 大于 L3 单核份额**:

```
缓存占位原理:
  CPU 缓存是全关联或组关联的。写入 4MB 数据会占满 L1/L2/L3，
  把之前存在缓存中的 SparseSet 数据 "挤出去"。

  之后再访问 SparseSet → Cache Miss → 从 DRAM 加载 → 延迟暴增
```

**步长 64**: 一条 Cache Line = 64 字节。每 64 字节写一次就足够占满每条 Line。无需逐字节写——那只是给同一条 Line 重复写入。

---

### rdtsc — CPU 周期精确计时

```cpp
_mm_lfence();
unsigned long long t0 = __rdtsc();
_mm_lfence();
volatile auto& data = pool.get(entity);
_mm_lfence();
unsigned long long t1 = __rdtsc();
```

> **语法知识 — `__rdtsc()` (Read Time Stamp Counter)**:
>
> x86 指令 `RDTSC`，读取 CPU 内部的 64 位时钟周期计数器。每个 CPU 周期 +1。
>
> 在 3 GHz CPU 上: 1 cycle ≈ 0.33ns。rdtsc 精度 ~1 cycle ≈ 0.3ns。`chrono` 的精度通常 ~100ns。
>
> **为什么需要 `_mm_lfence()`？**
>
> 现代 CPU 是**乱序执行** (Out-of-Order Execution) 的——CPU 为了提高吞吐量，可能调换指令的执行顺序:
> ```
> 可能的乱序: rdtsc → get() → rdtsc  ← 正确
>             get() → rdtsc → rdtsc  ← rdtsc 提前，测量不准
> ```
> `lfence` (Load Fence) 是内存屏障，强制之前的所有指令完成后才继续:
> ```
> lfence → rdtsc → lfence → get() → lfence → rdtsc → lfence
>            t0                                  t1
> ```
> 保证 `t0` 在 `get()` 之前采样，`t1` 在 `get()` 之后采样。

---

## 实验场景

### 场景 1: 顺序 vs 随机访问

**顺序**:
```cpp
for (uint16_t i = 0; i < 200; ++i) {
    auto& tf = pool.get(i);
    sink += tf.x;
}
```

**随机**:
```cpp
std::shuffle(random_order.begin(), random_order.end(), rng);
for (auto idx : random_order) {
    auto& tf = pool.get(idx);
    sink += tf.x;
}
```

**预期结果**:
```
顺序: Sparse[0], Sparse[1], ... → 同一 Cache Line 内连续访问 → 预取器完美命中
随机: Sparse[7000], Sparse[200], ... → 跨越不同 Cache Line → 预取器失效

随机/顺序 比值 ≈ 1.5x-3x
```

> **`volatile float sink`**: 累加结果存入 `volatile`，防止编译器把整个循环优化成编译期常量。

---

### 场景 2: 分散索引

200 个实体的 index 在 [0, 16384) 中随机分布。Dense 紧密但 Sparse 访问分散 → 更多 Cache Line 被触及。

---

### 场景 3: Cold vs Hot 访问

```
flush_cache();
cold_access: pool.get(7777)  → ~150-300 cycles (DRAM)
hot_access:  pool.get(7777)  → ~3-5 cycles (L1)
ratio: ~30-100x
```

这个比值直观反映了 Cache 的加速效果。如果 cold/hot < 10，说明 L3 命中（DRAM miss 被 L3 捕获）。如果 > 50，说明 DRAM miss。

---

## 文件级总结

| 实验 | 测量什么 | 预期 |
|------|---------|------|
| 顺序 vs 随机 | 硬件预取器的效果 | 随机慢 1.5-3x |
| 紧凑 vs 分散索引 | Sparse 数组的 Cache 效率 | 分散时 Sparse 跨越更多 Cache Line |
| 双池交替 | 工作集膨胀的影响 | 两个 Sparse=64KB，可能超 L1 |
| Cold vs Hot | Cache 的绝对加速比 | 30-100x |
