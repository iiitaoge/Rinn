#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <cstdint>
#include <atomic>
#include <new>     // std::hardware_destructive_interference_size

// ============================================================================
// False Sharing Benchmark - 可控对照实验
// ============================================================================
// 编译: cl /O2 /EHsc /std:c++20 false_sharing_bench.cpp
// 运行: false_sharing_bench.exe
// ============================================================================

static constexpr int NUM_ENTITIES = 10000;
static constexpr int NUM_ITERATIONS = 10000;

// ============================================================================
// 场景 A: AoS 布局 (你当前的 Transform)
// ============================================================================
struct Transform_AoS {
    float x, y;
    int layer;
};

// ============================================================================
// 场景 B: Padded (每个 Transform 独占 Cache Line)
// ============================================================================
struct alignas(64) Transform_Padded {
    float x, y;
    int layer;
    // 自动填充到 64 字节
};

// ============================================================================
// 场景 C: SoA (Position 和 Layer 分离)
// ============================================================================
struct Position {
    float x, y;
};

// ============================================================================
// 计时工具
// ============================================================================
using Clock = std::chrono::high_resolution_clock;

template<typename F>
double measure_ns(F&& func) {
    auto start = Clock::now();
    func();
    auto end = Clock::now();
    return std::chrono::duration<double, std::nano>(end - start).count();
}

// ============================================================================
// 测试 1: 单线程基准 (无 False Sharing)
// ============================================================================
void bench_single_thread() {
    std::vector<Transform_AoS> data(NUM_ENTITIES, {0.0f, 0.0f, 0});

    double ns = measure_ns([&] {
        for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
            for (int i = 0; i < NUM_ENTITIES; ++i) {
                data[i].x += 1.0f;
                data[i].y += 0.5f;
            }
        }
    });

    printf("[Single Thread AoS]       %.2f ms  (%.1f ns/entity/iter)\n",
        ns / 1e6, ns / (double)(NUM_ENTITIES * NUM_ITERATIONS));
}

// ============================================================================
// 测试 2: 双线程 AoS (False Sharing!)
// ============================================================================
void bench_false_sharing_aos() {
    std::vector<Transform_AoS> data(NUM_ENTITIES, {0.0f, 0.0f, 0});
    std::atomic<bool> go{false};

    auto physics_work = [&] {
        while (!go.load(std::memory_order_acquire)) {} // spin wait
        for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
            for (int i = 0; i < NUM_ENTITIES; ++i) {
                data[i].x += 1.0f;  // 写 x
                data[i].y += 0.5f;  // 写 y
            }
        }
    };

    auto render_work = [&] {
        while (!go.load(std::memory_order_acquire)) {} // spin wait
        for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
            for (int i = 0; i < NUM_ENTITIES; ++i) {
                data[i].layer = iter & 0xFF;  // 写 layer
            }
        }
    };

    std::thread t0(physics_work);
    std::thread t1(render_work);

    auto start = Clock::now();
    go.store(true, std::memory_order_release);
    t0.join();
    t1.join();
    auto end = Clock::now();

    double ns = std::chrono::duration<double, std::nano>(end - start).count();
    printf("[2-Thread AoS FALSE SHARE] %.2f ms  (%.1f ns/entity/iter)\n",
        ns / 1e6, ns / (double)(NUM_ENTITIES * NUM_ITERATIONS));
}

// ============================================================================
// 测试 3: 双线程 Padded (消除 False Sharing)
// ============================================================================
void bench_padded() {
    std::vector<Transform_Padded> data(NUM_ENTITIES, {0.0f, 0.0f, 0});
    std::atomic<bool> go{false};

    auto physics_work = [&] {
        while (!go.load(std::memory_order_acquire)) {}
        for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
            for (int i = 0; i < NUM_ENTITIES; ++i) {
                data[i].x += 1.0f;
                data[i].y += 0.5f;
            }
        }
    };

    auto render_work = [&] {
        while (!go.load(std::memory_order_acquire)) {}
        for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
            for (int i = 0; i < NUM_ENTITIES; ++i) {
                data[i].layer = iter & 0xFF;
            }
        }
    };

    std::thread t0(physics_work);
    std::thread t1(render_work);

    auto start = Clock::now();
    go.store(true, std::memory_order_release);
    t0.join();
    t1.join();
    auto end = Clock::now();

    double ns = std::chrono::duration<double, std::nano>(end - start).count();
    printf("[2-Thread PADDED 64B]     %.2f ms  (%.1f ns/entity/iter)\n",
        ns / 1e6, ns / (double)(NUM_ENTITIES * NUM_ITERATIONS));
}

// ============================================================================
// 测试 4: 双线程 SoA (最优方案)
// ============================================================================
void bench_soa() {
    std::vector<Position> positions(NUM_ENTITIES, {0.0f, 0.0f});
    std::vector<int> layers(NUM_ENTITIES, 0);
    std::atomic<bool> go{false};

    auto physics_work = [&] {
        while (!go.load(std::memory_order_acquire)) {}
        for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
            for (int i = 0; i < NUM_ENTITIES; ++i) {
                positions[i].x += 1.0f;
                positions[i].y += 0.5f;
            }
        }
    };

    auto render_work = [&] {
        while (!go.load(std::memory_order_acquire)) {}
        for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
            for (int i = 0; i < NUM_ENTITIES; ++i) {
                layers[i] = iter & 0xFF;
            }
        }
    };

    std::thread t0(physics_work);
    std::thread t1(render_work);

    auto start = Clock::now();
    go.store(true, std::memory_order_release);
    t0.join();
    t1.join();
    auto end = Clock::now();

    double ns = std::chrono::duration<double, std::nano>(end - start).count();
    printf("[2-Thread SoA SPLIT]      %.2f ms  (%.1f ns/entity/iter)\n",
        ns / 1e6, ns / (double)(NUM_ENTITIES * NUM_ITERATIONS));
}

// ============================================================================
// Main
// ============================================================================
int main() {
    printf("============================================================\n");
    printf(" False Sharing Benchmark - %d entities x %d iters\n", NUM_ENTITIES, NUM_ITERATIONS);
    printf(" sizeof(Transform_AoS)    = %zu bytes\n", sizeof(Transform_AoS));
    printf(" sizeof(Transform_Padded) = %zu bytes\n", sizeof(Transform_Padded));
    printf(" sizeof(Position)         = %zu bytes\n", sizeof(Position));
    printf(" Cache Line Size          = 64 bytes\n");
    printf(" Transforms per Line (AoS)= %zu\n", 64 / sizeof(Transform_AoS));
    printf("============================================================\n\n");

    // Warm up
    bench_single_thread();
    bench_single_thread();

    printf("\n--- Actual Measurements ---\n\n");

    bench_single_thread();
    bench_false_sharing_aos();
    bench_padded();
    bench_soa();

    printf("\n============================================================\n");
    printf(" 预期结果:\n");
    printf("   AoS False Sharing >> Single Thread (慢 3x-15x)\n");
    printf("   Padded ≈ Single Thread (消除竞争，但浪费内存)\n");
    printf("   SoA ≈ Single Thread (最优: 零竞争 + 零浪费)\n");
    printf("============================================================\n");

    return 0;
}
