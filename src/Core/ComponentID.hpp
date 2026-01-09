#pragma once
#include"Types.hpp"

// 1. 内部计数器：记录当前发到第几号了
// 这是一个普通的类，只存一个静态整数
struct ComponentCounter {
    static Component_ID counter; // 注意：要在 .cpp 里初始化，或者用 inline static (C++17)
};

// C++17 写法：直接在头文件初始化
inline Component_ID ComponentCounter::counter = 0;

// 2. 核心魔法函数：获取类型 T 的唯一 ID
template <typename T>
Component_ID get_component_type_id() {

    // 静态初始化只有第一次会执行，之后遇到都会跳过
    static Component_ID id = ComponentCounter::counter++;
    return id;
}