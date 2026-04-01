#pragma once

#include "MaterialParams.h"
#include "GDXTextureSlots.h"
#include "GDXDX11MaterialCBuffer.h"

#include <cstdint>

inline MaterialCBuffer ToGPU(const MaterialParams& p, const MaterialRenderPolicy& policy, const MaterialTextureLayerArray& layers) noexcept
{
    MaterialCBuffer gpu{};

    gpu.gBaseColor            = p.baseColor;             // MaterialParams::baseColor
    gpu.gLegacySpecularColor  = p.legacyPhong.specularColor;   // MaterialParams::legacyPhong.specularColor
    gpu.gEmissiveColor        = p.emissiveColor;         // MaterialParams::emissiveColor
    gpu.gUVTilingOffset       = p.uvTilingOffset;        // MaterialParams::uvTilingOffset
    gpu.gUVDetailTilingOffset = p.uvDetailTilingOffset;  // MaterialParams::uvDetailTilingOffset
    gpu.gUVNormalTilingOffset = p.uvNormalTilingOffset;  // MaterialParams::uvNormalTilingOffset

    gpu.gMetallic          = p.metallic;             // MaterialParams::metallic
    gpu.gRoughness         = p.roughness;            // MaterialParams::roughness
    gpu.gNormalScale       = p.normalScale;          // MaterialParams::normalScale
    gpu.gOcclusionStrength = p.occlusionStrength;    // MaterialParams::occlusionStrength
    gpu.gLegacyShininess   = p.legacyPhong.shininess;      // MaterialParams::legacyPhong.shininess
    gpu.gOpacity           = p.opacity;              // MaterialParams::opacity
    gpu.gAlphaCutoff       = p.alphaCutoff;          // MaterialParams::alphaCutoff
    gpu._matPad1           = 0.0f;                   // reserviert / kein MaterialParams-Feld

    gpu.gDetailBlendMode = static_cast<uint32_t>(p.detailBlendMode); // MaterialParams::detailBlendMode
    gpu.gBlendFactor     = p.blendFactor;                                // MaterialParams::blendFactor

    uint32_t flags = 0u;

    if (policy.receiveShadows)
    {
        flags |= (1u << 12); // MF_RECEIVE_SHADOWS
    }
    if (policy.doubleSided)
    {
        flags |= (1u << 1);  // MF_DOUBLE_SIDED
    }
    if (p.unlit)
    {
        flags |= (1u << 2);  // MF_UNLIT
    }
    if (policy.alphaTest)
    {
        flags |= (1u << 0);  // MF_ALPHA_TEST
    }
    if (IsMaterialTextureEnabled(layers, MaterialTextureSlot::Normal))
    {
        flags |= (1u << 3);  // MF_USE_NORMAL_MAP
    }
    if (IsMaterialTextureEnabled(layers, MaterialTextureSlot::ORM))
    {
        flags |= (1u << 4);  // MF_USE_ORM_MAP
    }
    if (IsMaterialTextureEnabled(layers, MaterialTextureSlot::Emissive)
        || p.emissiveColor.x != 0.0f
        || p.emissiveColor.y != 0.0f
        || p.emissiveColor.z != 0.0f)
    {
        flags |= (1u << 5);  // MF_USE_EMISSIVE
    }
    if (IsMaterialTextureEnabled(layers, MaterialTextureSlot::Detail))
    {
        flags |= (1u << 11); // MF_USE_DETAIL_MAP
    }
    if (p.shadingModel == MaterialShadingModel::PBR)
    {
        flags |= (1u << 10); // MF_SHADING_PBR
    }

    gpu.gFlags  = flags; // MaterialParams + MaterialRenderPolicy
    gpu._matPad0 = 0.0f; // reserviert / kein MaterialParams-Feld

    return gpu;
}
