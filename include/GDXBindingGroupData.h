#pragma once

#include "GDXResourceBinding.h"

#include <array>
#include <cstdint>
#include <vector>

struct GDXRecordedTextureBinding
{
    ShaderResourceSemantic semantic = ShaderResourceSemantic::Albedo;
    uint32_t bindingIndex = 0u;
    GDXBindingGroup bindingGroup = GDXBindingGroup::Material;
    GDXBoundResourceClass resourceClass = GDXBoundResourceClass::Texture;
    GDXShaderStageVisibility visibility = GDXShaderStageVisibility::Pixel;
    TextureHandle texture = TextureHandle::Invalid();
    MaterialTextureUVSet uvSet = MaterialTextureUVSet::UV0;
    bool enabled = false;
    bool expectsSRGB = false;
    bool required = true;
    ResourceState requiredState = ResourceState::ShaderRead;
};

struct GDXRecordedConstantBufferBinding
{
    GDXShaderConstantBufferSlot semantic = GDXShaderConstantBufferSlot::Entity;
    uint32_t bindingIndex = 0u;
    GDXBindingGroup bindingGroup = GDXBindingGroup::Draw;
    GDXBoundResourceClass resourceClass = GDXBoundResourceClass::ConstantBuffer;
    GDXShaderStageVisibility visibility = GDXShaderStageVisibility::AllGraphics;
    MaterialHandle materialHandle = MaterialHandle::Invalid();
    bool enabled = false;
    bool required = true;
};

struct GDXDescriptorSetBuildDesc
{
    // Descriptor-Set-Building darf nicht an die Material-Semantik-Anzahl gekoppelt sein.
    // PostProcess und spaetere deklarative Passes koennen Shader-Register ausserhalb
    // der klassischen 0..5-Materialsemantik verwenden.
    static constexpr size_t MaxTextureBindings = 32u;
    static constexpr size_t MaxConstantBufferBindings = 8u;

    ResourceBindingScope scope = ResourceBindingScope::Material;
    GDXBindingGroup bindingGroup = GDXBindingGroup::Material;
    std::array<ShaderResourceBindingDesc, MaxTextureBindings> textures{};
    std::array<ConstantBufferBindingDesc, MaxConstantBufferBindings> constantBuffers{};
    uint32_t textureCount = 0u;
    uint32_t constantBufferCount = 0u;

    void Clear() noexcept
    {
        textureCount = 0u;
        constantBufferCount = 0u;
        for (auto& t : textures) t = {};
        for (auto& cb : constantBuffers) cb = {};
    }

    void Reset(ResourceBindingScope inScope) noexcept
    {
        Clear();
        scope = inScope;
        bindingGroup = GDXBindingGroupFromScope(inScope);
    }

    void AddTexture(const ShaderResourceBindingDesc& desc) noexcept
    {
        if (textureCount < textures.size())
            textures[textureCount++] = desc;
    }

    void AddConstantBuffer(const ConstantBufferBindingDesc& desc) noexcept
    {
        if (constantBufferCount < constantBuffers.size())
            constantBuffers[constantBufferCount++] = desc;
    }

    bool HasAnyBinding() const noexcept
    {
        return textureCount != 0u || constantBufferCount != 0u;
    }
};

struct GDXRecordedBindingGroupData
{
    // Recorded Binding Groups muessen nach Binding-Index adressierbar bleiben.
    // Daher bewusst groesser als die reine Semantic-Anzahl.
    static constexpr size_t MaxTextureBindings = 32u;
    static constexpr size_t MaxConstantBufferBindings = 8u;

    ResourceBindingScope scope = ResourceBindingScope::Material;
    GDXBindingGroup bindingGroup = GDXBindingGroup::Material;
    std::array<GDXRecordedTextureBinding, MaxTextureBindings> textures{};
    std::array<GDXRecordedConstantBufferBinding, MaxConstantBufferBindings> constantBuffers{};
    uint32_t textureCount = 0u;
    uint32_t constantBufferCount = 0u;

    void Clear() noexcept
    {
        textureCount = 0u;
        constantBufferCount = 0u;
        for (auto& t : textures) t = {};
        for (auto& cb : constantBuffers) cb = {};
    }

    void AddTexture(const GDXRecordedTextureBinding& binding) noexcept
    {
        if (textureCount < textures.size())
            textures[textureCount++] = binding;
    }

    void AddConstantBuffer(const GDXRecordedConstantBufferBinding& binding) noexcept
    {
        if (constantBufferCount < constantBuffers.size())
            constantBuffers[constantBufferCount++] = binding;
    }

