#pragma once

#include <cstdint>
#include <cstddef>

enum class ShaderPassType : uint8_t
{
    Main   = 0,
    Shadow = 1,
};

enum ShaderVariantFeature : uint32_t
{
    SVF_NONE         = 0u,
    SVF_SKINNED      = 1u << 0,
    SVF_ALPHA_TEST   = 1u << 1,
    SVF_TRANSPARENT  = 1u << 2,
    SVF_VERTEX_COLOR = 1u << 3,
    SVF_NORMAL_MAP   = 1u << 4,
    SVF_UNLIT        = 1u << 5,
};

struct ShaderVariantKey
{
    ShaderPassType pass = ShaderPassType::Main;
    uint32_t vertexFlags = 0u;
    uint32_t features = 0u;

    bool operator==(const ShaderVariantKey& o) const noexcept
    {
        return pass == o.pass
            && vertexFlags == o.vertexFlags
            && features == o.features;
    }
};

struct ShaderVariantKeyHash
{
    size_t operator()(const ShaderVariantKey& k) const noexcept
    {
        const uint64_t a = static_cast<uint64_t>(static_cast<uint8_t>(k.pass));
        const uint64_t b = static_cast<uint64_t>(k.vertexFlags);
        const uint64_t c = static_cast<uint64_t>(k.features);
        uint64_t h = 1469598103934665603ull;
        auto mix = [&](uint64_t v)
        {
            h ^= v;
            h *= 1099511628211ull;
        };
        mix(a); mix(b); mix(c);
        return static_cast<size_t>(h);
    }
};
