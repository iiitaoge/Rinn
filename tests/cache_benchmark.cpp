#include <iostream>
#include <chrono>
#include <vector>
#include <random>
#include <numeric>
#include <array>
#include <format>
#include <intrin.h>  // __rdtsc, _mm_lfence

// ============================================================================
// Cache Benchmark for Project Rinn's SparseSet
// ============================================================================
// 目的：亲手测量 Sparse[random_idx] → Dense[pos] 两次间接寻址的延迟
// 编译：cl /O2 /EHsc /std:c++20 tests\cache_benchmark.cpp /Fe:cache_bench.exe
// 运行：.\cache_bench.exe
// ============================================================================

static constexpr uint16_t MAX_ENTITIES = 16384;
static constexpr uint16_t NULL_IDX = 0xFFFF;

// 模拟你的 SparseSet 内存布局
struct FakeTransform { float x, y; int layer; };  // 12 bytes

struct SimulatedSparseSet {
    std::array<uint16_t, MAX_ENTITIES> Sparse;
    std::vector<FakeTransform> Dense;

    SimulatedSparseSet() { Sparse.fill(NULL_IDX); }

    void add(uint16_t entity_idx, float x, float y) {
        uint16_t pos = static_cast<uint16_t>(Dense.size());
        Dense.push_back({x, y, 0});
        Sparse[entity_idx] = pos;
    }

    // 和你的 SparseSet::get 完全一致
    FakeTransform& get(uint16_t entity_idx) {
        return Dense[Sparse[entity_idx]];
    }
};

// 高精度计时
struct Timer {
    using Clock = std::chrono::high_resolution_clock;
    Clock::time_point start;
    Timer() : start(Clock::now()) {}
    double elapsed_ns() const {
        return std::chrono::duration<double, std::nano>(Clock::now() - start).count();
    }
};

// 刷新 cache（用大数组遍历把 L1/L2 冲掉）
static volatile char flush_buffer[4 * 1024 * 1024]; // 4MB > L3 per-core
void flush_cache() {
    for (size_t i = 0; i < sizeof(flush_buffer); i += 64) {
        flush_buffer[i] = static_cast<char>(i);
    }
}

