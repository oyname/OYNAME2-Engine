#pragma once
#include "Handle.h"
#include "GDXTextureResource.h"
#include <cstdint>
#include <string>

// ---------------------------------------------------------------------------
// GDXRenderTargetResource — CPU-Metadaten eines Render Targets.
//
// Kein GPU-Objekt. Das zugehörige DX11RenderTargetGpu liegt in
// GDXDX11GpuRegistry und ist nur dem Backend zugänglich.
// ---------------------------------------------------------------------------
struct GDXRenderTargetResource
{
    uint32_t         width    = 0u;
    uint32_t         height   = 0u;
    bool             ready    = false;
    GDXTextureFormat colorFormat = GDXTextureFormat::RGBA8_UNORM;
    GDXResourceUsageDesc usageDesc{};

    // Farbausgabe als normale Engine-Textur nutzbar.
    TextureHandle exposedTexture      = TextureHandle::Invalid();
    // Samplebare Depth-Ausgabe als Engine-Textur nutzbar.
    TextureHandle exposedDepthTexture = TextureHandle::Invalid();
    // Samplebare Screen-Normal-Ausgabe als Engine-Textur nutzbar.
    TextureHandle exposedNormalsTexture = TextureHandle::Invalid();

    std::wstring debugName;

    GDXRenderTargetResource() = default;
    ~GDXRenderTargetResource() = default;

    GDXRenderTargetResource(const GDXRenderTargetResource&)            = delete;
    GDXRenderTargetResource& operator=(const GDXRenderTargetResource&) = delete;
    GDXRenderTargetResource(GDXRenderTargetResource&&)                 = default;
    GDXRenderTargetResource& operator=(GDXRenderTargetResource&&)      = default;
};
