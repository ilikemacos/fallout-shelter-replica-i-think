#pragma once
// Platform-independent input snapshot. The Cocoa window fills this in from
// NSEvent; the game loop only ever reads from here.
#include "core/Math.hpp"
#include <array>
#include <string>

namespace hv::input {

enum class Key : u16 {
    Unknown = 0, A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    Space, Return, Escape, Tab, Backspace, Delete,
    Left, Right, Up, Down,
    LeftShift, RightShift, LeftCmd, RightCmd, LeftOption, RightOption, LeftCtrl, RightCtrl,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    Plus, Minus, Count
};

enum class MouseButton : u8 { Left = 0, Right, Middle, Count };

struct InputState {
    std::array<bool, static_cast<size_t>(Key::Count)> keyDown{};
    std::array<bool, static_cast<size_t>(Key::Count)> keyPressed{};   ///< true for exactly one frame
    std::array<bool, static_cast<size_t>(MouseButton::Count)> mouseDown{};
    std::array<bool, static_cast<size_t>(MouseButton::Count)> mousePressed{};
    std::array<bool, static_cast<size_t>(MouseButton::Count)> mouseReleased{};

    Vec2 mousePos;         ///< points, top-left origin
    Vec2 mouseDelta;
    f32  scrollDelta = 0.0f;
    std::string textInput;  ///< accumulated characters typed this frame (UI text fields)
    bool windowFocused = true;

    bool down(Key k) const { return keyDown[static_cast<size_t>(k)]; }
    bool pressed(Key k) const { return keyPressed[static_cast<size_t>(k)]; }
    bool down(MouseButton b) const { return mouseDown[static_cast<size_t>(b)]; }
    bool pressed(MouseButton b) const { return mousePressed[static_cast<size_t>(b)]; }
    bool released(MouseButton b) const { return mouseReleased[static_cast<size_t>(b)]; }

    /// Called once per frame after the game has read pressed/released edges.
    void endFrame() {
        keyPressed.fill(false);
        mousePressed.fill(false);
        mouseReleased.fill(false);
        mouseDelta = Vec2{0, 0};
        scrollDelta = 0.0f;
        textInput.clear();
    }
};

} // namespace hv::input
