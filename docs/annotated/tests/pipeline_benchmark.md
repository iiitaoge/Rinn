# pipeline_benchmark.cpp — CPU 流水线行为实测（深度注释版）

> 文件路径: `tests/pipeline_benchmark.cpp`  
> 角色: 对比 ECS SparseSet 遍历与理论最优方案（SoA 标量 / SoA+AVX2）的性能差距。

---

## 文件级设计意图

**核心问题**: SparseSet 的两次间接寻址 (`Sparse[idx] → Dense[pos]`) 比纯线性数组访问（SoA）慢多少？

**实验设计**: 四种方案执行相同操作 `x += vx * dt, y += vy * dt`，比较每实体的 CPU 周期数。

| 方案 | 数据布局 | 指令级并行 | 预期性能 |
|------|---------|-----------|---------|
| ECS 顺序 | SparseSet (间接寻址) | 低 | 基准 |
| ECS 碎片 | SparseSet (乱序) | 最低 | 最慢 |
| SoA 标量 | 纯数组 | 编译器自动向量化 | ~2-5x 于 ECS |
| **SoA + AVX2** | 纯数组 + 手写 SIMD | **最高** | **~5-15x 于 ECS** |

---

## 关键代码分析

### SoA 数据结构

```cpp
struct SoA {
    float* x  = nullptr;
    float* y  = nullptr;
    float* vx = nullptr;
    float* vy = nullptr;
    size_t count = 0;

    void alloc(size_t N) {
        x  = static_cast<float*>(_aligned_malloc(N * sizeof(float), 32));
        // ...
    }
};
```

> **语法知识 — `_aligned_malloc(size, alignment)` (MSVC)**:
>
> 分配对齐的堆内存。标准 `malloc` 只保证 8 或 16 字节对齐。AVX2 指令 `_mm256_load_ps` 要求地址 32 字节对齐——未对齐会触发 `#GP` 异常或性能下降。
>
> **跨平台替代**: 
> - C11: `aligned_alloc(alignment, size)`
> - C++17: `std::aligned_alloc(alignment, size)`
> - POSIX: `posix_memalign(&ptr, alignment, size)`
>
> **必须用 `_aligned_free()` 释放**: 普通 `free()` 不一定能正确释放对齐内存。

---

### SoA 标量更新

```cpp
__declspec(noinline) void update_soa_scalar(SoA& s, float dt) {
    float* __restrict px  = s.x;
    float* __restrict py  = s.y;
    const float* __restrict pvx = s.vx;
    const float* __restrict pvy = s.vy;
    const size_t N = s.count;
    for (size_t i = 0; i < N; i++) {
        px[i] += pvx[i] * dt;
        py[i] += pvy[i] * dt;
    }
}
```

> **语法知识 — `__declspec(noinline)` (MSVC)**:
>
> 禁止编译器内联此函数。内联后 rdtsc 可能会测量到意外的代码（内联展开改变了指令布局）。确保性能测量的对象是函数本身，而非被内联后混在调用者中的代码。

> **语法知识 — `__restrict` 指针**:
>
> C/C++ 扩展（标准 C 中是 `restrict`）。告诉编译器"这些指针不会互相别名（alias）"——即 `px` 和 `pvx` 不指向重叠的内存区域。
>
> **为什么重要？** 没有 `__restrict` 时，编译器必须假设 `px[i]` 的写入可能影响 `pvx[i]` 的值（如果它们指向同一块内存）→ 不能安全地重排或向量化操作。有了 `__restrict`，编译器可以大胆地自动向量化。
>
> **编译器自动向量化**: 这个简单循环在 `-O2` 下，编译器会自动将它转为 SSE/AVX 指令。`__restrict` 帮助它确认安全性。

---

### SoA + 手写 AVX2

