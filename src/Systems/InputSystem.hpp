#pragma once
#include "raylib.h"

// ============================================================================
// InputSystem.hpp - 输入系统封装（零运行时开销版）
// ============================================================================
// 设计哲学：
//   - 把字符串映射移到 Lua 加载期（只执行一次）
//   - 运行时直接传递 int 键码（零转换开销）
//   - C++ 层只做最薄的 Raylib API 封装
// ============================================================================

namespace Rinn {
    namespace Input {

        // ====================================================================
        // 键盘 API（直接传 int 键码，零开销）
        // ====================================================================
        [[nodiscard]] inline bool is_key_down(int key) { 
            return IsKeyDown(key); 
        }

        [[nodiscard]] inline bool is_key_pressed(int key) { 
            return IsKeyPressed(key); 
        }

        [[nodiscard]] inline bool is_key_released(int key) { 
            return IsKeyReleased(key); 
        }

        // ====================================================================
        // 鼠标 API
        // ====================================================================
        [[nodiscard]] inline float get_mouse_x() { 
            return GetMousePosition().x; 
        }

        [[nodiscard]] inline float get_mouse_y() { 
            return GetMousePosition().y; 
        }

        [[nodiscard]] inline bool is_mouse_down(int button) { 
            return IsMouseButtonDown(button); 
        }

        [[nodiscard]] inline bool is_mouse_pressed(int button) { 
            return IsMouseButtonPressed(button); 
        }

        [[nodiscard]] inline bool is_mouse_released(int button) { 
            return IsMouseButtonReleased(button); 
        }
    }
}