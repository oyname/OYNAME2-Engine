#pragma once

#include "Handle.h"
#include "GDXTextureSlots.h"
#include "GDXResourceState.h"
#include "GDXRenderBindingModel.h"

#include <array>
#include <cstdint>

enum class ResourceBindingScope : uint8_t
{
    Pass = 0,
    Material = 1,
    Draw = 2,
};

inline constexpr GDXBindingGroup GDXBindingGroupFromScope(ResourceBindingScope scope) noexcept
{
    switch (scope)
    {
    case ResourceBindingScope::Pass:     return GDXBindingGroup::Pass;
    case ResourceBindingScope::Material: return GDXBindingGroup::Material;
    case ResourceBindingScope::Draw:     return GDXBindingGroup::Draw;
    default:                             return GDXBindingGroup::Material;
    }
}

inline constexpr ResourceBindingScope GDXBindingScopeFromGroup(GDXBindingGroup group) noexcept
{
    switch (group)
    {
    case GDXBindingGroup::Pass:     return ResourceBindingScope::Pass;
    case GDXBindingGroup::Material: return ResourceBindingScope::Material;
    case GDXBindingGroup::Draw:     return ResourceBindingScope::Draw;
    default:                        return ResourceBindingScope::Material;
    }
}

inline constexpr ResourceBindingScope GDXBindingScopeForConstantBufferSlot(GDXShaderConstantBufferSlot slot) noexcept
{
    switch (slot)
    {
    case GDXShaderConstantBufferSlot::Frame:
    case GDXShaderConstantBufferSlot::Pass:     return ResourceBindingScope::Pass;
    case GDXShaderConstantBufferSlot::Material: return ResourceBindingScope::Material;
    case GDXShaderConstantBufferSlot::Entity:
    case GDXShaderConstantBufferSlot::Skin:
    case GDXShaderConstantBufferSlot::Light:
    default:                                    return ResourceBindingScope::Draw;
    }
}

inline constexpr ResourceBindingScope GDXBindingScopeForTextureSemantic(ShaderResourceSemantic semantic) noexcept
{
    return (semantic == ShaderResourceSemantic::ShadowMap)
        ? ResourceBindingScope::Pass
        : ResourceBindingScope::Material;
}


struct ShaderResourceBindingDesc
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
    ResourceState requiredState = ResourceState::ShaderRead;
    bool required = true;
    ResourceBindingScope scope = ResourceBindingScope::Material;
};

struct ConstantBufferBindingDesc
{
    GDXShaderConstantBufferSlot semantic   = GDXShaderConstantBufferSlot::Entity;
    uint32_t                    bindingIndex = 0u;
    GDXBindingGroup              bindingGroup = GDXBindingGroup::Draw;
    GDXBoundResourceClass        resourceClass = GDXBoundResourceClass::ConstantBuffer;
    GDXShaderStageVisibility     visibility = GDXShaderStageVisibility::AllGraphics;
    // Material-CB wird vom Backend über materialHandle aufgelöst.
    // Für alle anderen Slots (Entity, Frame, Skin) hält das Backend die CBs intern.
    MaterialHandle              materialHandle = MaterialHandle::Invalid();
    bool                        enabled    = false;
    bool                        required   = true;
    ResourceBindingScope        scope      = ResourceBindingScope::Draw;
};

struct ResourceBindingSet
{
    static constexpr size_t MaxTextureBindings = static_cast<size_t>(ShaderResourceSemantic::Count);
    static constexpr size_t MaxConstantBufferBindings = 5u;

    std::array<ShaderResourceBindingDesc, MaxTextureBindings> textures{};
    std::array<ConstantBufferBindingDesc, MaxConstantBufferBindings> constantBuffers{};
    uint32_t textureCount = 0u;
    uint32_t constantBufferCount = 0u;

    void Clear() noexcept
    {
        textureCount = 0u;
        constantBufferCount = 0u;
        for (auto& t : textures)
            t = {};
        for (auto& cb : constantBuffers)
            cb = {};
    }

    void AddTextureBinding(const ShaderResourceBindingDesc& desc) noexcept
    {
        if (textureCount >= textures.size())
            return;
        textures[textureCount++] = desc;
    }

