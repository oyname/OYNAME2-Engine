#pragma once

#include <cstdint>

enum class ResourceState : uint8_t
{
    Unknown = 0,
    ShaderRead,
    RenderTarget,
    DepthWrite,
    CopySource,
    CopyDest,
    Present,
};
