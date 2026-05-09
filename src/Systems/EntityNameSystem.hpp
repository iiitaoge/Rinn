#pragma once
#include "../Core/Registry.hpp"
#include "../Components/Components.hpp"
#include <string>

namespace Rinn::EntityNameSystem {

    inline Registry* g_reg = nullptr;

    inline void Init(Registry& reg) {
        g_reg = &reg;
    }

    inline std::string NameOrId(Entity e) {
        if (e.is_null()) return "-";
        if (g_reg && g_reg->is_alive(e)) {
            if (auto opt = g_reg->try_get<IdentityComponent>(e); opt.has_value()) {
                const auto& id = opt->get();
                if (id.name[0] != '\0') return id.name;
            }
        }
        return std::string("E") + std::to_string(e.index());
    }

    inline std::string DisplayName(Entity e) {
        if (e.is_null()) return "-";
        if (g_reg && g_reg->is_alive(e)) {
            if (auto opt = g_reg->try_get<IdentityComponent>(e); opt.has_value()) {
                const auto& id = opt->get();
                if (id.display_name[0] != '\0') return id.display_name;
                if (id.name[0] != '\0') return id.name;
            }
        }
        return std::string("E") + std::to_string(e.index());
    }
}
