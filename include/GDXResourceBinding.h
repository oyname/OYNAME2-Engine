#pragma once

#include "Handle.h"
#include "GDXTextureSlots.h"
#include "GDXResourceState.h"

#include <array>
#include <cstdint>

struct ShaderResourceBindingDesc
{
    ShaderResourceSemantic semantic = ShaderResourceSemantic::Albedo;
    uint8_t bindingIndex = 0u;
    TextureHandle texture = TextureHandle::Invalid();
    MaterialTextureUVSet uvSet = MaterialTextureUVSet::UV0;
    bool enabled = false;
    bool expectsSRGB = false;
    ResourceState requiredState = ResourceState::ShaderRead;
};

struct ResourceBindingSet
{
    static constexpr size_t MaxTextureBindings = static_cast<size_t>(ShaderResourceSemantic::Count);

    std::array<ShaderResourceBindingDesc, MaxTextureBindings> textures{};
    uint32_t textureCount = 0u;
    void* materialConstantBuffer = nullptr;
    uint8_t materialConstantBufferSlot = 2u;

    void Clear() noexcept
    {
        textureCount = 0u;
        materialConstantBuffer = nullptr;
        materialConstantBufferSlot = 2u;
        for (auto& t : textures)
            t = {};
    }

    void AddTextureBinding(const ShaderResourceBindingDesc& desc) noexcept
    {
        if (textureCount >= textures.size())
            return;
        textures[textureCount++] = desc;
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
};
