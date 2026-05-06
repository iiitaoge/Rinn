#pragma once
#include "Core/Registry.hpp"

namespace Rinn::EmotionDecaySystem {
    inline void Update(Registry& reg, float dt) {
        for (Entity e : reg.view<EmotionComponent>()) {
            auto& emo = reg.get<EmotionComponent>(e);
            for (int i = 0; i < emo.intensity.size(); i++) {
                emo.intensity[i] -= emo.decay_rate[i] * dt;
                emo.intensity[i] = std::max(0.0f, emo.intensity[i]);
            }
        }
    }
}
