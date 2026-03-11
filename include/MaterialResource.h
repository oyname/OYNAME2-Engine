#pragma once
#include "Handle.h"

#include <cstdint>
#include <DirectXMath.h>

// ---------------------------------------------------------------------------
// MaterialData — cbuffer-kompatibles PBR-Datenstruct.
//
// DIREKT aus der alten Engine übernommen (Material::MaterialData).
// Layout muss exakt mit dem HLSL cbuffer MaterialBuffer (b2) übereinstimmen.
// Änderungen hier erfordern Änderungen im PixelShader.
//
// 16-Byte-Alignment-Pflicht für DX11 Constant Buffers:
//   Jedes Feld oder jede Feldgruppe muss auf 16-Byte-Grenzen ausgerichtet sein.
//   Felder, die eine 16-Byte-Grenze überspannen, brechen das cbuffer-Layout.
// ---------------------------------------------------------------------------
struct MaterialData
{
    // --- Zeile 1: 16 Byte ---
    DirectX::XMFLOAT4 baseColor = { 1.0f, 1.0f, 1.0f, 1.0f };

    // --- Zeile 2: 16 Byte ---
    DirectX::XMFLOAT4 specularColor = { 0.5f, 0.5f, 0.5f, 1.0f };

    // --- Zeile 3: 16 Byte ---
    DirectX::XMFLOAT4 emissiveColor = { 0.0f, 0.0f, 0.0f, 1.0f };  // .rgb * intensity

    // --- Zeile 4: 16 Byte ---
    DirectX::XMFLOAT4 uvTilingOffset = { 1.0f, 1.0f, 0.0f, 0.0f }; // xy=tiling, zw=offset

    // --- Zeile 5: 16 Byte ---
    float metallic          = 0.0f;   // 0..1
    float roughness         = 0.5f;   // 0..1
    float normalScale       = 1.0f;   // 0..2
    float occlusionStrength = 1.0f;   // 0..1

    // --- Zeile 6: 16 Byte ---
    float    shininess       = 32.0f; // Phong (Legacy)
    float    transparency    = 0.0f;  // 0=opak, 1=voll transparent
    float    alphaCutoff     = 0.5f;  // Alpha-Test-Schwellwert
    float    receiveShadows  = 1.0f;  // 1.0 = ja, 0.0 = nein (als float für den Shader)

    // --- Zeile 7: 16 Byte ---
    float    blendMode   = 0.0f;      // 0=off 1=multiply 2=additive 3=lerp(alpha)
    float    blendFactor = 0.0f;
    uint32_t flags       = 0u;        // MaterialFlags-Bitfeld
    float    _pad0       = 0.0f;

    // Gesamtgröße: 7 × 16 = 112 Byte (cbuffer-konform)
};
static_assert(sizeof(MaterialData) == 112, "MaterialData muss 112 Byte sein (cbuffer-Anforderung)");

// ---------------------------------------------------------------------------
// MaterialFlags — Bitfeld für MaterialData::flags.
// Direkt aus der alten Engine übernommen.
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
};

// ---------------------------------------------------------------------------
// MaterialResource — geteilte Material-Ressource im ResourceStore.
//
// Kein Shader*-Zeiger, keine MaterialGpuData*-Zeiger.
// Alles über Handles referenziert.
//
// Eigentumsmodell:
//   ResourceStore<MaterialResource, MaterialTag> besitzt alle Instanzen.
//   Mehrere Entities können denselben MaterialHandle referenzieren.
//   Der cbuffer (cpuDirty=true → GPU-Upload) gehört dem Material.
// ---------------------------------------------------------------------------
struct MaterialResource
{
    // PBR-Daten (cbuffer-Inhalt)
    MaterialData data;

    // Shader-Referenz (per Handle — kein Zeiger)
    ShaderHandle shader;

    // Textur-Referenzen (per Handle — kein Zeiger)
    TextureHandle albedoTex;
    TextureHandle normalTex;
    TextureHandle ormTex;        // Occlusion/Roughness/Metallic combined
    TextureHandle emissiveTex;

    // Stabile numerische ID für Sort-Key-Berechnung.
    // Wird vom ResourceStore beim Add() gesetzt (Slot-Index genügt).
    uint32_t sortID = 0u;

    // GPU-Seite (Backend-agnostisch)
    void*  gpuConstantBuffer = nullptr;  // ID3D11Buffer* o.ä.
    bool   cpuDirty          = true;     // true → GPU-Upload nötig

    // Hilfsmethoden für MaterialFlags
    bool IsTransparent()  const noexcept { return (data.flags & MF_TRANSPARENT)  != 0u; }
    bool IsAlphaTest()    const noexcept { return (data.flags & MF_ALPHA_TEST)    != 0u; }
    bool IsDoubleSided()  const noexcept { return (data.flags & MF_DOUBLE_SIDED)  != 0u; }
    bool IsUnlit()        const noexcept { return (data.flags & MF_UNLIT)         != 0u; }
    bool UsesPBR()        const noexcept { return (data.flags & MF_SHADING_PBR)  != 0u; }

    void SetFlag(MaterialFlags f, bool on) noexcept
    {
        if (on) data.flags |=  static_cast<uint32_t>(f);
        else    data.flags &= ~static_cast<uint32_t>(f);
        cpuDirty = true;
    }

    // Bequemer Konstruktor für Flat-Color-Materialien (für Tests).
    static MaterialResource FlatColor(float r, float g, float b, float a = 1.0f)
    {
        MaterialResource m;
        m.data.baseColor = { r, g, b, a };
        return m;
    }
};
