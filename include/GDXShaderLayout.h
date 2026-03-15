#pragma once

#include "GDXVertexFormat.h"

#include <array>
#include <cstdint>

// ---------------------------------------------------------------------------
// GDXShaderLayout — backend-neutrale Shader-Metadaten.
//
// Kein Reflection-Ersatz. Die Idee ist kleiner und pragmatisch:
//   - welche Vertex-Streams der Shader erwartet
//   - welche ConstantBuffer-Bindings GIDX belegt
//   - welche Textur-Slots semantisch an welchen Registern hängen
//
// Das reicht, damit ECS-Renderer, Materialsystem und spätere Backends dieselbe
// Sprache sprechen.
// ---------------------------------------------------------------------------

enum class GDXShaderConstantBufferSlot : uint8_t
{
    Entity,
    Frame,
    Material,
    Skin,
    Light,
};

enum class GDXShaderTextureSemantic : uint8_t
{
    Albedo,
    Normal,
    ORM,
    Emissive,
    Detail,
    ShadowMap,
};

struct GDXShaderConstantBufferBinding
{
    GDXShaderConstantBufferSlot slot = GDXShaderConstantBufferSlot::Entity;
    uint8_t vsRegister = 0u;
    uint8_t psRegister = 0u;
};

struct GDXShaderTextureBinding
{
    GDXShaderTextureSemantic semantic = GDXShaderTextureSemantic::Albedo;
    uint8_t shaderRegister = 0u;
};

struct GDXShaderLayout
{
    GDXVertexFormatDesc vertexFormat;
    std::array<GDXShaderConstantBufferBinding, 5> constantBuffers{};
    std::array<GDXShaderTextureBinding, 6> textureBindings{};
    uint32_t constantBufferCount = 0u;
    uint32_t textureBindingCount = 0u;
    bool expectsShadowMap = false;
    bool depthOnly = false;

    void AddConstantBuffer(GDXShaderConstantBufferSlot slot, uint8_t vsRegister, uint8_t psRegister) noexcept
    {
        constantBuffers[constantBufferCount++] = { slot, vsRegister, psRegister };
    }

    void AddTexture(GDXShaderTextureSemantic semantic, uint8_t shaderRegister) noexcept
    {
        textureBindings[textureBindingCount++] = { semantic, shaderRegister };
    }
};

namespace GDXShaderLayouts
{
    inline GDXShaderLayout BuildMain(uint32_t vertexFlags, bool supportsSkinning) noexcept
    {
        GDXShaderLayout l{};
        l.vertexFormat = GDXVertexFormat::FromFlags(vertexFlags);
        l.AddConstantBuffer(GDXShaderConstantBufferSlot::Entity, 0u, 255u);
        l.AddConstantBuffer(GDXShaderConstantBufferSlot::Frame, 1u, 1u);
        l.AddConstantBuffer(GDXShaderConstantBufferSlot::Material, 255u, 2u);
        if (supportsSkinning)
            l.AddConstantBuffer(GDXShaderConstantBufferSlot::Skin, 4u, 255u);

        l.AddTexture(GDXShaderTextureSemantic::Albedo,   0u);
        l.AddTexture(GDXShaderTextureSemantic::Normal,   1u);
        l.AddTexture(GDXShaderTextureSemantic::ORM,      2u);
        l.AddTexture(GDXShaderTextureSemantic::Emissive, 3u);
        l.AddTexture(GDXShaderTextureSemantic::Detail,   4u);
        l.AddTexture(GDXShaderTextureSemantic::ShadowMap, 16u);
        l.expectsShadowMap = true;
        return l;
    }

    inline GDXShaderLayout BuildShadow(uint32_t vertexFlags, bool supportsSkinning) noexcept
    {
        GDXShaderLayout l{};
        l.vertexFormat = GDXVertexFormat::FromFlags(vertexFlags);
        l.AddConstantBuffer(GDXShaderConstantBufferSlot::Entity, 0u, 255u);
        l.AddConstantBuffer(GDXShaderConstantBufferSlot::Frame, 1u, 1u);
        l.AddConstantBuffer(GDXShaderConstantBufferSlot::Material, 255u, 2u);
        if (supportsSkinning)
            l.AddConstantBuffer(GDXShaderConstantBufferSlot::Skin, 4u, 255u);
        l.depthOnly = true;
        return l;
    }
}