```cpp
__declspec(noinline) void update_soa_avx2(SoA& s, float dt) {
    const __m256 dt_vec = _mm256_set1_ps(dt);
    size_t i = 0;
    for (; i + 7 < N; i += 8) {
        __m256 vx = _mm256_load_ps(&s.x[i]);
        __m256 vy = _mm256_load_ps(&s.y[i]);
        __m256 dvx = _mm256_load_ps(&s.vx[i]);
        __m256 dvy = _mm256_load_ps(&s.vy[i]);
        vx = _mm256_fmadd_ps(dvx, dt_vec, vx);
        vy = _mm256_fmadd_ps(dvy, dt_vec, vy);
        _mm256_store_ps(&s.x[i], vx);
        _mm256_store_ps(&s.y[i], vy);
    }
    // 尾部标量处理
    for (; i < N; i++) { s.x[i] += s.vx[i] * dt; s.y[i] += s.vy[i] * dt; }
}
```

> **语法知识 — AVX2 SIMD intrinsics**:
>
> | Intrinsic | 对应指令 | 含义 |
> |-----------|---------|------|
> | `_mm256_set1_ps(f)` | `VBROADCASTSS` | 将 float f 广播到 8 个通道 |
> | `_mm256_load_ps(p)` | `VMOVAPS` | 从对齐地址加载 8 个 float |
> | `_mm256_store_ps(p,v)` | `VMOVAPS` | 存储 8 个 float 到对齐地址 |
> | `_mm256_fmadd_ps(a,b,c)` | `VFMADD213PS` | `a*b+c`，单指令完成乘加 |
>
> **`__m256`**: 256 位 SIMD 寄存器类型。装 8 个 `float` (32×8=256)。一条指令同时处理 8 个实体的物理更新。

> **语法知识 — FMA (Fused Multiply-Add)**:
>
> `a * b + c` 分成两条指令（MUL + ADD）时需要 2 个周期 + 1 次舍入。FMA 合成一条指令:
> - 单周期完成
> - 只有 1 次舍入（精度更高）
> - Intel Haswell+ 和 AMD Zen+ 支持
>
> **吞吐量**: Haswell 有 2 个 FMA 单元，每周期可以处理 2×8=16 个 float 的乘加。

**尾部处理**:
```cpp
for (; i < N; i++) { s.x[i] += s.vx[i] * dt; }
```
当 N 不是 8 的倍数时，剩余 `N % 8` 个元素用标量循环处理。例如 N=103: 主循环处理 96 个(12×8)，尾部处理 7 个。

---

### 性能测量框架

```cpp
constexpr int WARMUP = 200;
constexpr int RUNS   = 2000;
```

| 阶段 | 次数 | 目的 |
|------|------|------|
| Warmup | 200 | CPU 升频到 Turbo 频率 + 分支预测器稳定 + Cache 预热 |
| 测量 | 2000 | 取平均减少噪声（OS 中断、GC、电源管理等干扰） |

> **为什么是 2000 次？** 统计学上，2000 次测量的标准误差 ≈ σ/√2000 ≈ σ/45。如果单次测量的变异系数 (CV) 是 ±10%，2000 次平均后 CV 降到 ~0.2%。

---

## 预期结果

```
性能阶梯（cycles/entity, 越低越好）:

SoA+AVX2:       ~1-2 cycles     ← 理论极限
SoA-Scalar:     ~3-5 cycles     ← 编译器自动向量化
ECS-Sequential: ~8-15 cycles    ← 间接寻址 + 可能的 Cache miss
ECS-Scattered:  ~15-50 cycles   ← Sparse 数组 Cache miss + 随机 Dense 访问
```

**ECS 与理论极限的差距**:
- 间接寻址: 2 次数组查找 (~2-4 cycles)
- 虚函数调用: View 构造时有，遍历时无 (0)
- Cache Miss: Sparse 在 L1 → 0 extra cycles；不在 L1 → +10-50 cycles

结论: 对于当前规模（< 1000 实体），ECS 的开销相对于游戏帧的 16ms 可忽略（总计 < 0.05ms）。只有当实体过万且需要极致性能时，才需要考虑 SoA 或 SIMD 优化。

---

## 文件级总结

| 设计决策 | 选择 | 理由 |
|---------|------|------|
| 测量工具 | rdtsc + lfence | 周期级精度 |
| 对照组 | 4 组 (ECS×2 + SoA×2) | 完整的性能阶梯 |
| SIMD | 手写 AVX2 | 展示理论极限，验证编译器自动化的效果 |
| 内存对齐 | _aligned_malloc(32) | AVX2 的硬性要求 |
