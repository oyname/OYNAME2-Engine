#pragma once
#include <cstdint>
#include <string>

// ---------------------------------------------------------------------------
// GDXTextureResource — GPU-Textur im ResourceStore.
//
// Besitz-Strategie: KEIN Destruktor-Callback.
// GPU-Cleanup passiert ausschliesslich explizit in GDXECSRenderer::Shutdown()
// via m_texStore.ForEach(). Das vermeidet jeglichen Double-Free durch Move-
// Semantik, ResourceStore-interne Kopien oder Shutdown-Reihenfolge.
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
    Procedural,
};

struct GDXTextureResource
{
    void* srv = nullptr;   // ID3D11ShaderResourceView* — nicht owned hier
    uint32_t     width = 0u;
    uint32_t     height = 0u;
    bool         ready = false;
    bool         isSRGB = false;
    GDXTextureSemantic semantic = GDXTextureSemantic::Unknown;
    std::wstring debugName;

    // Kein Destruktor-Cleanup — kein Double-Free moeglich.
    GDXTextureResource() = default;
    ~GDXTextureResource() = default;

    GDXTextureResource(const GDXTextureResource&) = delete;
    GDXTextureResource& operator=(const GDXTextureResource&) = delete;

    // Default-Move reicht: srv ist ein raw pointer, wird einfach uebertragen.
    // Cleanup obliegt dem Renderer, nicht dem Destruktor.
    GDXTextureResource(GDXTextureResource&&) = default;
    GDXTextureResource& operator=(GDXTextureResource&&) = default;
};

// ---------------------------------------------------------------------------
// Vordefinierte Fallback-Textur-Indices (nach Initialize gueltig).
// ---------------------------------------------------------------------------
enum class GDXDefaultTexture : uint32_t
{
    White = 0,
    FlatNormal = 1,
    ORM = 2,
    Black = 3,
};
