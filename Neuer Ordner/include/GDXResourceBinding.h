#pragma once

#include "Handle.h"
#include "GDXTextureSlots.h"
#include "GDXResourceState.h"
#include "GDXShaderLayout.h"

#include <array>
#include <cstdint>

enum class ResourceBindingScope : uint8_t
{
    Pass = 0,
    Material = 1,
    Draw = 2,
};

struct ShaderResourceBindingDesc
{
    ShaderResourceSemantic semantic = ShaderResourceSemantic::Albedo;
    uint8_t bindingIndex = 0u;
    TextureHandle texture = TextureHandle::Invalid();
    MaterialTextureUVSet uvSet = MaterialTextureUVSet::UV0;
    bool enabled = false;
    bool expectsSRGB = false;
    ResourceState requiredState = ResourceState::ShaderRead;
    ResourceBindingScope scope = ResourceBindingScope::Material;
};

struct ConstantBufferBindingDesc
{
    GDXShaderConstantBufferSlot semantic = GDXShaderConstantBufferSlot::Entity;
    uint8_t vsRegister = 255u;
    uint8_t psRegister = 255u;
    void* buffer = nullptr;
    bool enabled = false;
    ResourceBindingScope scope = ResourceBindingScope::Draw;
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
        HashResourceBindingValue(hash, static_cast<uint64_t>(binding.texture.value));
        HashResourceBindingValue(hash, static_cast<uint64_t>(binding.uvSet));
        HashResourceBindingValue(hash, static_cast<uint64_t>(binding.enabled ? 1u : 0u));
        HashResourceBindingValue(hash, static_cast<uint64_t>(binding.expectsSRGB ? 1u : 0u));
        HashResourceBindingValue(hash, static_cast<uint64_t>(binding.requiredState));
    }

    for (uint32_t i = 0; i < set.constantBufferCount; ++i)
    {
        const auto& binding = set.constantBuffers[i];
        if (binding.scope != scope)
            continue;

        HashResourceBindingValue(hash, 0x43425546ull);
        HashResourceBindingValue(hash, static_cast<uint64_t>(binding.semantic));
        HashResourceBindingValue(hash, static_cast<uint64_t>(binding.vsRegister));
        HashResourceBindingValue(hash, static_cast<uint64_t>(binding.psRegister));
        HashResourceBindingValue(hash, static_cast<uint64_t>(binding.enabled ? 1u : 0u));
    }

    return hash;
}
