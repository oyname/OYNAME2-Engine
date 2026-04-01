#pragma once

#include "GDXRenderBindingModel.h"
#include "GDXTextureSlots.h"

#include <cstdint>

// DX11-spezifische Register-/Binding-Uebersetzung. Core-Layout und Recorded
// Bindings bleiben rein semantisch; erst hier wird auf konkrete D3D11-Slots
// abgebildet.
constexpr uint8_t GDXDX11PixelShaderRegisterForSemantic(ShaderResourceSemantic semantic) noexcept
{
    switch (semantic)
    {
    case ShaderResourceSemantic::Albedo:   return 0u;
    case ShaderResourceSemantic::Normal:   return 1u;
    case ShaderResourceSemantic::ORM:      return 2u;
    case ShaderResourceSemantic::Emissive: return 3u;
    case ShaderResourceSemantic::Detail:   return 4u;
    case ShaderResourceSemantic::ShadowMap:return 16u;
    default:                               return 0u;
    }
}

constexpr uint8_t GDXDX11PixelShaderRegisterForSemantic(GDXShaderTextureSemantic semantic) noexcept
{
    switch (semantic)
    {
    case GDXShaderTextureSemantic::Albedo:   return 0u;
    case GDXShaderTextureSemantic::Normal:   return 1u;
    case GDXShaderTextureSemantic::ORM:      return 2u;
    case GDXShaderTextureSemantic::Emissive: return 3u;
    case GDXShaderTextureSemantic::Detail:   return 4u;
    case GDXShaderTextureSemantic::ShadowMap:return 16u;
    default:                                 return 0u;
    }
}

constexpr uint8_t GDXDX11BindingIndexForSemantic(ShaderResourceSemantic semantic) noexcept
{
    switch (semantic)
    {
    case ShaderResourceSemantic::Albedo:   return 0u;
    case ShaderResourceSemantic::Normal:   return 1u;
    case ShaderResourceSemantic::ORM:      return 2u;
    case ShaderResourceSemantic::Emissive: return 3u;
    case ShaderResourceSemantic::Detail:   return 4u;
    case ShaderResourceSemantic::ShadowMap:return 5u;
    default:                               return 0u;
    }
}

struct GDXDX11StageRegisterPair
{
    uint8_t vs = 255u;
    uint8_t ps = 255u;
};

constexpr uint8_t GDXDX11SafeConstantBufferRegister(uint32_t slot) noexcept
{
    // D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT == 14 (0..13)
    return slot < 14u ? static_cast<uint8_t>(slot) : 255u;
}

constexpr GDXDX11StageRegisterPair GDXDX11RegistersForConstantBufferBinding(
    GDXBindingGroup group,
    GDXShaderConstantBufferSlot slot,
    uint32_t fallbackBindingIndex,
    GDXShaderStageVisibility visibility) noexcept
{
    switch (group)
    {
    case GDXBindingGroup::Draw:
        if (slot == GDXShaderConstantBufferSlot::Entity)
            return { 0u, 255u };
        if (slot == GDXShaderConstantBufferSlot::Skin)
            return { 4u, 255u };
        break;
    case GDXBindingGroup::Pass:
        if (slot == GDXShaderConstantBufferSlot::Frame)
            return { 1u, 1u };
        if (slot == GDXShaderConstantBufferSlot::Pass)
            return { 0u, 0u };
        break;
    case GDXBindingGroup::Material:
        if (slot == GDXShaderConstantBufferSlot::Material)
            return { 255u, 2u };
        break;
    default:
        break;
    }

    GDXDX11StageRegisterPair regs{};
    if (HasStageVisibility(visibility, GDXShaderStageVisibility::Vertex))
        regs.vs = GDXDX11SafeConstantBufferRegister(fallbackBindingIndex);
    if (HasStageVisibility(visibility, GDXShaderStageVisibility::Pixel))
        regs.ps = GDXDX11SafeConstantBufferRegister(fallbackBindingIndex);
    return regs;
}

constexpr uint8_t GDXDX11PixelShaderRegisterForTextureBinding(const GDXShaderTextureBinding& binding) noexcept
{
    if (binding.bindingGroup == GDXBindingGroup::Material)
        return GDXDX11PixelShaderRegisterForSemantic(binding.semantic);
    if (binding.semantic == GDXShaderTextureSemantic::ShadowMap)
        return 16u;
    return static_cast<uint8_t>(binding.layoutBindingIndex);
}