    bool HasAnyEnabledBinding() const noexcept
    {
        for (uint32_t i = 0; i < textureCount; ++i)
            if (textures[i].enabled)
                return true;
        for (uint32_t i = 0; i < constantBufferCount; ++i)
            if (constantBuffers[i].enabled)
                return true;
        return false;
    }
};

inline bool GDXBindingConflicts(uint32_t bindingIndexA,
                                GDXBindingGroup bindingGroupA,
                                GDXBoundResourceClass resourceClassA,
                                uint32_t bindingIndexB,
                                GDXBindingGroup bindingGroupB,
                                GDXBoundResourceClass resourceClassB) noexcept
{
    return bindingIndexA == bindingIndexB &&
           bindingGroupA == bindingGroupB &&
           resourceClassA == resourceClassB;
}

inline bool GDXBindingEnabledForValidation(bool enabled, bool required) noexcept
{
    return enabled && required;
}

inline bool GDXValidateDescriptorSetBuildDesc(const GDXDescriptorSetBuildDesc& data) noexcept
{
    if (data.bindingGroup != GDXBindingGroupFromScope(data.scope))
        return false;

    for (uint32_t i = 0; i < data.textureCount; ++i)
    {
        const auto& a = data.textures[i];
        if (a.bindingGroup != data.bindingGroup || GDXBindingScopeFromGroup(a.bindingGroup) != data.scope)
            return false;
        if (!GDXBindingEnabledForValidation(a.enabled, a.required))
            continue;
        for (uint32_t j = i + 1u; j < data.textureCount; ++j)
        {
            const auto& b = data.textures[j];
            if (!GDXBindingEnabledForValidation(b.enabled, b.required))
                continue;
            if (GDXBindingConflicts(a.bindingIndex, a.bindingGroup, a.resourceClass,
                                    b.bindingIndex, b.bindingGroup, b.resourceClass))
                return false;
        }
    }

    for (uint32_t i = 0; i < data.constantBufferCount; ++i)
    {
        const auto& a = data.constantBuffers[i];
        if (a.bindingGroup != data.bindingGroup || GDXBindingScopeFromGroup(a.bindingGroup) != data.scope)
            return false;
        if (!GDXBindingEnabledForValidation(a.enabled, a.required))
            continue;
        for (uint32_t j = i + 1u; j < data.constantBufferCount; ++j)
        {
            const auto& b = data.constantBuffers[j];
            if (!GDXBindingEnabledForValidation(b.enabled, b.required))
                continue;
            if (GDXBindingConflicts(a.bindingIndex, a.bindingGroup, a.resourceClass,
                                    b.bindingIndex, b.bindingGroup, b.resourceClass))
                return false;
        }
    }

    return true;
}

inline bool GDXValidateBindingGroupData(const GDXRecordedBindingGroupData& data) noexcept
{
    if (data.bindingGroup != GDXBindingGroupFromScope(data.scope))
        return false;

    for (uint32_t i = 0; i < data.textureCount; ++i)
    {
        const auto& a = data.textures[i];
        if (a.bindingGroup != data.bindingGroup || GDXBindingScopeFromGroup(a.bindingGroup) != data.scope)
            return false;
        if (!GDXBindingEnabledForValidation(a.enabled, a.required))
            continue;
        for (uint32_t j = i + 1u; j < data.textureCount; ++j)
        {
            const auto& b = data.textures[j];
            if (!GDXBindingEnabledForValidation(b.enabled, b.required))
                continue;
            if (GDXBindingConflicts(a.bindingIndex, a.bindingGroup, a.resourceClass,
                                    b.bindingIndex, b.bindingGroup, b.resourceClass))
                return false;
        }
    }

    for (uint32_t i = 0; i < data.constantBufferCount; ++i)
    {
        const auto& a = data.constantBuffers[i];
        if (a.bindingGroup != data.bindingGroup || GDXBindingScopeFromGroup(a.bindingGroup) != data.scope)
            return false;
        if (!GDXBindingEnabledForValidation(a.enabled, a.required))
            continue;
        for (uint32_t j = i + 1u; j < data.constantBufferCount; ++j)
        {
            const auto& b = data.constantBuffers[j];
            if (!GDXBindingEnabledForValidation(b.enabled, b.required))
                continue;
            if (GDXBindingConflicts(a.bindingIndex, a.bindingGroup, a.resourceClass,
                                    b.bindingIndex, b.bindingGroup, b.resourceClass))
                return false;
        }
    }

    return true;
}

