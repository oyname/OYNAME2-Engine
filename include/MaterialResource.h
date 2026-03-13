#pragma once
#include "Handle.h"

#include <cstdint>
#include <DirectXMath.h>

// ---------------------------------------------------------------------------
// MaterialData — cbuffer-kompatibles PBR-Datenstruct.
//
// Layout muss exakt mit dem HLSL cbuffer MaterialConstants (b2) übereinstimmen.
// Änderungen hier erfordern Änderungen im PixelShader.
//
// 16-Byte-Alignment-Pflicht für DX11 Constant Buffers.
// ---------------------------------------------------------------------------
struct MaterialData
{
    // --- Zeile 1: 16 Byte ---
    DirectX::XMFLOAT4 baseColor = { 1.0f, 1.0f, 1.0f, 1.0f };

    // --- Zeile 2: 16 Byte ---
    DirectX::XMFLOAT4 specularColor = { 0.5f, 0.5f, 0.5f, 1.0f };

    // --- Zeile 3: 16 Byte ---
    DirectX::XMFLOAT4 emissiveColor = { 0.0f, 0.0f, 0.0f, 1.0f };

    // --- Zeile 4: 16 Byte ---
    DirectX::XMFLOAT4 uvTilingOffset = { 1.0f, 1.0f, 0.0f, 0.0f }; // UV0: xy=tiling, zw=offset

    // --- Zeile 5: 16 Byte ---
    // UV1 / Detail-Map Tiling+Offset — unabhängig von UV0.
    // Wird nur ausgewertet wenn MF_USE_DETAIL_MAP gesetzt.
    DirectX::XMFLOAT4 uvDetailTilingOffset = { 1.0f, 1.0f, 0.0f, 0.0f };

    // --- Zeile 6: 16 Byte ---
    float metallic          = 0.0f;
    float roughness         = 0.5f;
    float normalScale       = 1.0f;
    float occlusionStrength = 1.0f;

    // --- Zeile 7: 16 Byte ---
    float    shininess      = 32.0f;
    float    transparency   = 0.0f;
    float    alphaCutoff    = 0.5f;
    float    receiveShadows = 1.0f;

    // --- Zeile 8: 16 Byte ---
    float    blendMode   = 0.0f;
    float    blendFactor = 0.0f;
    uint32_t flags       = 0u;    // MaterialFlags-Bitfeld
    float    _pad0       = 0.0f;

    // Gesamtgröße: 8 × 16 = 128 Byte (cbuffer-konform)
};
static_assert(sizeof(MaterialData) == 128, "MaterialData muss 128 Byte sein (cbuffer-Anforderung)");

// ---------------------------------------------------------------------------
// MaterialFlags — Bitfeld für MaterialData::flags.
// ---------------------------------------------------------------------------
enum MaterialFlags : uint32_t
{
    MF_NONE              = 0u,
    MF_ALPHA_TEST        = 1u << 0,
    MF_DOUBLE_SIDED      = 1u << 1,
    MF_UNLIT             = 1u << 2,
    MF_USE_NORMAL_MAP    = 1u << 3,
    MF_USE_ORM_MAP       = 1u << 4,
    MF_USE_EMISSIVE      = 1u << 5,
    MF_TRANSPARENT       = 1u << 6,
    MF_USE_OCCLUSION_MAP = 1u << 7,
    MF_USE_ROUGHNESS_MAP = 1u << 8,
    MF_USE_METALLIC_MAP  = 1u << 9,
    MF_SHADING_PBR       = 1u << 10,

    // Detail-Map über UV1 (2. UV-Set).
    // Wenn gesetzt: gDetailMap (t4) wird mit UV1 abgetastet und als
    // 2x-Multiply über Albedo gemischt.
    // Wenn kein UV1-Set im Mesh vorhanden: Fallback auf UV0 (kein Crash,
    // kein sichtbarer Effekt wenn Detail-Textur neutral/grau).
    MF_USE_DETAIL_MAP    = 1u << 11,
};

// ---------------------------------------------------------------------------
// MaterialResource — geteilte Material-Ressource im ResourceStore.
//
// Kein Shader*-Zeiger, keine GPU-Manager-Zeiger.
// Alles über Handles referenziert — ECS-konform.
// ---------------------------------------------------------------------------
struct MaterialResource
{
    // PBR-Daten (cbuffer-Inhalt)
    MaterialData data;

    // Shader-Referenz (per Handle)
    ShaderHandle shader;

    // Textur-Referenzen (per Handle — kein Zeiger)
    TextureHandle albedoTex;
    TextureHandle normalTex;
    TextureHandle ormTex;
    TextureHandle emissiveTex;
    TextureHandle detailTex;   // UV1-Detail-Map — optional, nur wenn MF_USE_DETAIL_MAP

    // Stabile ID für Sort-Key
    uint32_t sortID = 0u;

    // GPU-Seite (backend-agnostisch)
    void* gpuConstantBuffer = nullptr;
    bool  cpuDirty          = true;

    bool IsTransparent() const noexcept { return (data.flags & MF_TRANSPARENT)     != 0u; }
    bool IsAlphaTest()   const noexcept { return (data.flags & MF_ALPHA_TEST)      != 0u; }
    bool IsDoubleSided() const noexcept { return (data.flags & MF_DOUBLE_SIDED)    != 0u; }
    bool IsUnlit()       const noexcept { return (data.flags & MF_UNLIT)           != 0u; }
    bool UsesPBR()       const noexcept { return (data.flags & MF_SHADING_PBR)     != 0u; }
    bool UsesDetailMap() const noexcept { return (data.flags & MF_USE_DETAIL_MAP)  != 0u; }

    void SetFlag(MaterialFlags f, bool on) noexcept
    {
        if (on) data.flags |=  static_cast<uint32_t>(f);
        else    data.flags &= ~static_cast<uint32_t>(f);
        cpuDirty = true;
    }

    // Setzt UV1-Tiling für die Detail-Map (unabhängig von UV0).
    void SetDetailTiling(float tilingX, float tilingY,
                         float offsetX = 0.0f, float offsetY = 0.0f) noexcept
    {
        data.uvDetailTilingOffset = { tilingX, tilingY, offsetX, offsetY };
        cpuDirty = true;
    }

    static MaterialResource FlatColor(float r, float g, float b, float a = 1.0f)
    {
        MaterialResource m;
        m.data.baseColor = { r, g, b, a };
        return m;
    }
};
