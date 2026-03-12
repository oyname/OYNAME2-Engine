#pragma once

#include <variant>

enum class Key
{
    Unknown,
    Escape, Space,
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Left, Right, Up, Down
};

struct QuitEvent {};

struct WindowResizedEvent { int width; int height; };
struct KeyPressedEvent { Key key; bool repeat; };
struct KeyReleasedEvent { Key key; };

using Event = std::variant<QuitEvent, WindowResizedEvent, KeyPressedEvent, KeyReleasedEvent>;
