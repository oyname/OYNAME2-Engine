#pragma once

#include "Handle.h"

#include <array>
#include <cstdint>

enum class MaterialTextureSlot : uint8_t
{
    Albedo = 0,
    Normal = 1,
    ORM = 2,
    Emissive = 3,
    Detail = 4,
    Count = 5,
};

enum class MaterialTextureUVSet : uint8_t
{
    UV0 = 0,
    UV1 = 1,
    Auto = 2,
};

enum class ShaderResourceSemantic : uint8_t
{
    Albedo = 0,
    Normal = 1,
    ORM = 2,
    Emissive = 3,
    Detail = 4,
    ShadowMap = 5,
    Count = 6,
};

struct MaterialTextureLayer
{
    TextureHandle texture = TextureHandle::Invalid();
    MaterialTextureUVSet uvSet = MaterialTextureUVSet::Auto;
    bool enabled = false;
    bool expectsSRGB = false;
    uint32_t reserved = 0u;
};

using MaterialTextureLayerArray = std::array<MaterialTextureLayer, static_cast<size_t>(MaterialTextureSlot::Count)>;

constexpr bool IsMaterialTextureEnabled(const MaterialTextureLayerArray& layers, MaterialTextureSlot slot) noexcept
{
    const size_t idx = static_cast<size_t>(slot);
    return idx < layers.size() && layers[idx].enabled && layers[idx].texture.IsValid();
}

constexpr ShaderResourceSemantic ToShaderResourceSemantic(MaterialTextureSlot slot) noexcept
{
    switch (slot)
    {
    case MaterialTextureSlot::Albedo:   return ShaderResourceSemantic::Albedo;
    case MaterialTextureSlot::Normal:   return ShaderResourceSemantic::Normal;
    case MaterialTextureSlot::ORM:      return ShaderResourceSemantic::ORM;
    case MaterialTextureSlot::Emissive: return ShaderResourceSemantic::Emissive;
    case MaterialTextureSlot::Detail:   return ShaderResourceSemantic::Detail;
    default:                            return ShaderResourceSemantic::Albedo;
    }
}

constexpr bool TryMapShaderSemanticToMaterialSlot(ShaderResourceSemantic semantic, MaterialTextureSlot& outSlot) noexcept
{
    switch (semantic)
    {
    case ShaderResourceSemantic::Albedo:   outSlot = MaterialTextureSlot::Albedo;   return true;
    case ShaderResourceSemantic::Normal:   outSlot = MaterialTextureSlot::Normal;   return true;
    case ShaderResourceSemantic::ORM:      outSlot = MaterialTextureSlot::ORM;      return true;
    case ShaderResourceSemantic::Emissive: outSlot = MaterialTextureSlot::Emissive; return true;
    case ShaderResourceSemantic::Detail:   outSlot = MaterialTextureSlot::Detail;   return true;
    case ShaderResourceSemantic::ShadowMap:
    case ShaderResourceSemantic::Count:
    default:
        break;
    }
    outSlot = MaterialTextureSlot::Albedo;
    return false;
}

constexpr MaterialTextureUVSet DefaultUVSetForSemantic(ShaderResourceSemantic semantic) noexcept
{
    switch (semantic)
    {
    case ShaderResourceSemantic::Detail:   return MaterialTextureUVSet::UV1;
    case ShaderResourceSemantic::Albedo:
    case ShaderResourceSemantic::Normal:
    case ShaderResourceSemantic::ORM:
    case ShaderResourceSemantic::Emissive:
    case ShaderResourceSemantic::ShadowMap:
    default:                               return MaterialTextureUVSet::UV0;
    }
}

constexpr bool DefaultExpectsSRGBForSemantic(ShaderResourceSemantic semantic) noexcept
{
    switch (semantic)
    {
    case ShaderResourceSemantic::Albedo:
    case ShaderResourceSemantic::Emissive:
    case ShaderResourceSemantic::Detail:
        return true;
    case ShaderResourceSemantic::Normal:
    case ShaderResourceSemantic::ORM:
    case ShaderResourceSemantic::ShadowMap:
    default:
        return false;
    }
}
