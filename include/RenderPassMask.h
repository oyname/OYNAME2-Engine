#pragma once

#include <cstdint>

// Explicit frontend-level pass classification. This is intentionally separate
// from backend render-pass nodes and from material shader selection.
enum class DrawPassType : uint8_t
{
    Depth         = 0,
    Opaque        = 1,
    Transparent   = 2,
    ShadowDepth   = 3,
    MotionVectors = 4,
    Distortion    = 5,
    Particles     = 6,
};

using DrawPassMask = uint32_t;

namespace DrawPassBits
{
    static constexpr DrawPassMask None          = 0u;
    static constexpr DrawPassMask Depth         = 1u << static_cast<uint32_t>(DrawPassType::Depth);
    static constexpr DrawPassMask Opaque        = 1u << static_cast<uint32_t>(DrawPassType::Opaque);
    static constexpr DrawPassMask Transparent   = 1u << static_cast<uint32_t>(DrawPassType::Transparent);
    static constexpr DrawPassMask ShadowDepth   = 1u << static_cast<uint32_t>(DrawPassType::ShadowDepth);
    static constexpr DrawPassMask MotionVectors = 1u << static_cast<uint32_t>(DrawPassType::MotionVectors);
    static constexpr DrawPassMask Distortion    = 1u << static_cast<uint32_t>(DrawPassType::Distortion);
    static constexpr DrawPassMask Particles     = 1u << static_cast<uint32_t>(DrawPassType::Particles);

    static constexpr DrawPassMask MainScene = Opaque | Transparent | Particles;
    static constexpr DrawPassMask AllGraphics = Depth | Opaque | Transparent | ShadowDepth | MotionVectors | Distortion | Particles;
};

constexpr DrawPassMask DrawPassBit(DrawPassType pass) noexcept
{
    return 1u << static_cast<uint32_t>(pass);
}

constexpr bool HasDrawPass(DrawPassMask mask, DrawPassType pass) noexcept
{
    return (mask & DrawPassBit(pass)) != 0u;
}

constexpr bool HasAnyDrawPass(DrawPassMask mask, DrawPassMask required) noexcept
{
    return (mask & required) != 0u;
}

