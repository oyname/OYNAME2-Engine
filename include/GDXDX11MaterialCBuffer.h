#pragma once

#include <cstdint>
#include "Core/GDXMath.h"

// DX11-internes GPU-Abbild für den Material-Constant-Buffer.
// Muss byte-genau mit shader/include/CBuffer_Material.hlsli übereinstimmen.
// Nicht im Public API verwenden.
//
// Offset-Tabelle (Byte):
//   0   gBaseColor
//   16  gLegacySpecularColor
//   32  gEmissiveColor
//   48  gUVTilingOffset
//   64  gUVDetailTilingOffset
//   80  gUVNormalTilingOffset
//   96  gMetallic
//   100 gRoughness
//   104 gNormalScale
//   108 gOcclusionStrength
//   112 gLegacyShininess
//   116 gOpacity
//   120 gAlphaCutoff
//   124 _matPad1
//   128 gDetailBlendMode
//   132 gBlendFactor
//   136 gFlags
//   140 _matPad0
struct MaterialCBuffer
{
    Float4   gBaseColor;            // MaterialParams::baseColor
    Float4   gLegacySpecularColor;  // MaterialParams::legacyPhong.specularColor
    Float4   gEmissiveColor;        // MaterialParams::emissiveColor
    Float4   gUVTilingOffset;       // MaterialParams::uvTilingOffset
    Float4   gUVDetailTilingOffset; // MaterialParams::uvDetailTilingOffset
    Float4   gUVNormalTilingOffset; // MaterialParams::uvNormalTilingOffset

    float    gMetallic;             // MaterialParams::metallic
    float    gRoughness;            // MaterialParams::roughness
    float    gNormalScale;          // MaterialParams::normalScale
    float    gOcclusionStrength;    // MaterialParams::occlusionStrength

    float    gLegacyShininess;      // MaterialParams::legacyPhong.shininess
    float    gOpacity;              // MaterialParams::opacity
    float    gAlphaCutoff;          // MaterialParams::alphaCutoff
    float    _matPad1;              // reserviert; receiveShadows liegt jetzt in MaterialRenderPolicy und wird in gFlags serialisiert

    uint32_t gDetailBlendMode;      // MaterialParams::detailBlendMode (als uint32_t serialisiert)
    float    gBlendFactor;          // MaterialParams::blendFactor
    uint32_t gFlags;                // aus MaterialParams + MaterialRenderPolicy durch ToGPU() aufgebaut
    float    _matPad0;              // Padding für 16-Byte-CBuffer-Registerende
};

static_assert(sizeof(MaterialCBuffer) == 144, "MaterialCBuffer muss exakt 144 Byte groß sein.");
