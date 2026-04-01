#pragma once

#include "GDXTextureSlots.h"
#include "MaterialResource.h"
#include "GDXShaderLayout.h"
#include "GDXResourceBinding.h"

#include <array>
#include <cstdint>

// Autoritativer Materialvertrag fuer Surface-Daten, Render-Policy und
// Material-zu-Shader-Texturbindings.
enum class MaterialParameterSemantic : uint8_t
{
    BaseColor = 0,
    LegacySpecularColor,
    EmissiveColor,
    MainUVTransform,
    DetailUVTransform,
    NormalUVTransform,
    Metallic,
    Roughness,
    NormalScale,
    OcclusionStrength,
    LegacyShininess,
    Opacity,
    AlphaCutoff,
    DetailBlendFactor,
    ReceiveShadows,
    DoubleSided,
    Unlit,
    AlphaTest,
    BlendMode,
    ShadingModel,
    DetailBlendMode,
    Count
};

struct MaterialParameterBinding
{
    MaterialParameterSemantic semantic = MaterialParameterSemantic::BaseColor;
    bool required = true;
};

struct MaterialTextureSemanticBinding
{
    MaterialTextureSlot slot = MaterialTextureSlot::Albedo;
    ShaderResourceSemantic shaderSemantic = ShaderResourceSemantic::Albedo;
    bool required = false;
};

struct MaterialSemanticValidationResult
{
    bool valid = true;
    bool missingRequiredParameter = false;
    bool missingRequiredTexture = false;
    bool unsupportedShaderSemantic = false;
    bool undeclaredMaterialTexture = false;
    bool incompatibleShadingModel = false;
};

struct MaterialSemanticLayout
{
    std::array<MaterialParameterBinding, static_cast<size_t>(MaterialParameterSemantic::Count)> parameters{};
    std::array<MaterialTextureSemanticBinding, static_cast<size_t>(MaterialTextureSlot::Count)> textures{};
    uint32_t parameterCount = 0u;
    uint32_t textureCount = 0u;

    static constexpr bool SupportsSemanticInShadingModel(MaterialParameterSemantic semantic,
                                                         MaterialShadingModel shadingModel) noexcept
    {
        switch (semantic)
        {
        case MaterialParameterSemantic::Metallic:
        case MaterialParameterSemantic::Roughness:
        case MaterialParameterSemantic::NormalScale:
        case MaterialParameterSemantic::OcclusionStrength:
            return shadingModel == MaterialShadingModel::PBR;

        case MaterialParameterSemantic::LegacySpecularColor:
        case MaterialParameterSemantic::LegacyShininess:
            return shadingModel == MaterialShadingModel::Phong;

        default:
            return true;
        }
    }

    static bool HasUsableParameterValue(const MaterialResource& mat,
                                                  MaterialParameterSemantic semantic) noexcept
    {
        const MaterialParams& p = mat.GetParams();
        const MaterialRenderPolicy& policy = mat.GetRenderPolicy();

        switch (semantic)
        {
        case MaterialParameterSemantic::BaseColor:
        case MaterialParameterSemantic::LegacySpecularColor:
        case MaterialParameterSemantic::EmissiveColor:
        case MaterialParameterSemantic::MainUVTransform:
        case MaterialParameterSemantic::DetailUVTransform:
        case MaterialParameterSemantic::NormalUVTransform:
        case MaterialParameterSemantic::Metallic:
        case MaterialParameterSemantic::Roughness:
        case MaterialParameterSemantic::NormalScale:
        case MaterialParameterSemantic::OcclusionStrength:
        case MaterialParameterSemantic::LegacyShininess:
        case MaterialParameterSemantic::Opacity:
        case MaterialParameterSemantic::AlphaCutoff:
        case MaterialParameterSemantic::DetailBlendFactor:
        case MaterialParameterSemantic::Unlit:
        case MaterialParameterSemantic::BlendMode:
        case MaterialParameterSemantic::ShadingModel:
        case MaterialParameterSemantic::DetailBlendMode:
            (void)p;
            return true;

        case MaterialParameterSemantic::ReceiveShadows:
            return policy.receiveShadows || !policy.receiveShadows;
        case MaterialParameterSemantic::DoubleSided:
            return policy.doubleSided || !policy.doubleSided;
        case MaterialParameterSemantic::AlphaTest:
            return policy.alphaTest || !policy.alphaTest;
        }

        return false;
    }

