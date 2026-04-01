#pragma once

#include "Core/GDXMath.h"

#include <cstdint>

// Backend-neutrales, logisches CPU-Materialmodell.
// Keine GPU-Flags, kein CBuffer-Layout, keine DX11-Abhängigkeit.

enum class BlendMode : uint8_t
{
    Opaque = 0,
    AlphaBlend = 1,
};

// Werte absichtlich stabil gehalten, damit Backend-Mapping und Shader-Konvertierung
// eindeutig bleiben können.
enum class MaterialShadingModel : uint8_t
{
    // Legacy-/Kompatibilitaetspfad. Nicht als gleichrangiges Hauptmodell behandeln.
    Phong = 0,
    PBR   = 1,
};

struct MaterialLegacyPhongParams
{
    Float4 specularColor = { 0.5f, 0.5f, 0.5f, 1.0f };
    float shininess      = 32.0f;
};

enum class MaterialDetailBlendMode : uint8_t
{
    Multiply2x = 0,
    Multiply   = 1,
    Add        = 2,
    Lerp       = 3,
};

struct MaterialParams
{
    // Basisfarbe / Hauptfarbmodulation.
    Float4 baseColor            = { 1.0f, 1.0f, 1.0f, 1.0f };

    // Emissive-Farbe.
    Float4 emissiveColor        = { 0.0f, 0.0f, 0.0f, 1.0f };

    // UV-Transformationen: {tileX, tileY, offsetX, offsetY}.
    Float4 uvTilingOffset       = { 1.0f, 1.0f, 0.0f, 0.0f };
    Float4 uvDetailTilingOffset = { 1.0f, 1.0f, 0.0f, 0.0f };
    Float4 uvNormalTilingOffset = { 1.0f, 1.0f, 0.0f, 0.0f };

    // PBR-Parameter.
    float metallic              = 0.0f;
    float roughness             = 0.5f;
    float normalScale           = 1.0f;
    float occlusionStrength     = 1.0f;

    // Oberflächenverhalten.
    float opacity               = 1.0f; // 1 = opak, 0 = transparent
    float alphaCutoff           = 0.5f;
    float blendFactor           = 0.0f; // Relevant für DetailBlendMode::Lerp

    // Rein material-/oberflächenbezogene Zustände.
    bool unlit                  = false;

    // Logische Betriebsmodi.
    MaterialShadingModel shadingModel         = MaterialShadingModel::PBR;
    MaterialDetailBlendMode detailBlendMode   = MaterialDetailBlendMode::Multiply2x;

    // Legacy-/Kompatibilitaetspfad separat gehalten, damit PBR der Hauptpfad bleibt.
    MaterialLegacyPhongParams legacyPhong{};
};

// Render-/Pipeline-Policy bewusst getrennt von den eigentlichen Materialdaten.
struct MaterialRenderPolicy
{
    bool receiveShadows = true;
    bool doubleSided    = false;
    bool alphaTest      = false;
    BlendMode blendMode = BlendMode::Opaque;
};
