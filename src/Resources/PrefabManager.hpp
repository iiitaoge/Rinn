#pragma once
#include "Core/Registry.hpp"
#include "components/Components.hpp"
#include "Resources/ResourceManager.hpp"
#include <string>
#include <unordered_map>
#include <functional>

namespace Rinn {

    // ============================================================================
    // PrefabManager - 预制体管理器
    // ============================================================================
    // 职责：
    //   - 存储预制体定义
    //   - 根据名称创建实体并挂载所有组件
    // ============================================================================
    // 设计原则：
    //   - Lua 通过 spawn("player", x, y) 创建实体
    //   - Lua 不直接调用 emplace，PrefabManager 内部处理
    // ============================================================================

    class PrefabManager {
    public:
        // Prefab 定义：一个函数，接受 Registry 和位置，返回配置好的 Entity
        using PrefabSpawner = std::function<Entity(Registry&, ResourceManager&, float x, float y)>;

    private:
        std::unordered_map<std::string, PrefabSpawner> prefabs;

    public:
        // 注册预制体
        void register_prefab(const std::string& name, PrefabSpawner spawner) {
            prefabs[name] = std::move(spawner);
        }

        // 生成实体
        [[nodiscard]] Entity spawn(Registry& reg, ResourceManager& rm, 
                                   const std::string& name, float x, float y) {
            auto it = prefabs.find(name);
            if (it == prefabs.end()) {
                // Prefab 不存在，返回空实体
                return Entity{};
            }
            return it->second(reg, rm, x, y);
        }

        // 检查预制体是否存在
        [[nodiscard]] bool has_prefab(const std::string& name) const {
            return prefabs.contains(name);
        }
    };

    // ============================================================================
    // 内置预制体定义
    // ============================================================================
    // 可以在 main.cpp 或单独的 PrefabDefinitions.hpp 中调用
    // ============================================================================

    inline void register_default_prefabs(PrefabManager& pm) {
        // Player 预制体
        pm.register_prefab("player", [](Registry& reg, ResourceManager& rm, float x, float y) {
            Entity e = reg.create_entity();
            reg.emplace<Transform>(e, x, y, 0);
            reg.emplace<Velocity>(e, 0.0f, 0.0f);
            // Sprite 需要外部设置 texture_id，这里用默认值
            reg.emplace<Sprite>(e, uint16_t(0), 64.0f, 64.0f);
            return e;
        });

        // 可扩展更多预制体...
    }

}
