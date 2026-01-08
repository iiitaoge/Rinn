#pragma once
#include<cstdint>
#include <limits>
#include <bitset> // <--- 必须加这个

//定义实体
typedef uint32_t Entity;
typedef uint32_t Entity_index;

// 实体数量上限 （不要用全局变量）
constexpr Entity MAX_ENTITIES = 100000;

// 无效实体号
#define NULL_ENTITY  0xFFFFFFFF

// 组件最大数量 (假设 32 种)
constexpr uint8_t MAX_COMPONENTS = 32;

// 每一个实体都有一串二进制位，表示它拥有哪些组件
using Signature = std::bitset<MAX_COMPONENTS>;
