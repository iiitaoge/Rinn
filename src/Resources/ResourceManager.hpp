#pragma once
#include <unordered_map>
#include <string>
#include <raylib.h>
#include <vector>
#include <cassert>
namespace Rinn {
    class ResourceManager {
        std::vector<Texture2D> textures;    // 纹理资源
        std::unordered_map<std::string, uint16_t> path_to_id;  // 路径 → ID 映射
    public:
        uint16_t load_texture(const std::string& path);  // 返回 ID
        Texture2D& get_texture(uint16_t id);             // O(1) 查找

        void unload_all() {
            for (auto& tex : textures) {
                UnloadTexture(tex);
            }
            textures.clear();
            path_to_id.clear();
        }

        ~ResourceManager() {
            if (!textures.empty()) {
                unload_all();
            }
        }
    };

    // texture相关
    inline uint16_t ResourceManager::load_texture(const std::string& path) {
        // 1. 已加载则直接返回
        auto it = path_to_id.find(path);
        if (it != path_to_id.end()) {
            return it->second;
        }

        // 2. 未加载则加载
        Texture2D tex = LoadTexture(path.c_str());
        uint16_t id = static_cast<uint16_t>(textures.size());
        textures.push_back(tex);
        path_to_id[path] = id;
        return id;
    }

    // 获取纹理资源
    inline Texture2D& ResourceManager::get_texture(uint16_t id) {
        assert(id < textures.size() && "Invalid texture ID");
        return textures[id];
    }
}
