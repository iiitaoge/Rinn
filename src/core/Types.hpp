#pragma once
#include<cstdint>
#include <limits>
#include <bitset> // <--- 必须加这个
#include <algorithm>	// 实现 fill

// 1. 定义实体 ID
using Entity = std::uint32_t;

// 2. 定义实体索引
using Entity_index = std::uint32_t;


// 2. 无效实体号 (Modern C++ 写法，替代 #define)
// max() 通常是 0xFFFFFFFF
constexpr Entity NULL_ENTITY = std::numeric_limits<Entity>::max();

// 3. 组件 ID 类型
using Component_ID = std::uint8_t; // 64个组件用 uint8 就够了(0-255)，省内存

// 4. 数量限制
constexpr Entity MAX_ENTITIES = 100000;
constexpr Component_ID MAX_COMPONENTS = 64;

// 5. 签名 (Signature)
// std::bitset<64> 占用 8 字节，非常紧凑
using Signature = std::bitset<MAX_COMPONENTS>;