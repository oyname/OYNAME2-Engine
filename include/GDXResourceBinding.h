#pragma once

#include "Handle.h"
#include "GDXTextureSlots.h"
#include "GDXResourceState.h"
#include "GDXShaderLayout.h"

#include <array>
#include <cstdint>

enum class GDXBindingScope : uint8_t
{
    Pass = 0,
    Material = 1,
    Draw = 2,
};

struct ShaderResourceBindingDesc
{
    ShaderResourceSemantic semantic = ShaderResourceSemantic::Albedo;
    uint8_t bindingIndex = 255u;
    TextureHandle texture = TextureHandle::Invalid();
    void* nativeView = nullptr;
    MaterialTextureUVSet uvSet = MaterialTextureUVSet::UV0;
    GDXBindingScope scope = GDXBindingScope::Material;
    bool enabled = false;
    bool expectsSRGB = false;
    ResourceState requiredState = ResourceState::ShaderRead;
};

struct ConstantBufferBindingDesc
{
    GDXShaderConstantBufferSlot semantic = GDXShaderConstantBufferSlot::Entity;
    uint8_t vsRegister = 255u;
    uint8_t psRegister = 255u;
    void* buffer = nullptr;
    GDXBindingScope scope = GDXBindingScope::Draw;
    bool enabled = false;
};

struct ResourceBindingSet
{
    static constexpr size_t MaxTextureBindings = static_cast<size_t>(ShaderResourceSemantic::Count);
    static constexpr size_t MaxConstantBufferBindings = 8u;

    std::array<ShaderResourceBindingDesc, MaxTextureBindings> textures{};
    std::array<ConstantBufferBindingDesc, MaxConstantBufferBindings> constantBuffers{};
    uint32_t textureCount = 0u;
    uint32_t constantBufferCount = 0u;
    uint32_t layoutKey = 0u;
    uint64_t bindingKey = 0ull;

    void Clear() noexcept
    {
        textureCount = 0u;
        constantBufferCount = 0u;
        layoutKey = 0u;
        bindingKey = 0ull;
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


    static uint64_t MakeStableKey(const ResourceBindingSet& set) noexcept
    {
        auto mix = [](uint64_t h, uint64_t v) noexcept
        {
            h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
            return h;
        };

        uint64_t key = 1469598103934665603ull;
        key = mix(key, set.layoutKey);
        key = mix(key, set.textureCount);
        key = mix(key, set.constantBufferCount);

        for (uint32_t i = 0; i < set.textureCount; ++i)
        {
            const auto& t = set.textures[i];
            key = mix(key, static_cast<uint64_t>(t.semantic));
            key = mix(key, static_cast<uint64_t>(t.bindingIndex));
            key = mix(key, static_cast<uint64_t>(t.scope));
            key = mix(key, static_cast<uint64_t>(t.enabled ? 1u : 0u));
            key = mix(key, static_cast<uint64_t>(t.texture.value));
            key = mix(key, reinterpret_cast<uintptr_t>(t.nativeView));
        }

        for (uint32_t i = 0; i < set.constantBufferCount; ++i)
        {
            const auto& cb = set.constantBuffers[i];
            key = mix(key, static_cast<uint64_t>(cb.semantic));
            key = mix(key, static_cast<uint64_t>(cb.vsRegister));
            key = mix(key, static_cast<uint64_t>(cb.psRegister));
            key = mix(key, static_cast<uint64_t>(cb.scope));
            key = mix(key, static_cast<uint64_t>(cb.enabled ? 1u : 0u));
            key = mix(key, reinterpret_cast<uintptr_t>(cb.buffer));
        }

        return key;
    }
};
