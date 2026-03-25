#pragma once
#include "Handle.h"
#include "GDXTextureSlots.h"

#include <array>
#include <cstdint>
#include "Core/GDXMath.h"

struct MaterialData
{
    Float4 baseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    Float4 specularColor = { 0.5f, 0.5f, 0.5f, 1.0f };
    Float4 emissiveColor = { 0.0f, 0.0f, 0.0f, 1.0f };
    Float4 uvTilingOffset = { 1.0f, 1.0f, 0.0f, 0.0f };
    Float4 uvDetailTilingOffset  = { 1.0f, 1.0f, 0.0f, 0.0f };
    Float4 uvNormalTilingOffset  = { 1.0f, 1.0f, 0.0f, 0.0f };
    float metallic          = 0.0f;
    float roughness         = 0.5f;
    float normalScale       = 1.0f;
    float occlusionStrength = 1.0f;
    float shininess         = 32.0f;
    float transparency      = 0.0f;
    float alphaCutoff       = 0.5f;
    float receiveShadows    = 1.0f;
    float blendMode         = 0.0f;
    float blendFactor       = 0.0f;
    uint32_t flags          = 0u;
    float    _pad0          = 0.0f;
};
static_assert(sizeof(MaterialData) == 144, "MaterialData muss 144 Byte sein (cbuffer-Anforderung)");

enum class MaterialTextureBlendMode : uint32_t
{
    Multiply2x = 0u,
    Multiply   = 1u,
    Add        = 2u,
    Lerp       = 3u,
};

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
    MF_USE_DETAIL_MAP    = 1u << 11,
};

enum class MaterialShadowCullMode : uint8_t
{
    Auto = 0, // folgt MF_DOUBLE_SIDED für Rückwärtskompatibilität
    Back = 1, // Shadow-Pass cullt Backfaces
    None = 2, // Shadow-Pass rendert beidseitig
};

class MaterialResource
{
public:
    MaterialData data;
    ShaderHandle shader;

    // Kanonischer Materialzustand: nur textureLayers wird vom Renderer gelesen.
    MaterialTextureLayerArray textureLayers{};

    uint32_t sortID = 0u;
    void* gpuConstantBuffer = nullptr;
    bool  cpuDirty          = true;
    MaterialShadowCullMode shadowCullMode = MaterialShadowCullMode::Auto;

    MaterialResource()
    {
        NormalizeTextureLayers();
    }

    bool IsTransparent() const noexcept { return (data.flags & MF_TRANSPARENT)     != 0u; }
    bool IsAlphaTest()   const noexcept { return (data.flags & MF_ALPHA_TEST)      != 0u; }
    bool IsDoubleSided() const noexcept { return (data.flags & MF_DOUBLE_SIDED)    != 0u; }
    bool IsUnlit()       const noexcept { return (data.flags & MF_UNLIT)           != 0u; }
    bool UsesPBR()       const noexcept { return (data.flags & MF_SHADING_PBR)     != 0u; }
    bool UsesDetailMap() const noexcept { return (data.flags & MF_USE_DETAIL_MAP)  != 0u; }

    MaterialShadowCullMode GetShadowCullMode() const noexcept
    {
        return shadowCullMode;
    }

    void SetShadowCullMode(MaterialShadowCullMode mode) noexcept
    {
        shadowCullMode = mode;
    }

    bool IsShadowDoubleSided() const noexcept
    {
        switch (shadowCullMode)
        {
        case MaterialShadowCullMode::None: return true;
        case MaterialShadowCullMode::Back: return false;
        case MaterialShadowCullMode::Auto:
        default: return IsDoubleSided();
        }
    }

    void SetFlag(MaterialFlags f, bool on) noexcept
    {
        if (on) data.flags |=  static_cast<uint32_t>(f);
        else    data.flags &= ~static_cast<uint32_t>(f);
        cpuDirty = true;
    }

    MaterialTextureLayer& Layer(MaterialTextureSlot slot) noexcept
    {
        return textureLayers[static_cast<size_t>(slot)];
    }

    const MaterialTextureLayer& Layer(MaterialTextureSlot slot) const noexcept
    {
        return textureLayers[static_cast<size_t>(slot)];
    }

    void SetTexture(MaterialTextureSlot slot, TextureHandle texture, MaterialTextureUVSet uvSet = MaterialTextureUVSet::Auto) noexcept
    {
        auto& layer = Layer(slot);
        layer.texture = texture;
        layer.enabled = texture.IsValid();
        layer.uvSet = (uvSet == MaterialTextureUVSet::Auto) ? DefaultUVSetForSlot(slot) : uvSet;
        layer.expectsSRGB = DefaultExpectsSRGBForSlot(slot);

        ApplyTextureFeatureFlag(slot, texture.IsValid());
        cpuDirty = true;
    }

    void ClearTexture(MaterialTextureSlot slot) noexcept
    {
        auto& layer = Layer(slot);
        layer.texture = TextureHandle::Invalid();
        layer.enabled = false;
        layer.uvSet = DefaultUVSetForSlot(slot);
        layer.expectsSRGB = DefaultExpectsSRGBForSlot(slot);

        ApplyTextureFeatureFlag(slot, false);
        cpuDirty = true;
    }

