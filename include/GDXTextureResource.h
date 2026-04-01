#pragma once
#include <cstdint>
#include <string>
#include "GDXResourceState.h"

// ---------------------------------------------------------------------------
// GDXTextureResource — CPU-Metadaten einer Textur im ResourceStore.
//
// Kein GPU-Objekt. Das zugehörige DX11TextureGpu liegt in
// GDXDX11GpuRegistry und ist nur dem Backend zugänglich.
// ---------------------------------------------------------------------------
enum class GDXTextureSemantic : uint8_t
{
    Unknown,
    Albedo,
    Normal,
    ORM,
    Emissive,
    Detail,
    RenderTarget,
    Depth,
    ScreenNormals,
    Procedural,
};

enum class GDXTextureFormat : uint8_t
{
    Unknown,
    RGBA8_UNORM,
    RGBA8_UNORM_SRGB,
    RGBA16_FLOAT,
};

struct GDXTextureResource
{
    uint32_t           width    = 0u;
    uint32_t           height   = 0u;
    bool               ready    = false;
    bool               isSRGB   = false;
    GDXTextureFormat   format   = GDXTextureFormat::Unknown;
    GDXTextureSemantic semantic = GDXTextureSemantic::Unknown;
    GDXResourceUsageDesc usageDesc{};
    std::wstring       debugName;

    GDXTextureResource() = default;
    ~GDXTextureResource() = default;

    GDXTextureResource(const GDXTextureResource&)            = delete;
    GDXTextureResource& operator=(const GDXTextureResource&) = delete;
    GDXTextureResource(GDXTextureResource&&)                 = default;
    GDXTextureResource& operator=(GDXTextureResource&&)      = default;
};

enum class GDXDefaultTexture : uint32_t
{
    White      = 0,
    FlatNormal = 1,
    ORM        = 2,
    Black      = 3,
};
