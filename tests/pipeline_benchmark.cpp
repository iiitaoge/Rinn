// ============================================================================
// pipeline_benchmark.cpp - CPU 流水线行为实测
// ============================================================================
// 编译 (MSVC, Release, 在项目根目录):
//   cl /O2 /arch:AVX2 /EHsc /std:c++20 /I src tests\pipeline_benchmark.cpp /Fe:bench.exe
// 运行:
//   bench.exe
//
// Intel VTune 采集 (管理员 PowerShell):
//   vtune -collect uarch-exploration -- bench.exe
//   vtune -collect memory-access     -- bench.exe
// ============================================================================

#include <iostream>
#include <vector>
#include <random>
#include <intrin.h>     // __rdtsc, _mm_lfence, _mm_prefetch
#include <immintrin.h>  // AVX2: _mm256_*
#include <format>
#include <algorithm>
#include <cstring>      // memset

#include "Core/World.hpp"
#include "Systems/PhysicsSystem.hpp"

using namespace Rinn;

// ── 精确周期计数器 ──────────────────────────────────────────
struct CycleTimer {
    uint64_t start;
    void begin() { _mm_lfence(); start = __rdtsc(); _mm_lfence(); }
    uint64_t end()  { _mm_lfence(); uint64_t e = __rdtsc(); _mm_lfence(); return e - start; }
};

// ── 场景 A: 顺序创建 (最佳 Cache 行为) ─────────────────────
void setup_sequential(Registry& reg, int N) {
    for (int i = 0; i < N; i++) {
        auto e = reg.create_entity();
        reg.emplace<Transform>(e, (float)i, (float)i, 0);
        reg.emplace<Velocity>(e, 1.0f, 1.0f);
    }
}

// ── 场景 B: 碎片化 (创建 1.5N, 随机删除 0.5N, 留 N 个散乱实体) ──
// 注意: MAX_ENTITIES=16384, 所以 1.5N 必须 < 16384
void setup_scattered(Registry& reg, int N) {
    int total = N + N / 2;  // 1.5N, 安全上限
    std::vector<Entity> all;
    all.reserve(total);
    for (int i = 0; i < total; i++) {
        auto e = reg.create_entity();
        reg.emplace<Transform>(e, (float)i, (float)i, 0);
        reg.emplace<Velocity>(e, 1.0f, 1.0f);
        all.push_back(e);
    }
    // 随机删除多余的 0.5N 个
    std::mt19937 rng(42);
    std::shuffle(all.begin(), all.end(), rng);
    int to_delete = total - N;
    for (int i = 0; i < to_delete; i++) {
        reg.destroy_entity(all[i]);
    }
}

// ── 场景 C: SoA 标量 (无间接寻址, 纯线性扫描) ──────────────
struct SoA {
    // 32字节对齐, 满足 AVX 要求
    float* x  = nullptr;
    float* y  = nullptr;
    float* vx = nullptr;
    float* vy = nullptr;
    size_t count = 0;

    void alloc(size_t N) {
        free();
        count = N;
        x  = static_cast<float*>(_aligned_malloc(N * sizeof(float), 32));
        y  = static_cast<float*>(_aligned_malloc(N * sizeof(float), 32));
        vx = static_cast<float*>(_aligned_malloc(N * sizeof(float), 32));
        vy = static_cast<float*>(_aligned_malloc(N * sizeof(float), 32));
        for (size_t i = 0; i < N; i++) {
            x[i] = (float)i; y[i] = (float)i;
            vx[i] = 1.0f;    vy[i] = 1.0f;
        }
    }
    void free() {
        if (x) { _aligned_free(x); x = nullptr; }
        if (y) { _aligned_free(y); y = nullptr; }
        if (vx){ _aligned_free(vx); vx = nullptr; }
        if (vy){ _aligned_free(vy); vy = nullptr; }
    }
    ~SoA() { free(); }
};

// SoA 标量版: 编译器自动向量化
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

// ── 场景 D: SoA + 手写 AVX2 (理论极限) ─────────────────────
// 每次处理 8 个 float (256 bit), 用 FMA 指令
__declspec(noinline) void update_soa_avx2(SoA& s, float dt) {
    const size_t N = s.count;
    const __m256 dt_vec = _mm256_set1_ps(dt);

    // 主循环: 每次 8 个实体
    size_t i = 0;
    for (; i + 7 < N; i += 8) {
        __m256 vx = _mm256_load_ps(&s.x[i]);
        __m256 vy = _mm256_load_ps(&s.y[i]);
        __m256 dvx = _mm256_load_ps(&s.vx[i]);
        __m256 dvy = _mm256_load_ps(&s.vy[i]);
        vx = _mm256_fmadd_ps(dvx, dt_vec, vx);  // x += vx * dt
        vy = _mm256_fmadd_ps(dvy, dt_vec, vy);  // y += vy * dt
        _mm256_store_ps(&s.x[i], vx);
        _mm256_store_ps(&s.y[i], vy);
    }
    // 尾部处理
    for (; i < N; i++) {
        s.x[i] += s.vx[i] * dt;
        s.y[i] += s.vy[i] * dt;
    }
}

// ── 通用测量框架 ───────────────────────────────────────────
void run_bench(const char* label, auto setup_fn, auto update_fn, int N) {
    constexpr int WARMUP = 200;
    constexpr int RUNS   = 2000;
    CycleTimer timer;

    setup_fn();

    for (int i = 0; i < WARMUP; i++) update_fn();

    timer.begin();
    for (int i = 0; i < RUNS; i++) update_fn();
    uint64_t total = timer.end();

    double per_run = (double)total / RUNS;
    double per_ent = per_run / N;

    std::cout << std::format("{:<22} N={:<6} | {:>10.0f} cyc/frame | {:>6.1f} cyc/entity\n",
                             label, N, per_run, per_ent);
}

int main() {
    std::cout << "=== Project Rinn Pipeline Benchmark ===\n";
    std::cout << "Measures: ECS (current) vs SoA (scalar) vs SoA+AVX2 (theoretical max)\n";
    std::cout << "Cycles via __rdtsc, WARMUP=200, RUNS=2000\n\n";

    // MAX_ENTITIES=16384, scattered creates 1.5N → cap at 10000
    for (int N : {100, 1000, 5000, 10000}) {
        Registry reg_seq, reg_scat;
        SoA soa;

        run_bench("ECS-Sequential", [&]{ setup_sequential(reg_seq, N); },
                  [&]{ PhysicsSystem::Update(reg_seq, 0.016f); }, N);

        run_bench("ECS-Scattered",  [&]{ setup_scattered(reg_scat, N); },
                  [&]{ PhysicsSystem::Update(reg_scat, 0.016f); }, N);

        run_bench("SoA-Scalar",     [&]{ soa.alloc(N); },
                  [&]{ update_soa_scalar(soa, 0.016f); }, N);

        run_bench("SoA-AVX2",       [&]{ soa.alloc(N); },
                  [&]{ update_soa_avx2(soa, 0.016f); }, N);

        std::cout << "-------------------------------------------------------------------\n";
    }

    std::cout << "\nDone. To profile with VTune:\n";
    std::cout << "  vtune -collect uarch-exploration -- bench.exe\n";
    std::cout << "  Key counters: MEM_LOAD_RETIRED.L1_MISS, BR_MISP_RETIRED.ALL_BRANCHES\n";
    return 0;
}
