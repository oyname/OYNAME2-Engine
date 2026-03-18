#pragma once

#include "Handle.h"
#include "GDXTextureResource.h"
#include <cstdint>
#include <string>

// ---------------------------------------------------------------------------
// GDXRenderTargetResource — backend-neutrales Render-Target-Resource im Store.
//
// Besitzt nur opaque native Pointer. Die konkrete API (DX11/OpenGL/Vulkan/DX12)
// lebt ausschließlich im Backend. Die Engine arbeitet nur mit Handle + Größen
// + freigegebener Shader-Read-Textur.
// ---------------------------------------------------------------------------
struct GDXRenderTargetResource
{
    void* colorTexture = nullptr;   // API-spezifische Farbtextur
    void* rtv = nullptr;            // RenderTarget-View / FBO / ImageView
    void* srv = nullptr;            // Shader-Read-View / Texture-Handle
    void* depthTexture = nullptr;   // API-spezifische Depth-Textur
    void* dsv = nullptr;            // DepthStencil-View / Depth-Attachment

    uint32_t width = 0u;
    uint32_t height = 0u;
    bool ready = false;
    GDXTextureFormat colorFormat = GDXTextureFormat::RGBA8_UNORM;

    // Farbausgabe des RenderTargets als normale Engine-Textur nutzbar.
    TextureHandle exposedTexture = TextureHandle::Invalid();

    std::wstring debugName;

    GDXRenderTargetResource() = default;
    ~GDXRenderTargetResource() = default;

    GDXRenderTargetResource(const GDXRenderTargetResource&) = delete;
    GDXRenderTargetResource& operator=(const GDXRenderTargetResource&) = delete;
    GDXRenderTargetResource(GDXRenderTargetResource&&) = default;
    GDXRenderTargetResource& operator=(GDXRenderTargetResource&&) = default;
};