    bool HasTexture(MaterialTextureSlot slot) const noexcept
    {
        const auto& layer = Layer(slot);
        return layer.enabled && layer.texture.IsValid();
    }

    TextureHandle GetTexture(MaterialTextureSlot slot) const noexcept
    {
        const auto& layer = Layer(slot);
        if (layer.enabled && layer.texture.IsValid())
            return layer.texture;
        return TextureHandle::Invalid();
    }

    bool HasConsistentTextureState() const noexcept
    {
        for (size_t i = 0; i < textureLayers.size(); ++i)
        {
            const auto& layer = textureLayers[i];
            if (layer.enabled != layer.texture.IsValid())
                return false;
        }
        return true;
    }

    void NormalizeTextureLayers() noexcept
    {
        for (size_t i = 0; i < textureLayers.size(); ++i)
        {
            auto& layer = textureLayers[i];
            const auto slot = static_cast<MaterialTextureSlot>(i);
            layer.enabled = layer.texture.IsValid();
            if (layer.uvSet == MaterialTextureUVSet::Auto)
                layer.uvSet = DefaultUVSetForSlot(slot);
            layer.expectsSRGB = DefaultExpectsSRGBForSlot(slot);
        }
    }

    // Always use SetTiling/SetNormalTiling/SetDetailTiling instead of writing
    // data.uvXxxTilingOffset directly — direct field writes bypass cpuDirty
    // so the GPU cbuffer never gets the updated values.
    void SetTiling(float tilingX, float tilingY, float offsetX = 0.0f, float offsetY = 0.0f) noexcept
    {
        data.uvTilingOffset = { tilingX, tilingY, offsetX, offsetY };
        cpuDirty = true;
    }

    void SetNormalTiling(float tilingX, float tilingY, float offsetX = 0.0f, float offsetY = 0.0f) noexcept
    {
        data.uvNormalTilingOffset = { tilingX, tilingY, offsetX, offsetY };
        cpuDirty = true;
    }

    void SetDetailTiling(float tilingX, float tilingY, float offsetX = 0.0f, float offsetY = 0.0f) noexcept
    {
        data.uvDetailTilingOffset = { tilingX, tilingY, offsetX, offsetY };
        cpuDirty = true;
    }

    void SetDetailBlendMode(MaterialTextureBlendMode mode) noexcept
    {
        data.blendMode = static_cast<float>(static_cast<uint32_t>(mode));
        cpuDirty = true;
    }

    MaterialTextureBlendMode GetDetailBlendMode() const noexcept
    {
        const uint32_t raw = static_cast<uint32_t>(data.blendMode + 0.5f);
        switch (raw)
        {
        case 1u: return MaterialTextureBlendMode::Multiply;
        case 2u: return MaterialTextureBlendMode::Add;
        case 3u: return MaterialTextureBlendMode::Lerp;
        case 0u:
        default: return MaterialTextureBlendMode::Multiply2x;
        }
    }

    void SetDetailBlendFactor(float factor) noexcept
    {
        data.blendFactor = factor;
        cpuDirty = true;
    }

    static MaterialResource FlatColor(float r, float g, float b, float a = 1.0f)
    {
        MaterialResource m;
        m.data.baseColor = { r, g, b, a };
        return m;
    }

    static constexpr MaterialTextureUVSet DefaultUVSetForSlot(MaterialTextureSlot slot) noexcept
    {
        switch (slot)
        {
        case MaterialTextureSlot::Detail: return MaterialTextureUVSet::UV1;
        case MaterialTextureSlot::Albedo:
        case MaterialTextureSlot::Normal:
        case MaterialTextureSlot::ORM:
        case MaterialTextureSlot::Emissive:
        default: return MaterialTextureUVSet::UV0;
        }
    }

    static constexpr bool DefaultExpectsSRGBForSlot(MaterialTextureSlot slot) noexcept
    {
        switch (slot)
        {
        case MaterialTextureSlot::Albedo:
        case MaterialTextureSlot::Emissive:
        case MaterialTextureSlot::Detail:
            return true;
        case MaterialTextureSlot::Normal:
        case MaterialTextureSlot::ORM:
        default:
            return false;
        }
    }

private:
    void ApplyTextureFeatureFlag(MaterialTextureSlot slot, bool enabled) noexcept
    {
        switch (slot)
        {
        case MaterialTextureSlot::Normal:   SetFlag(MF_USE_NORMAL_MAP, enabled); break;
        case MaterialTextureSlot::ORM:      SetFlag(MF_USE_ORM_MAP, enabled); break;
        case MaterialTextureSlot::Emissive: SetFlag(MF_USE_EMISSIVE, enabled); break;
        case MaterialTextureSlot::Detail:   SetFlag(MF_USE_DETAIL_MAP, enabled); break;
        case MaterialTextureSlot::Albedo:
        default: break;
        }
    }
};