int main() {
    std::cout << "============================================\n";
    std::cout << "  Project Rinn - Cache Miss Benchmark\n";
    std::cout << "  Sparse[16384] = " << (MAX_ENTITIES * 2) / 1024 << " KB\n";
    std::cout << "============================================\n\n";

    SimulatedSparseSet pool;
    std::mt19937 rng(42);

    // ---------- 场景1：200个实体，index 连续分配 ----------
    std::cout << "--- Scenario 1: 200 entities, sequential indices ---\n";
    for (uint16_t i = 0; i < 200; ++i) {
        pool.add(i, static_cast<float>(i), 0.0f);
    }

    // 顺序访问
    {
        flush_cache();
        volatile float sink = 0;
        Timer t;
        for (int rep = 0; rep < 10000; ++rep) {
            for (uint16_t i = 0; i < 200; ++i) {
                auto& tf = pool.get(i);
                sink += tf.x;
            }
        }
        double total_ns = t.elapsed_ns();
        double per_get = total_ns / (10000.0 * 200.0);
        std::cout << std::format("  Sequential access: {:.1f} ns/get  ({:.0f} ns total for 200 gets)\n",
                                 per_get, per_get * 200);
    }

    // 随机访问
    {
        std::vector<uint16_t> random_order(200);
        std::iota(random_order.begin(), random_order.end(), 0);
        std::shuffle(random_order.begin(), random_order.end(), rng);

        flush_cache();
        volatile float sink = 0;
        Timer t;
        for (int rep = 0; rep < 10000; ++rep) {
            for (auto idx : random_order) {
                auto& tf = pool.get(idx);
                sink += tf.x;
            }
        }
        double total_ns = t.elapsed_ns();
        double per_get = total_ns / (10000.0 * 200.0);
        std::cout << std::format("  Random access:     {:.1f} ns/get  ({:.0f} ns total for 200 gets)\n",
                                 per_get, per_get * 200);
    }

    // ---------- 场景2：200个实体，index 在 [0,16384) 中随机分散 ----------
    std::cout << "\n--- Scenario 2: 200 entities, scattered indices in [0,16384) ---\n";
    SimulatedSparseSet pool2;
    std::vector<uint16_t> scattered_indices;
    {
        std::vector<uint16_t> all_indices(MAX_ENTITIES);
        std::iota(all_indices.begin(), all_indices.end(), 0);
        std::shuffle(all_indices.begin(), all_indices.end(), rng);
        for (int i = 0; i < 200; ++i) {
            scattered_indices.push_back(all_indices[i]);
            pool2.add(all_indices[i], static_cast<float>(i), 0.0f);
        }
    }

    // 按 entity index 顺序
    {
        auto sorted = scattered_indices;
        std::sort(sorted.begin(), sorted.end());

        flush_cache();
        volatile float sink = 0;
        Timer t;
        for (int rep = 0; rep < 10000; ++rep) {
            for (auto idx : sorted) {
                auto& tf = pool2.get(idx);
                sink += tf.x;
            }
        }
        double total_ns = t.elapsed_ns();
        double per_get = total_ns / (10000.0 * 200.0);
        std::cout << std::format("  Sorted-index:      {:.1f} ns/get\n", per_get);
    }

    // 完全随机顺序
    {
        auto shuffled = scattered_indices;
        std::shuffle(shuffled.begin(), shuffled.end(), rng);

        flush_cache();
        volatile float sink = 0;
        Timer t;
        for (int rep = 0; rep < 10000; ++rep) {
            for (auto idx : shuffled) {
                auto& tf = pool2.get(idx);
                sink += tf.x;
            }
        }
        double total_ns = t.elapsed_ns();
        double per_get = total_ns / (10000.0 * 200.0);
        std::cout << std::format("  Random-order:      {:.1f} ns/get\n", per_get);
    }

    // ---------- 场景3：模拟两个 SparseSet 交替访问（DrawSprites 的模式） ----------
    std::cout << "\n--- Scenario 3: Alternating 2 SparseSets (simulates get<T> + get<S>) ---\n";
    SimulatedSparseSet pool_sprite;
    for (uint16_t i = 0; i < 200; ++i) {
        pool_sprite.add(i, static_cast<float>(i), 0.0f);
    }

    // 单池访问 baseline
    {
        flush_cache();
        volatile float sink = 0;
        Timer t;
        for (int rep = 0; rep < 10000; ++rep) {
            for (uint16_t i = 0; i < 200; ++i) {
                auto& tf = pool.get(i);
                sink += tf.x;
            }
        }
        double total_ns = t.elapsed_ns();
        double per_get = total_ns / (10000.0 * 200.0);
        std::cout << std::format("  Single pool:       {:.1f} ns/get\n", per_get);
    }

    // 双池交替访问（每实体查两个池）
    {
        flush_cache();
        volatile float sink = 0;
        Timer t;
        for (int rep = 0; rep < 10000; ++rep) {
            for (uint16_t i = 0; i < 200; ++i) {
                auto& tf = pool.get(i);
                auto& sp = pool_sprite.get(i);
                sink += tf.x + sp.x;
            }
        }
        double total_ns = t.elapsed_ns();
        double per_get = total_ns / (10000.0 * 200.0 * 2.0); // 2 gets per entity
        std::cout << std::format("  Dual pool alt:     {:.1f} ns/get  (2 gets/entity)\n", per_get);
    }

    // ---------- 场景4：rdtsc 精确测量单次 pointer chase ----------
    std::cout << "\n--- Scenario 4: Single pointer-chase latency (rdtsc) ---\n";
    {
        flush_cache();
        _mm_lfence();

        // 预热一次
        volatile auto warm = pool.get(100);

        // 冷访问：先刷缓存
        flush_cache();
        _mm_lfence();

        unsigned long long t0 = __rdtsc();
        _mm_lfence();
        volatile auto& cold = pool.get(7777 % 200);
        _mm_lfence();
        unsigned long long t1 = __rdtsc();

        std::cout << std::format("  Cold single get(): {} cycles\n", t1 - t0);

        // 热访问：连续访问同一个
        _mm_lfence();
        unsigned long long t2 = __rdtsc();
        _mm_lfence();
        volatile auto& hot = pool.get(7777 % 200);
        _mm_lfence();
        unsigned long long t3 = __rdtsc();

        std::cout << std::format("  Hot  single get(): {} cycles\n", t3 - t2);
        std::cout << std::format("  Cold/Hot ratio:    {:.1f}x\n",
                                 static_cast<double>(t1 - t0) / std::max(1ULL, t3 - t2));
    }

    std::cout << "\n============================================\n";
    std::cout << "  Done. Compare these numbers with the\n";
    std::cout << "  theoretical predictions above.\n";
    std::cout << "============================================\n";
    return 0;
}