    static constexpr ShaderResourceSemantic ToShaderResourceSemantic(GDXShaderTextureSemantic semantic) noexcept
    {
        switch (semantic)
        {
        case GDXShaderTextureSemantic::Albedo:   return ShaderResourceSemantic::Albedo;
        case GDXShaderTextureSemantic::Normal:   return ShaderResourceSemantic::Normal;
        case GDXShaderTextureSemantic::ORM:      return ShaderResourceSemantic::ORM;
        case GDXShaderTextureSemantic::Emissive: return ShaderResourceSemantic::Emissive;
        case GDXShaderTextureSemantic::Detail:   return ShaderResourceSemantic::Detail;
        case GDXShaderTextureSemantic::ShadowMap:return ShaderResourceSemantic::ShadowMap;
        default:                                 return ShaderResourceSemantic::Count;
        }
    }

    static MaterialSemanticLayout BuildDefault() noexcept
    {
        MaterialSemanticLayout l{};
        auto addP = [&](MaterialParameterSemantic s)
        {
            l.parameters[l.parameterCount++] = MaterialParameterBinding{ s, true };
        };
        addP(MaterialParameterSemantic::BaseColor);
        addP(MaterialParameterSemantic::LegacySpecularColor);
        addP(MaterialParameterSemantic::EmissiveColor);
        addP(MaterialParameterSemantic::MainUVTransform);
        addP(MaterialParameterSemantic::DetailUVTransform);
        addP(MaterialParameterSemantic::NormalUVTransform);
        addP(MaterialParameterSemantic::Metallic);
        addP(MaterialParameterSemantic::Roughness);
        addP(MaterialParameterSemantic::NormalScale);
        addP(MaterialParameterSemantic::OcclusionStrength);
        addP(MaterialParameterSemantic::LegacyShininess);
        addP(MaterialParameterSemantic::Opacity);
        addP(MaterialParameterSemantic::AlphaCutoff);
        addP(MaterialParameterSemantic::DetailBlendFactor);
        addP(MaterialParameterSemantic::ReceiveShadows);
        addP(MaterialParameterSemantic::DoubleSided);
        addP(MaterialParameterSemantic::Unlit);
        addP(MaterialParameterSemantic::AlphaTest);
        addP(MaterialParameterSemantic::BlendMode);
        addP(MaterialParameterSemantic::ShadingModel);
        addP(MaterialParameterSemantic::DetailBlendMode);

        l.textures[l.textureCount++] = { MaterialTextureSlot::Albedo,   ShaderResourceSemantic::Albedo,   false };
        l.textures[l.textureCount++] = { MaterialTextureSlot::Normal,   ShaderResourceSemantic::Normal,   false };
        l.textures[l.textureCount++] = { MaterialTextureSlot::ORM,      ShaderResourceSemantic::ORM,      false };
        l.textures[l.textureCount++] = { MaterialTextureSlot::Emissive, ShaderResourceSemantic::Emissive, false };
        l.textures[l.textureCount++] = { MaterialTextureSlot::Detail,   ShaderResourceSemantic::Detail,   false };
        return l;
    }

    bool HasParameter(MaterialParameterSemantic semantic) const noexcept
    {
        for (uint32_t i = 0; i < parameterCount; ++i)
        {
            if (parameters[i].semantic == semantic)
                return true;
        }
        return false;
    }

    const MaterialTextureSemanticBinding* FindBySlot(MaterialTextureSlot slot) const noexcept
    {
        for (uint32_t i = 0; i < textureCount; ++i)
        {
            if (textures[i].slot == slot)
                return &textures[i];
        }
        return nullptr;
    }

    const MaterialTextureSemanticBinding* FindBySemantic(ShaderResourceSemantic semantic) const noexcept
    {
        for (uint32_t i = 0; i < textureCount; ++i)
        {
            if (textures[i].shaderSemantic == semantic)
                return &textures[i];
        }
        return nullptr;
    }

    bool TryResolveBinding(ShaderResourceSemantic semantic, MaterialTextureSemanticBinding& outBinding) const noexcept
    {
        const MaterialTextureSemanticBinding* found = FindBySemantic(semantic);
        if (!found)
            return false;
        outBinding = *found;
        return true;
    }

    bool ValidateMaterial(const MaterialResource& mat, MaterialSemanticValidationResult* outResult = nullptr) const noexcept
    {
        MaterialSemanticValidationResult result{};

        for (uint32_t i = 0; i < parameterCount; ++i)
        {
            const MaterialParameterBinding& binding = parameters[i];
            if (!SupportsSemanticInShadingModel(binding.semantic, mat.GetShadingModel()))
                continue;
            if (binding.required && !HasUsableParameterValue(mat, binding.semantic))
            {
                result.valid = false;
                result.missingRequiredParameter = true;
            }
        }

        for (size_t i = 0; i < static_cast<size_t>(MaterialTextureSlot::Count); ++i)
        {
            const MaterialTextureSlot slot = static_cast<MaterialTextureSlot>(i);
            const bool hasTexture = mat.HasTexture(slot);
            const MaterialTextureSemanticBinding* binding = FindBySlot(slot);
            if (hasTexture && !binding)
            {
                result.valid = false;
                result.undeclaredMaterialTexture = true;
            }
            if (binding && binding->required && !hasTexture)
            {
                result.valid = false;
                result.missingRequiredTexture = true;
            }
        }

        if (outResult)
            *outResult = result;
        return result.valid;
    }

