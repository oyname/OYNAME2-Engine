#pragma once

#include <array>
#include <cstddef>
#include "Events.h"

class GDXInput
{
public:
    static void BeginFrame();
    static void OnEvent(const Event& e);

    static bool KeyDown(Key key);
    static bool KeyHit(Key key);
    static bool KeyReleased(Key key);

private:
    static constexpr std::size_t KEY_COUNT = 256;

    static std::size_t ToIndex(Key key);

    static std::array<bool, KEY_COUNT> s_down;
    static std::array<bool, KEY_COUNT> s_hit;
    static std::array<bool, KEY_COUNT> s_released;
};
