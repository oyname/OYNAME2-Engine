#pragma once

#include <variant>

enum class Key
{
    Unknown,
    Escape, Space,
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Left, Right, Up, Down,
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9
};

struct QuitEvent {};

struct WindowResizedEvent { int width; int height; };
struct KeyPressedEvent { Key key; bool repeat; };
struct KeyReleasedEvent { Key key; };

using Event = std::variant<QuitEvent, WindowResizedEvent, KeyPressedEvent, KeyReleasedEvent>;