    bool ValidateShaderCompatibility(const GDXShaderLayout& shaderLayout,
                                     const MaterialResource& mat,
                                     MaterialSemanticValidationResult* outResult = nullptr) const noexcept
    {
        MaterialSemanticValidationResult result{};
        (void)ValidateMaterial(mat, &result);

        for (uint32_t i = 0; i < shaderLayout.textureBindingCount; ++i)
        {
            const auto& shaderBinding = shaderLayout.textureBindings[i];
            const ShaderResourceSemantic semantic = ToShaderResourceSemantic(shaderBinding.semantic);
            if (semantic == ShaderResourceSemantic::ShadowMap)
                continue;
            if (semantic == ShaderResourceSemantic::Count)
            {
                result.valid = false;
                result.unsupportedShaderSemantic = true;
                continue;
            }

            const MaterialTextureSemanticBinding* binding = FindBySemantic(semantic);
            if (!binding)
            {
                result.valid = false;
                result.unsupportedShaderSemantic = true;
                continue;
            }

            if (binding->required && !mat.HasTexture(binding->slot))
            {
                result.valid = false;
                result.missingRequiredTexture = true;
            }
        }

        if (mat.UsesLegacyPhong())
        {
            for (uint32_t i = 0; i < shaderLayout.textureBindingCount; ++i)
            {
                const ShaderResourceSemantic semantic = ToShaderResourceSemantic(shaderLayout.textureBindings[i].semantic);
                if (semantic == ShaderResourceSemantic::ORM)
                {
                    result.valid = false;
                    result.incompatibleShadingModel = true;
                    break;
                }
            }
        }

        if (outResult)
            *outResult = result;
        return result.valid;
    }

    bool BuildTextureBindingDesc(const GDXShaderTextureBinding& shaderBinding,
                                 const MaterialResource& mat,
                                 ShaderResourceBindingDesc& outDesc,
                                 MaterialSemanticValidationResult* outResult = nullptr) const noexcept
    {
        MaterialSemanticValidationResult result{};
        const ShaderResourceSemantic semantic = ToShaderResourceSemantic(shaderBinding.semantic);

        outDesc = {};
        outDesc.semantic = semantic;
        outDesc.bindingIndex = shaderBinding.layoutBindingIndex;
        outDesc.bindingGroup = shaderBinding.bindingGroup;
        outDesc.resourceClass = shaderBinding.resourceClass;
        outDesc.visibility = shaderBinding.visibility;
        outDesc.requiredState = ResourceState::ShaderRead;
        outDesc.scope = GDXBindingScopeForTextureSemantic(semantic);

        if (semantic == ShaderResourceSemantic::ShadowMap)
        {
            outDesc.texture = TextureHandle::Invalid();
            outDesc.uvSet = MaterialTextureUVSet::UV0;
            outDesc.enabled = false;
            outDesc.expectsSRGB = false;
            outDesc.required = false;
            if (outResult) *outResult = result;
            return true;
        }

        if (semantic == ShaderResourceSemantic::Count)
        {
            result.valid = false;
            result.unsupportedShaderSemantic = true;
            if (outResult) *outResult = result;
            return false;
        }

        const MaterialTextureSemanticBinding* binding = FindBySemantic(semantic);
        if (!binding)
        {
            result.valid = false;
            result.unsupportedShaderSemantic = true;
            if (outResult) *outResult = result;
            return false;
        }

        const auto& layer = mat.Layer(binding->slot);
        outDesc.texture = layer.texture;
        outDesc.uvSet = (layer.uvSet == MaterialTextureUVSet::Auto)
            ? DefaultUVSetForSemantic(semantic)
            : layer.uvSet;
        outDesc.enabled = layer.enabled;
        outDesc.expectsSRGB = layer.expectsSRGB;
        outDesc.required = binding->required;

        if (binding->required && !layer.texture.IsValid())
        {
            result.valid = false;
            result.missingRequiredTexture = true;
            if (outResult) *outResult = result;
            return false;
        }

        if (outResult)
            *outResult = result;
        return true;
    }
};
