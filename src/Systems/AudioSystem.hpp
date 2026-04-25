#pragma once
#include <raylib.h>
#include <string>

namespace Rinn::AudioSystem {
    
    // 我们坚持做减法，全生命周期只维护一首背景音乐，避免引入沉重的资源池。
    static Music current_bgm = { 0 };
    static bool is_bgm_loaded = false;

    inline void Init() {
        InitAudioDevice();
    }

    inline void PlayBGM(const std::string& path) {
        if (is_bgm_loaded) {
            StopMusicStream(current_bgm);
            UnloadMusicStream(current_bgm);
            is_bgm_loaded = false;
        }

        current_bgm = LoadMusicStream(path.c_str());
        // 如果文件不存在或者加载失败，Raylib 会返回空指针级别的对象，并输出警告。
        if (current_bgm.stream.buffer != nullptr) {
            PlayMusicStream(current_bgm);
            is_bgm_loaded = true;
        }
    }

    // 每帧调用以推流数据到硬件缓冲
    inline void Update() {
        if (is_bgm_loaded) {
            UpdateMusicStream(current_bgm);
        }
    }

    inline void Shutdown() {
        if (is_bgm_loaded) {
            StopMusicStream(current_bgm);
            UnloadMusicStream(current_bgm);
            is_bgm_loaded = false;
        }
        CloseAudioDevice();
    }
}
