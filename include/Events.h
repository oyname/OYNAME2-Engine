#pragma once

#include <variant>

enum class Key
{
    Unknown,
    Escape, Space,
    A, D, S, W,
    Left, Right, Up, Down
};

struct QuitEvent {};

struct WindowResizedEvent { int width; int height; };
struct KeyPressedEvent    { Key key; bool repeat; };
struct KeyReleasedEvent   { Key key; };

using Event = std::variant<QuitEvent,WindowResizedEvent,KeyPressedEvent,KeyReleasedEvent>;