    void AddConstantBufferBinding(const ConstantBufferBindingDesc& desc) noexcept
    {
        if (constantBufferCount >= constantBuffers.size())
            return;
        constantBuffers[constantBufferCount++] = desc;
    }

    const ShaderResourceBindingDesc* FindTextureBinding(ShaderResourceSemantic semantic) const noexcept
    {
        for (uint32_t i = 0; i < textureCount; ++i)
        {
            if (textures[i].semantic == semantic)
                return &textures[i];
        }
        return nullptr;
    }

    const ConstantBufferBindingDesc* FindConstantBufferBinding(GDXShaderConstantBufferSlot semantic) const noexcept
    {
        for (uint32_t i = 0; i < constantBufferCount; ++i)
        {
            if (constantBuffers[i].semantic == semantic)
                return &constantBuffers[i];
        }
        return nullptr;
    }

    bool HasBindingsForScope(ResourceBindingScope scope) const noexcept
    {
        for (uint32_t i = 0; i < textureCount; ++i)
        {
            if (textures[i].scope == scope)
                return true;
        }

        for (uint32_t i = 0; i < constantBufferCount; ++i)
        {
            if (constantBuffers[i].scope == scope)
                return true;
        }

        return false;
    }
};

inline void HashResourceBindingValue(uint64_t& hash, uint64_t value) noexcept
{
    constexpr uint64_t kBindingHashPrime = 1099511628211ull;
    hash ^= value;
    hash *= kBindingHashPrime;
}

inline uint64_t BuildResourceBindingScopeKey(const ResourceBindingSet& set,
                                             ResourceBindingScope scope,
                                             uint32_t stableOwnerValue) noexcept
{
    constexpr uint64_t kBindingHashOffset = 1469598103934665603ull;

    uint64_t hash = kBindingHashOffset;
    HashResourceBindingValue(hash, static_cast<uint64_t>(scope));
    HashResourceBindingValue(hash, stableOwnerValue);

    for (uint32_t i = 0; i < set.textureCount; ++i)
    {
        const auto& binding = set.textures[i];
        if (binding.scope != scope)
            continue;

        HashResourceBindingValue(hash, 0x54584eull);
        HashResourceBindingValue(hash, static_cast<uint64_t>(binding.semantic));
        HashResourceBindingValue(hash, static_cast<uint64_t>(binding.bindingIndex));
        HashResourceBindingValue(hash, static_cast<uint64_t>(binding.bindingGroup));
        HashResourceBindingValue(hash, static_cast<uint64_t>(binding.resourceClass));
        HashResourceBindingValue(hash, static_cast<uint64_t>(binding.visibility));
        HashResourceBindingValue(hash, static_cast<uint64_t>(binding.texture.value));
        HashResourceBindingValue(hash, static_cast<uint64_t>(binding.uvSet));
        HashResourceBindingValue(hash, static_cast<uint64_t>(binding.enabled ? 1u : 0u));
        HashResourceBindingValue(hash, static_cast<uint64_t>(binding.expectsSRGB ? 1u : 0u));
        HashResourceBindingValue(hash, static_cast<uint64_t>(binding.requiredState));
        HashResourceBindingValue(hash, static_cast<uint64_t>(binding.required ? 1u : 0u));
    }

    for (uint32_t i = 0; i < set.constantBufferCount; ++i)
    {
        const auto& binding = set.constantBuffers[i];
        if (binding.scope != scope)
            continue;

        HashResourceBindingValue(hash, 0x43425546ull);
        HashResourceBindingValue(hash, static_cast<uint64_t>(binding.semantic));
        HashResourceBindingValue(hash, static_cast<uint64_t>(binding.bindingIndex));
        HashResourceBindingValue(hash, static_cast<uint64_t>(binding.bindingGroup));
        HashResourceBindingValue(hash, static_cast<uint64_t>(binding.resourceClass));
        HashResourceBindingValue(hash, static_cast<uint64_t>(binding.visibility));
        HashResourceBindingValue(hash, static_cast<uint64_t>(binding.enabled ? 1u : 0u));
        HashResourceBindingValue(hash, static_cast<uint64_t>(binding.required ? 1u : 0u));
    }

    return hash;
}
