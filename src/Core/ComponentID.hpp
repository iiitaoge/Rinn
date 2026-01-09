#pragma once
#include "Types.hpp"
#include <atomic>  // 添加这个

// 1. 内部计数器：记录当前发到第几号了
// 这是一个普通的类，只存一个静态整数
struct ComponentCounter {
    inline static std::atomic<Component_ID> counter{ 0 }; 
};


// 2. 核心魔法函数：获取类型 T 的唯一 ID
template <typename T>
Component_ID get_component_type_id() {

    // 静态初始化只有第一次会执行，之后遇到都会跳过
    // ComponentID.hpp 中的原子操作
    static Component_ID id = ComponentCounter::counter.fetch_add(1, std::memory_order_relaxed);
    return id;
}

// 组件ID和Components_pool中组件池的位置以及签名位置一致对应