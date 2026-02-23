#pragma once
#include <raylib.h>
#include <type_traits>

namespace Rinn{
    
    // 渲染，目前 位置 + 精灵 预留 层级
    // 任意空间变换
    struct Transform {
        float x, y;
        int layer = 0;  // ← 预留层级字段，但暂时不用 画的顺序决定了谁遮挡谁
    };
    // Sprite = Image + 行为能力
    struct Sprite {
        uint16_t texture_id;  // 2 bytes - 索引到 ResourceManager
        float width, height;
        // 未来扩展：int animationFrame; 
    };

    struct Velocity {
        float vx, vy;
    };

    // 碰撞器组件
    struct Collider {
        float width, height;          // 碰撞盒尺寸
        float offset_x = 0.0f;        // 相对 Transform 的偏移
        float offset_y = 0.0f;
        bool is_trigger = false;      // true = 不阻挡，只触发事件
        bool is_static = false;       // true = 静态物体（优化用）
    };


    // 暂时用的情绪组件
    struct Emotion {
        float fear;
        float anger;
        float greed;
    };

    // ====================================================================
    // 标签组件 (Tag Components) - 零开销标记
    // ====================================================================
    // 标签组件 sizeof == 1 (C++ 最小)，仅靠签名位存在
    // 用于标记实体身份，配合 View 过滤
    struct IsPlayer {};
    struct IsEnemy {};
    struct IsDead {};
    struct IsStatic {};

    // ====================================================================
    // 编译期组件合法性校验
    // ====================================================================
    // 如果某天有人写了带虚函数/指针的组件，这里立刻报错
    // 而不是运行到序列化时才发现 memcpy 出了问题

    // 数据组件：聚合类型 + 非空
    static_assert(std::is_aggregate_v<Transform>,  "Transform must be aggregate (no custom ctor/virtual)");
    static_assert(std::is_aggregate_v<Sprite>,     "Sprite must be aggregate");
    static_assert(std::is_aggregate_v<Velocity>,   "Velocity must be aggregate");
    static_assert(std::is_aggregate_v<Collider>,   "Collider must be aggregate");

    // 可序列化：trivially copyable + standard layout → memcpy 安全
    static_assert(std::is_trivially_copyable_v<Transform>, "Transform must be trivially copyable for serialization");
    static_assert(std::is_trivially_copyable_v<Sprite>,    "Sprite must be trivially copyable for serialization");
    static_assert(std::is_trivially_copyable_v<Velocity>,  "Velocity must be trivially copyable for serialization");
    static_assert(std::is_trivially_copyable_v<Collider>,  "Collider must be trivially copyable for serialization");

    // 标签组件：空类型
    static_assert(std::is_empty_v<IsPlayer>, "IsPlayer must be empty (tag component)");
    static_assert(std::is_empty_v<IsEnemy>,  "IsEnemy must be empty (tag component)");
    static_assert(std::is_empty_v<IsDead>,   "IsDead must be empty (tag component)");
    static_assert(std::is_empty_v<IsStatic>, "IsStatic must be empty (tag component)");
}

