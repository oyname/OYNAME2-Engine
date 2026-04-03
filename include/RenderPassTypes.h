#pragma once

#include <cstdint>

enum class RenderPass : uint8_t
{
    Shadow               = 0,
    Opaque               = 1,
    Transparent          = 2,
    ParticlesTransparent = 3,
    Distortion           = 4,
    Depth                = 5,
    MotionVectors        = 6,
};
