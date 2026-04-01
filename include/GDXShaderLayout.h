#pragma once

#include "GDXVertexFormat.h"
#include "GDXRenderBindingModel.h"
#include "GDXResourceBinding.h"

#include <array>
#include <cstdint>

// ---------------------------------------------------------------------------
// GDXShaderLayout — backend-neutrale Shader-Metadaten.
//
// Kein Reflection-Ersatz. Die Idee ist kleiner und pragmatisch:
//   - welche Vertex-Streams der Shader erwartet
//   - welche ConstantBuffer-Bindings KROM belegt
//   - welche semantischen Textur-Bindings der Shader erwartet
//
// Das reicht, damit ECS-Renderer, Materialsystem und spätere Backends dieselbe
// Sprache sprechen.
// ---------------------------------------------------------------------------

struct GDXShaderLayout
{
    GDXVertexFormatDesc vertexFormat;
    std::array<GDXShaderConstantBufferBinding, 5> constantBuffers{};
    std::array<GDXShaderTextureBinding, 6> textureBindings{};
    uint32_t constantBufferCount = 0u;
    uint32_t textureBindingCount = 0u;
    bool expectsShadowMap = false;
    bool depthOnly = false;

    void AddConstantBuffer(GDXShaderConstantBufferSlot slot, GDXShaderStageVisibility visibility) noexcept
    {
        GDXShaderConstantBufferBinding b{};
        b.slot = slot;
        b.bindingGroup = GDXBindingGroupFromScope(GDXBindingScopeForConstantBufferSlot(slot));
        b.resourceClass = GDXBoundResourceClass::ConstantBuffer;
        b.visibility = visibility;
        b.layoutBindingIndex = constantBufferCount;
        if (constantBufferCount >= constantBuffers.size())
            return;
        constantBuffers[constantBufferCount++] = b;
    }

    void AddTexture(GDXShaderTextureSemantic semantic) noexcept
    {
        GDXShaderTextureBinding b{};
        b.semantic = semantic;
        b.bindingGroup = (semantic == GDXShaderTextureSemantic::ShadowMap)
            ? GDXBindingGroup::Pass
            : GDXBindingGroup::Material;
        b.resourceClass = GDXBoundResourceClass::Texture;
        b.visibility = GDXShaderStageVisibility::Pixel;
        b.layoutBindingIndex = textureBindingCount;
        if (textureBindingCount >= textureBindings.size())
            return;
        textureBindings[textureBindingCount++] = b;
    }
};

inline GDXPipelineLayoutDesc BuildPipelineLayoutFromShaderLayout(const GDXShaderLayout& layout) noexcept
{
    GDXPipelineLayoutDesc out{};
    out.Reset();
    for (uint32_t i = 0; i < layout.constantBufferCount; ++i)
        out.AddConstantBufferBinding(layout.constantBuffers[i]);
    for (uint32_t i = 0; i < layout.textureBindingCount; ++i)
        out.AddTextureBinding(layout.textureBindings[i]);
    return out;
}

namespace GDXShaderLayouts
{
    inline GDXShaderLayout BuildMain(uint32_t vertexFlags, bool supportsSkinning) noexcept
    {
        GDXShaderLayout l{};
        l.vertexFormat = GDXVertexFormat::FromFlags(vertexFlags);
        l.AddConstantBuffer(GDXShaderConstantBufferSlot::Entity, GDXShaderStageVisibility::Vertex);
        l.AddConstantBuffer(GDXShaderConstantBufferSlot::Frame, GDXShaderStageVisibility::AllGraphics);
        l.AddConstantBuffer(GDXShaderConstantBufferSlot::Material, GDXShaderStageVisibility::Pixel);
        if (supportsSkinning)
            l.AddConstantBuffer(GDXShaderConstantBufferSlot::Skin, GDXShaderStageVisibility::Vertex);

        l.AddTexture(GDXShaderTextureSemantic::Albedo);
        l.AddTexture(GDXShaderTextureSemantic::Normal);
        l.AddTexture(GDXShaderTextureSemantic::ORM);
        l.AddTexture(GDXShaderTextureSemantic::Emissive);
        l.AddTexture(GDXShaderTextureSemantic::Detail);
        l.AddTexture(GDXShaderTextureSemantic::ShadowMap);
        l.expectsShadowMap = true;
        return l;
    }

    inline GDXShaderLayout BuildShadow(uint32_t vertexFlags, bool supportsSkinning, bool alphaTest) noexcept
    {
        GDXShaderLayout l{};
        l.vertexFormat = GDXVertexFormat::FromFlags(vertexFlags);
        l.AddConstantBuffer(GDXShaderConstantBufferSlot::Entity, GDXShaderStageVisibility::Vertex);
        l.AddConstantBuffer(GDXShaderConstantBufferSlot::Frame, GDXShaderStageVisibility::AllGraphics);

        if (alphaTest)
        {
            l.AddConstantBuffer(GDXShaderConstantBufferSlot::Material, GDXShaderStageVisibility::Pixel);
            l.AddTexture(GDXShaderTextureSemantic::Albedo);
        }

        if (supportsSkinning)
            l.AddConstantBuffer(GDXShaderConstantBufferSlot::Skin, GDXShaderStageVisibility::Vertex);

        l.depthOnly = true;
        return l;
    }
}
