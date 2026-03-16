#pragma once

#include <cstdint>

enum class GDXBlendMode : uint8_t
{
    Opaque = 0,
    AlphaBlend = 1,
};

enum class GDXCullMode : uint8_t
{
    Back = 0,
    None = 1,
};

enum class GDXDepthMode : uint8_t
{
    ReadWrite = 0,
    ReadOnly = 1,
};

enum class GDXPrimitiveTopology : uint8_t
{
    TriangleList = 0,
};

enum class GDXPipelinePassClass : uint8_t
{
    Graphics = 0,
    Shadow = 1,
};

struct GDXPipelineStateDesc
{
    GDXBlendMode blendMode = GDXBlendMode::Opaque;
    GDXCullMode  cullMode = GDXCullMode::Back;
    GDXDepthMode depthMode = GDXDepthMode::ReadWrite;
    GDXPrimitiveTopology topology = GDXPrimitiveTopology::TriangleList;
    GDXPipelinePassClass passClass = GDXPipelinePassClass::Graphics;
    bool depthTestEnabled = true;
    bool alphaTestEnabled = false;

    bool IsTransparent() const noexcept
    {
        return blendMode == GDXBlendMode::AlphaBlend;
    }
};

struct GDXPipelineStateKey
{
    uint32_t value = 0u;

    static GDXPipelineStateKey FromDesc(const GDXPipelineStateDesc& desc) noexcept
    {
        GDXPipelineStateKey key;
        key.value = 0u;
        key.value |= (static_cast<uint32_t>(desc.blendMode) & 0x3u) << 0;
        key.value |= (static_cast<uint32_t>(desc.cullMode) & 0x3u) << 2;
        key.value |= (static_cast<uint32_t>(desc.depthMode) & 0x3u) << 4;
        key.value |= (static_cast<uint32_t>(desc.topology) & 0x3u) << 6;
        key.value |= (static_cast<uint32_t>(desc.passClass) & 0x3u) << 8;
        key.value |= desc.depthTestEnabled ? (1u << 10) : 0u;
        key.value |= desc.alphaTestEnabled ? (1u << 11) : 0u;
        return key;
    }
};
