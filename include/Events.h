#pragma once

#include <variant>

enum class Key
{
    Unknown = 0,

    Escape,
    Space,

    Left,
    Right,
    Up,
    Down,

    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,

    Num0, Num1, Num2, Num3, Num4,
    Num5, Num6, Num7, Num8, Num9,

    Plus,
    Minus,
    Equals,

    F1, F2, F3, F4, F5, F6,
    F7, F8, F9, F10, F11, F12
};

struct QuitEvent {};

struct WindowResizedEvent { int width; int height; };
struct KeyPressedEvent { Key key; bool repeat; };
struct KeyReleasedEvent { Key key; };

using Event = std::variant<QuitEvent, WindowResizedEvent, KeyPressedEvent, KeyReleasedEvent>;
