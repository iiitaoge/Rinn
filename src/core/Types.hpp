#pragma once
#include <cstdint>
#include <limits>
#include <bitset> // <--- 必须加这个
#include <algorithm>	// 实现 fill
#include <array>		// 为了内存连续
#include <vector>
#include <cassert>
#include <utility>  // 包含 std::forward
#include <concepts> // 确保构造的时候参数合法，能够造出 T
#include <optional>		// 为了实现 “空返回”
#include <bit>			// 为了实现快速  实体销毁组件

// 1. 定义实体 ID
// -------------------------------------------------------------------------
    // 实体句柄 (Entity Handle) - 32位值类型
    // -------------------------------------------------------------------------
    // 布局：[ Generation (16 bits) | Index (16 bits) ]
    // -------------------------------------------------------------------------
struct Entity {
    // 唯一的成员变量：32位无符号整数
    uint32_t id = 0;

    // 掩码常量 (Compile-time constants)
    static constexpr uint32_t INDEX_MASK = 0xFFFF;
    static constexpr uint32_t GENERATION_SHIFT = 16;
    static constexpr uint32_t NULL_ID = 0xFFFFFFFF;

    // 默认构造：创建一个无效实体
    constexpr Entity() : id(NULL_ID) {}

    // 内部构造：由 Registry 使用
    constexpr Entity(uint16_t index, uint16_t generation) {
        id = (static_cast<uint32_t>(generation) << GENERATION_SHIFT) | index;
    }

    // 1. 获取索引 (用于数组寻址) -> O(1) 位运算
    // 如果可能，请在编译期算，运行期算也可以
    [[nodiscard]] constexpr uint16_t index() const noexcept {
        return static_cast<uint16_t>(id & INDEX_MASK);
    }

    // 2. 获取版本 (用于生存检查) -> O(1) 位运算
    [[nodiscard]] constexpr uint16_t generation() const noexcept {
        return static_cast<uint16_t>(id >> GENERATION_SHIFT);
    }

    // 3. 检查是否为 Null
    [[nodiscard]] constexpr bool is_null() const noexcept {
        return id == NULL_ID;
    }

    // 4. 支持 C++20 默认比较 (==, !=)
    // 虽然使用场景不多，常用的是index和generation，但未来可能用上
    // 传值引用 比指针 引用 更快
    friend auto operator<=>(Entity, Entity) = default;  

    // 5. 支持作为 Map 的 Key (如果是 std::map)
    // 但我们在 ECS 里通常不用 map，而是用 sparse set
};

// 为了支持 std::unordered_map (如果有必要的话，尽管不推荐)
// 还需要特化 std::hash，但暂时不需要写


// 2. 定义实体索引
using Entity_index = std::uint16_t;
using Entity_generation = std::uint16_t;


// 2. 无效实体组件号 (Modern C++ 写法，检查Sparse数组中该实体有无对应组件)
// max() 通常是 0xFFFFFFFF
constexpr Entity_index NULL_COMPONENT_ENTITY = std::numeric_limits<Entity_index>::max();

// 3. 组件 ID 类型
using Component_ID = std::uint8_t; // 64个组件用 uint8 就够了(0-255)，省内存

// 4. 数量限制（L1 cache）
constexpr Entity_index MAX_ENTITIES = 16384;
constexpr Component_ID MAX_COMPONENTS = 64;

// 5. 签名 (Signature)
// std::bitset<64> 占用 8 字节，非常紧凑
using Signature = std::bitset<MAX_COMPONENTS>;