#pragma once
#include "Handle.h"
#include "GDXTextureSlots.h"
#include "MaterialParams.h"

#include <array>
#include <cassert>
#include <cstdint>
#include "Core/GDXMath.h"

enum class MaterialShadowCullMode : uint8_t
{
    Auto = 0,
    Back = 1,
    None = 2,
};

class MaterialResource
{
public:
    MaterialResource()
    {
        NormalizeTextureLayers();
    }

    const MaterialParams& GetParams() const noexcept { return m_params; }
    const MaterialRenderPolicy& GetRenderPolicy() const noexcept { return m_renderPolicy; }
    const MaterialTextureLayerArray& GetTextureLayers() const noexcept { return m_textureLayers; }
    ShaderHandle GetShader() const noexcept { return m_shader; }
    void SetShader(ShaderHandle shader) noexcept { m_shader = shader; Touch(); }
    uint32_t GetSortID() const noexcept { return m_sortID; }
    void SetSortID(uint32_t sortID) noexcept { m_sortID = sortID; }
    uint32_t GetStateVersion() const noexcept { return m_stateVersion; }

    bool IsTransparent() const noexcept { return m_renderPolicy.blendMode == BlendMode::AlphaBlend; }
    bool IsAlphaTest() const noexcept { return m_renderPolicy.alphaTest; }
    bool IsDoubleSided() const noexcept { return m_renderPolicy.doubleSided; }
    bool IsUnlit() const noexcept { return m_params.unlit; }
    bool UsesPBR() const noexcept { return m_params.shadingModel == MaterialShadingModel::PBR; }
    bool UsesLegacyPhong() const noexcept { return m_params.shadingModel == MaterialShadingModel::Phong; }
    bool UsesDetailMap() const noexcept { return HasTexture(MaterialTextureSlot::Detail); }
    bool ReceivesShadows() const noexcept { return m_renderPolicy.receiveShadows; }
    bool UsesEmissive() const noexcept { return HasTexture(MaterialTextureSlot::Emissive) || HasEmissiveColor(); }

    MaterialShadingModel GetShadingModel() const noexcept { return m_params.shadingModel; }
    MaterialDetailBlendMode GetDetailBlendMode() const noexcept { return m_params.detailBlendMode; }
    MaterialShadowCullMode GetShadowCullMode() const noexcept { return m_shadowCullMode; }
    BlendMode GetBlendMode() const noexcept { return m_renderPolicy.blendMode; }

    bool IsShadowDoubleSided() const noexcept
    {
        switch (m_shadowCullMode)
        {
        case MaterialShadowCullMode::None: return true;
        case MaterialShadowCullMode::Back: return false;
        case MaterialShadowCullMode::Auto:
        default: return IsDoubleSided();
        }
    }

    void SetShadingModel(MaterialShadingModel model) noexcept { m_params.shadingModel = model; Touch(); }
    void SetShadowCullMode(MaterialShadowCullMode mode) noexcept { m_shadowCullMode = mode; Touch(); }
    void SetReceiveShadows(bool on) noexcept { m_renderPolicy.receiveShadows = on; Touch(); }
    void SetTransparent(bool on) noexcept
    {
        m_renderPolicy.blendMode = on ? BlendMode::AlphaBlend : BlendMode::Opaque;
        Touch();
    }
    void SetAlphaTest(bool on) noexcept { m_renderPolicy.alphaTest = on; Touch(); }
    void SetDoubleSided(bool on) noexcept { m_renderPolicy.doubleSided = on; Touch(); }
    void SetBlendMode(BlendMode mode) noexcept { m_renderPolicy.blendMode = mode; Touch(); }
    void SetUnlit(bool on) noexcept { m_params.unlit = on; Touch(); }
    void SetBaseColor(float r, float g, float b, float a = 1.0f) noexcept { m_params.baseColor = { r, g, b, a }; Touch(); }
    void SetMetallic(float v) noexcept { m_params.metallic = v; Touch(); }
    void SetRoughness(float v) noexcept { m_params.roughness = v; Touch(); }
    void SetNormalScale(float v) noexcept { m_params.normalScale = v; Touch(); }
    void SetOcclusionStrength(float v) noexcept { m_params.occlusionStrength = v; Touch(); }

    void SetLegacyPhong(float r, float g, float b, float shininess) noexcept
    {
        m_params.shadingModel = MaterialShadingModel::Phong;
        m_params.legacyPhong.specularColor = { r, g, b, 1.0f };
        m_params.legacyPhong.shininess = shininess;
        Touch();
    }

    void SetOpacity(float v) noexcept
    {
        m_params.opacity = v;
        Touch();
    }
    void SetAlphaCutoff(float v) noexcept { m_params.alphaCutoff = v; Touch(); }

    void SetEmissiveColor(float r, float g, float b) noexcept
    {
        m_params.emissiveColor = { r, g, b, 1.0f };
        Touch();
    }

    void SetTiling(float tx, float ty, float ox = 0.0f, float oy = 0.0f) noexcept
    {
        m_params.uvTilingOffset = { tx, ty, ox, oy };
        Touch();
    }

