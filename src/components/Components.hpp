#pragma once
#include "../Core/Types.hpp"
#include <raylib.h>
#include <array>
#include <bitset>
#include <type_traits>

namespace Rinn{
    
    // 渲染，目前 位置 + 精灵 预留 层级
    // 任意空间变换
    struct Transform {
        float x, y;
        int layer = 0;  // ← 预留层级字段，但暂时不用 画的顺序决定了谁遮挡谁
    };
    // Sprite = Image + 行为能力
    // Components.hpp
    struct Sprite {
        size_t texture_id;    // 依然指代那一整张大图（Assets中的 TX Props.png）
        size_t normal_id = 0; // 法线贴图 ID，0 = 无法线
        float width, height;  // 最终画在屏幕上的大小

        // 截取信息（从大图中切那一小块的坐标和尺寸）
        float src_x = 0;
        float src_y = 0;
        float src_w = 0;
        float src_h = 0;
        bool is_ground = false; // 地面瓦片：水平铺在 XZ 平面
    };

    // 专属轻量级 UI 气泡组件，保证了 0 指针引用
    struct TextBubble {
        char text[256] = { 0 };
        float display_time = 0.0f;
    };

    struct IdentityComponent {
        char name[32] = { 0 };
        char display_name[32] = { 0 };
    };

    struct Velocity {
        float vx, vy;
    };

    // 碰撞器组件
    struct Collider {
        float width, height;          // 碰撞盒尺寸
        float offset_x = 0.0f;        // 相对 Transform 的偏移
        float offset_y = 0.0f;
        uint16_t layer = 0x0001;      // 我属于哪一层（位掩码）
        uint16_t mask  = 0xFFFF;      // 我和哪些层发生碰撞（位掩码）
    };

    // ─── NPC 自身 ───

    struct NeedComponent {
        static constexpr int N = 6;  // 资源/社交/亲情/安全/信仰/好奇心
        std::array<float, N> weights;        // 稳态人格
        std::array<float, N> satisfaction;   // 当前满足度
        std::array<float, N> expectation;    // 对未来满足度的预测
    };

    struct EmotionComponent {
        static constexpr int E = 5;  // 愤怒/焦虑/恐慌/悲伤/孤独
        std::array<float, E> intensity;
        std::array<Entity, E> target;        // 情绪指向（可 null）
        std::array<float, E> decay_rate;
    };

    struct DecisionComponent {
        Entity current_action_target;
        uint16_t current_action_id;          // 索引到 Lua action catalog
        float action_progress;
        uint32_t next_decision_tick;
    };

    struct PerceptionComponent {
        static constexpr int INBOX = 16;
        std::array<uint32_t, INBOX> pending; // EventId 队列
        uint8_t head, tail;
    };

    struct StoneTabletComponent {
        bool online;
        Entity owner;
        int8_t broadcast_range;
    };

    // ─── Edge 实体上挂的 ───

    struct RelationComponent {
        Entity from, to;
        int8_t affinity;     // -100..100
        int8_t power_diff;   // from 相对 to 的权力差
    };

    struct KnowledgeFactComponent {
        uint32_t fact_type;       // enum: TaxRaised, SonDied, Rumor, ...
        Entity subject;
        uint32_t timestamp;
        std::bitset<MAX_ENTITIES> knowers;   // ← 信息差的核心
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
    static_assert(std::is_aggregate_v<TextBubble>, "TextBubble must be aggregate");
    static_assert(std::is_aggregate_v<IdentityComponent>, "IdentityComponent must be aggregate");
    static_assert(std::is_aggregate_v<NeedComponent>, "NeedComponent must be aggregate");
    static_assert(std::is_aggregate_v<EmotionComponent>, "EmotionComponent must be aggregate");
    static_assert(std::is_aggregate_v<DecisionComponent>, "DecisionComponent must be aggregate");
    static_assert(std::is_aggregate_v<PerceptionComponent>, "PerceptionComponent must be aggregate");
    static_assert(std::is_aggregate_v<StoneTabletComponent>, "StoneTabletComponent must be aggregate");
    static_assert(std::is_aggregate_v<RelationComponent>, "RelationComponent must be aggregate");
    static_assert(std::is_aggregate_v<KnowledgeFactComponent>, "KnowledgeFactComponent must be aggregate");

    // 可序列化：trivially copyable + standard layout → memcpy 安全
    static_assert(std::is_trivially_copyable_v<Transform>, "Transform must be trivially copyable for serialization");
    static_assert(std::is_trivially_copyable_v<Sprite>,    "Sprite must be trivially copyable for serialization");
    static_assert(std::is_trivially_copyable_v<Velocity>,  "Velocity must be trivially copyable for serialization");
    static_assert(std::is_trivially_copyable_v<Collider>,  "Collider must be trivially copyable for serialization");
    static_assert(std::is_trivially_copyable_v<TextBubble>,"TextBubble must be trivially copyable for serialization");
    static_assert(std::is_trivially_copyable_v<IdentityComponent>,"IdentityComponent must be trivially copyable for serialization");
    static_assert(std::is_trivially_copyable_v<NeedComponent>, "NeedComponent must be trivially copyable for serialization");
    static_assert(std::is_trivially_copyable_v<EmotionComponent>, "EmotionComponent must be trivially copyable for serialization");
    static_assert(std::is_trivially_copyable_v<DecisionComponent>, "DecisionComponent must be trivially copyable for serialization");
    static_assert(std::is_trivially_copyable_v<PerceptionComponent>, "PerceptionComponent must be trivially copyable for serialization");
    static_assert(std::is_trivially_copyable_v<StoneTabletComponent>, "StoneTabletComponent must be trivially copyable for serialization");
    static_assert(std::is_trivially_copyable_v<RelationComponent>, "RelationComponent must be trivially copyable for serialization");
    static_assert(std::is_trivially_copyable_v<KnowledgeFactComponent>, "KnowledgeFactComponent must be trivially copyable for serialization");

    // 标签组件：空类型
    static_assert(std::is_empty_v<IsPlayer>, "IsPlayer must be empty (tag component)");
    static_assert(std::is_empty_v<IsEnemy>,  "IsEnemy must be empty (tag component)");
    static_assert(std::is_empty_v<IsDead>,   "IsDead must be empty (tag component)");
    static_assert(std::is_empty_v<IsStatic>, "IsStatic must be empty (tag component)");
}