inline GDXDescriptorSetBuildDesc BuildDescriptorSetBuildDesc(
    const ResourceBindingSet& set,
    ResourceBindingScope scope) noexcept
{
    GDXDescriptorSetBuildDesc out{};
    out.Reset(scope);

    for (uint32_t i = 0; i < set.textureCount; ++i)
    {
        const auto& src = set.textures[i];
        if (src.scope == scope)
            out.AddTexture(src);
    }

    for (uint32_t i = 0; i < set.constantBufferCount; ++i)
    {
        const auto& src = set.constantBuffers[i];
        if (src.scope == scope)
            out.AddConstantBuffer(src);
    }

    return out;
}


inline GDXPipelineLayoutDesc BuildPipelineLayoutFromDescriptorSetBuildDesc(
    const GDXDescriptorSetBuildDesc& buildDesc) noexcept
{
    GDXPipelineLayoutDesc out{};
    out.Reset();

    for (uint32_t i = 0; i < buildDesc.textureCount; ++i)
    {
        const auto& src = buildDesc.textures[i];
        GDXShaderTextureBinding dst{};
        switch (src.semantic)
        {
        case ShaderResourceSemantic::Albedo:   dst.semantic = GDXShaderTextureSemantic::Albedo; break;
        case ShaderResourceSemantic::Normal:   dst.semantic = GDXShaderTextureSemantic::Normal; break;
        case ShaderResourceSemantic::ORM:      dst.semantic = GDXShaderTextureSemantic::ORM; break;
        case ShaderResourceSemantic::Emissive: dst.semantic = GDXShaderTextureSemantic::Emissive; break;
        case ShaderResourceSemantic::ShadowMap:dst.semantic = GDXShaderTextureSemantic::ShadowMap; break;
        case ShaderResourceSemantic::Detail:
        case ShaderResourceSemantic::Count:
        default:                              dst.semantic = GDXShaderTextureSemantic::Detail; break;
        }
        dst.bindingGroup = GDXBindingGroupFromScope(buildDesc.scope);
        dst.resourceClass = src.resourceClass;
        dst.visibility = src.visibility;
        dst.layoutBindingIndex = src.bindingIndex;
        out.AddTextureBinding(dst);
    }

    for (uint32_t i = 0; i < buildDesc.constantBufferCount; ++i)
    {
        const auto& src = buildDesc.constantBuffers[i];
        GDXShaderConstantBufferBinding dst{};
        dst.slot = src.semantic;
        dst.bindingGroup = GDXBindingGroupFromScope(buildDesc.scope);
        dst.resourceClass = src.resourceClass;
        dst.visibility = src.visibility;
        dst.layoutBindingIndex = src.bindingIndex;
        out.AddConstantBufferBinding(dst);
    }

    return out;
}

inline GDXRecordedBindingGroupData BuildRecordedBindingGroupData(
    const GDXDescriptorSetBuildDesc& buildDesc) noexcept
{
    GDXRecordedBindingGroupData out{};
    out.scope = buildDesc.scope;
    out.bindingGroup = buildDesc.bindingGroup;

    for (uint32_t i = 0; i < buildDesc.textureCount; ++i)
    {
        const auto& src = buildDesc.textures[i];
        GDXRecordedTextureBinding dst{};
        dst.semantic = src.semantic;
        dst.bindingIndex = src.bindingIndex;
        dst.bindingGroup = GDXBindingGroupFromScope(buildDesc.scope);
        dst.resourceClass = src.resourceClass;
        dst.visibility = src.visibility;
        dst.texture = src.texture;
        dst.uvSet = src.uvSet;
        dst.enabled = src.enabled;
        dst.expectsSRGB = src.expectsSRGB;
        dst.required = src.required;
        dst.requiredState = src.requiredState;
        out.AddTexture(dst);
    }

    for (uint32_t i = 0; i < buildDesc.constantBufferCount; ++i)
    {
        const auto& src = buildDesc.constantBuffers[i];
        GDXRecordedConstantBufferBinding dst{};
        dst.semantic = src.semantic;
        dst.bindingIndex = src.bindingIndex;
        dst.bindingGroup = GDXBindingGroupFromScope(buildDesc.scope);
        dst.resourceClass = src.resourceClass;
        dst.visibility = src.visibility;
        dst.materialHandle = src.materialHandle;
        dst.enabled = src.enabled;
        dst.required = src.required;
        out.AddConstantBuffer(dst);
    }

    return out;
}

inline GDXRecordedBindingGroupData BuildRecordedBindingGroupData(
    const ResourceBindingSet& set,
    ResourceBindingScope scope) noexcept
{
    return BuildRecordedBindingGroupData(BuildDescriptorSetBuildDesc(set, scope));
}