    void SetTilingX(float tx) noexcept
    {
        m_params.uvTilingOffset.x = tx;
        Touch();
    }

    void SetNormalTiling(float tx, float ty, float ox = 0.0f, float oy = 0.0f) noexcept
    {
        m_params.uvNormalTilingOffset = { tx, ty, ox, oy };
        Touch();
    }

    void SetDetailTiling(float tx, float ty, float ox = 0.0f, float oy = 0.0f) noexcept
    {
        m_params.uvDetailTilingOffset = { tx, ty, ox, oy };
        Touch();
    }

    void SetDetailBlendMode(MaterialDetailBlendMode mode) noexcept { m_params.detailBlendMode = mode; Touch(); }
    void SetDetailBlendFactor(float factor) noexcept { m_params.blendFactor = factor; Touch(); }

    MaterialTextureLayer& Layer(MaterialTextureSlot slot) noexcept
    {
        const size_t idx = static_cast<size_t>(slot);
        assert(idx < m_textureLayers.size());
        return m_textureLayers[idx];
    }

    const MaterialTextureLayer& Layer(MaterialTextureSlot slot) const noexcept
    {
        const size_t idx = static_cast<size_t>(slot);
        assert(idx < m_textureLayers.size());
        return m_textureLayers[idx];
    }

    void SetTexture(MaterialTextureSlot slot, TextureHandle texture,
                    MaterialTextureUVSet uvSet = MaterialTextureUVSet::Auto) noexcept
    {
        auto& layer = Layer(slot);
        layer.texture = texture;
        layer.enabled = texture.IsValid();
        layer.uvSet = (uvSet == MaterialTextureUVSet::Auto) ? DefaultUVSetForSlot(slot) : uvSet;
        layer.expectsSRGB = DefaultExpectsSRGBForSlot(slot);
        Touch();
    }

    void ClearTexture(MaterialTextureSlot slot) noexcept
    {
        auto& layer = Layer(slot);
        layer.texture = TextureHandle::Invalid();
        layer.enabled = false;
        layer.uvSet = DefaultUVSetForSlot(slot);
        layer.expectsSRGB = DefaultExpectsSRGBForSlot(slot);
        Touch();
    }

    bool HasTexture(MaterialTextureSlot slot) const noexcept
    {
        const auto& layer = Layer(slot);
        return layer.enabled && layer.texture.IsValid();
    }

    TextureHandle GetTexture(MaterialTextureSlot slot) const noexcept
    {
        const auto& layer = Layer(slot);
        return (layer.enabled && layer.texture.IsValid()) ? layer.texture : TextureHandle::Invalid();
    }

    bool HasConsistentTextureState() const noexcept
    {
        for (size_t i = 0; i < m_textureLayers.size(); ++i)
        {
            const auto& l = m_textureLayers[i];
            if (l.enabled != l.texture.IsValid())
                return false;
        }
        return true;
    }

    void NormalizeTextureLayers() noexcept
    {
        bool changed = false;
        for (size_t i = 0; i < m_textureLayers.size(); ++i)
        {
            auto& layer = m_textureLayers[i];
            const auto slot = static_cast<MaterialTextureSlot>(i);
            const bool enabled = layer.texture.IsValid();
            if (layer.enabled != enabled)
            {
                layer.enabled = enabled;
                changed = true;
            }
            if (layer.uvSet == MaterialTextureUVSet::Auto)
            {
                layer.uvSet = DefaultUVSetForSlot(slot);
                changed = true;
            }
            const bool expectsSRGB = DefaultExpectsSRGBForSlot(slot);
            if (layer.expectsSRGB != expectsSRGB)
            {
                layer.expectsSRGB = expectsSRGB;
                changed = true;
            }
        }
        if (changed)
            Touch();
    }

    static MaterialResource FlatColor(float r, float g, float b, float a = 1.0f)
    {
        MaterialResource m;
        m.SetBaseColor(r, g, b, a);
        m.SetShadingModel(MaterialShadingModel::PBR);
        return m;
    }

    static constexpr MaterialTextureUVSet DefaultUVSetForSlot(MaterialTextureSlot slot) noexcept
    {
        switch (slot)
        {
        case MaterialTextureSlot::Detail: return MaterialTextureUVSet::UV1;
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
        default:
            return false;
        }
    }

    bool HasEmissiveColor() const noexcept
    {
        const Float4& e = m_params.emissiveColor;
        return (e.x != 0.0f) || (e.y != 0.0f) || (e.z != 0.0f);
    }

private:
    MaterialParams m_params{};
    MaterialRenderPolicy m_renderPolicy{};
    ShaderHandle m_shader{};
    MaterialTextureLayerArray m_textureLayers{};
    uint32_t m_sortID = 0u;
    uint32_t m_stateVersion = 1u;
    MaterialShadowCullMode m_shadowCullMode = MaterialShadowCullMode::Auto;

    void Touch() noexcept { ++m_stateVersion; }
};
