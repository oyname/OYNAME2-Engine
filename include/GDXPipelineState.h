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
    LineList     = 1,
};

enum class GDXFillMode : uint8_t
{
    Solid = 0,
    Wireframe = 1,
};

struct GDXRasterStateDesc
{
    GDXCullMode cullMode = GDXCullMode::Back;
    GDXFillMode fillMode = GDXFillMode::Solid;
    bool frontCounterClockwise = false;
};

struct GDXBlendStateDesc
{
    GDXBlendMode colorBlend = GDXBlendMode::Opaque;
    bool alphaToCoverageEnabled = false;

    bool IsTransparent() const noexcept
    {
        return colorBlend == GDXBlendMode::AlphaBlend;
    }
};

struct GDXDepthStencilStateDesc
{
    GDXDepthMode depthMode = GDXDepthMode::ReadWrite;
    bool depthTestEnabled = true;
    bool depthWriteEnabled = true;
};

struct GDXPipelineStateDesc
{
    GDXBlendStateDesc blend{};
    GDXRasterStateDesc raster{};
    GDXDepthStencilStateDesc depthStencil{};
    bool alphaTestEnabled = false;
    GDXPrimitiveTopology topology = GDXPrimitiveTopology::TriangleList;

    GDXBlendMode& blendMode = blend.colorBlend;
    GDXCullMode& cullMode = raster.cullMode;
    GDXDepthMode& depthMode = depthStencil.depthMode;
    bool& depthTestEnabled = depthStencil.depthTestEnabled;

    GDXPipelineStateDesc() = default;
    GDXPipelineStateDesc(const GDXPipelineStateDesc& other)
        : blend(other.blend)
        , raster(other.raster)
        , depthStencil(other.depthStencil)
        , alphaTestEnabled(other.alphaTestEnabled)
        , topology(other.topology)
        , blendMode(blend.colorBlend)
        , cullMode(raster.cullMode)
        , depthMode(depthStencil.depthMode)
        , depthTestEnabled(depthStencil.depthTestEnabled)
    {
    }

    GDXPipelineStateDesc& operator=(const GDXPipelineStateDesc& other)
    {
        if (this == &other)
            return *this;
        blend = other.blend;
        raster = other.raster;
        depthStencil = other.depthStencil;
        alphaTestEnabled = other.alphaTestEnabled;
        topology = other.topology;
        return *this;
    }

    bool IsTransparent() const noexcept
    {
        return blend.IsTransparent();
    }
};

struct GDXPipelineStateKey
{
    uint32_t value = 0u;

    static GDXPipelineStateKey FromDesc(const GDXPipelineStateDesc& desc) noexcept
    {
        GDXPipelineStateKey key;
        key.value = 0u;
        key.value |= (static_cast<uint32_t>(desc.blend.colorBlend) & 0x3u) << 0;
        key.value |= (static_cast<uint32_t>(desc.raster.cullMode) & 0x3u) << 2;
        key.value |= (static_cast<uint32_t>(desc.depthStencil.depthMode) & 0x3u) << 4;
        key.value |= desc.depthStencil.depthTestEnabled ? (1u << 6) : 0u;
        key.value |= desc.alphaTestEnabled ? (1u << 7) : 0u;
        key.value |= (static_cast<uint32_t>(desc.topology) & 0x3u) << 8;
        key.value |= desc.depthStencil.depthWriteEnabled ? (1u << 10) : 0u;
        key.value |= (static_cast<uint32_t>(desc.raster.fillMode) & 0x3u) << 11;
        key.value |= desc.blend.alphaToCoverageEnabled ? (1u << 13) : 0u;
        key.value |= desc.raster.frontCounterClockwise ? (1u << 14) : 0u;
        return key;
    }
};
