#pragma once

#include "PostProcessResource.h"
#include "GDXBindingGroupData.h"

#include <vector>

inline PostProcessRuntimeTextureRef BuildPostProcessRuntimeTextureRef(const PostProcessInputSlot& slot)
{
    PostProcessRuntimeTextureRef ref{};
    switch (slot.semantic)
    {
    case PostProcessInputSemantic::SceneColor:
        ref.source = PostProcessRuntimeTextureSource::SceneColorCurrent;
        break;
    case PostProcessInputSemantic::OriginalSceneColor:
        ref.source = PostProcessRuntimeTextureSource::OriginalSceneColorBranch;
        break;
    case PostProcessInputSemantic::SceneDepth:
        ref.source = PostProcessRuntimeTextureSource::SceneDepth;
        break;
    case PostProcessInputSemantic::SceneNormals:
        ref.source = PostProcessRuntimeTextureSource::SceneNormals;
        break;
    case PostProcessInputSemantic::Custom:
        ref.source = PostProcessRuntimeTextureSource::CustomTexture;
        ref.customTexture = slot.customTexture;
        break;
    case PostProcessInputSemantic::ShadowMap:
        ref.source = PostProcessRuntimeTextureSource::ShadowMap;
        break;
    default:
        ref.source = PostProcessRuntimeTextureSource::SceneColorCurrent;
        break;
    }
    return ref;
}

inline bool IsPostProcessRuntimeTextureRefLocallyResolvable(
    const PostProcessRuntimeTextureRef& ref)
{
    switch (ref.source)
    {
    case PostProcessRuntimeTextureSource::CustomTexture:
        return ref.customTexture.IsValid();
    case PostProcessRuntimeTextureSource::SceneColorCurrent:
    case PostProcessRuntimeTextureSource::OriginalSceneColorBranch:
    case PostProcessRuntimeTextureSource::OriginalSceneColorFallback:
    case PostProcessRuntimeTextureSource::SceneDepth:
    case PostProcessRuntimeTextureSource::SceneNormals:
    case PostProcessRuntimeTextureSource::ShadowMap:
        return true;
    default:
        return false;
    }
}

inline std::vector<ResolvedPostProcessBinding> ResolvePostProcessBindings(
    const std::vector<PostProcessInputSlot>& slots,
    const PostProcessExecutionInputs& inputs,
    bool* outHasMissingRequired = nullptr)
{
    (void)inputs;

    bool hasMissingRequired = false;
    std::vector<ResolvedPostProcessBinding> bindings;
    bindings.reserve(slots.size());
    for (const auto& slot : slots)
    {
        const PostProcessRuntimeTextureRef ref = BuildPostProcessRuntimeTextureRef(slot);
        if (!IsPostProcessRuntimeTextureRefLocallyResolvable(ref) && slot.required)
            hasMissingRequired = true;

        ResolvedPostProcessBinding binding{};
        binding.name = slot.name;
        binding.shaderRegister = slot.shaderRegister;
        binding.semantic = slot.semantic;
        binding.textureRef = ref;
        binding.required = slot.required;
        bindings.push_back(binding);
    }
    if (outHasMissingRequired) *outHasMissingRequired = hasMissingRequired;
    return bindings;
}


inline GDXDescriptorSetBuildDesc BuildPostProcessDescriptorSetBuildDesc(
    const std::vector<PostProcessInputSlot>& slots,
    bool includeConstantBuffer,
    uint32_t constantBufferBindingIndex = 0u) noexcept
{
    GDXDescriptorSetBuildDesc out{};
    out.Reset(ResourceBindingScope::Pass);
    out.bindingGroup = GDXBindingGroup::Pass;

    for (const auto& slot : slots)
    {
        ShaderResourceBindingDesc desc{};
        desc.semantic = (slot.semantic == PostProcessInputSemantic::ShadowMap)
            ? ShaderResourceSemantic::ShadowMap
            : ShaderResourceSemantic::Detail;
        desc.bindingIndex = slot.shaderRegister;
        desc.bindingGroup = GDXBindingGroup::Pass;
        desc.resourceClass = GDXBoundResourceClass::Texture;
        desc.visibility = GDXShaderStageVisibility::Pixel;
        desc.texture = slot.customTexture;
        desc.enabled = true;
        desc.required = slot.required;
        desc.scope = ResourceBindingScope::Pass;
        out.AddTexture(desc);
    }

    if (includeConstantBuffer)
    {
        ConstantBufferBindingDesc cb{};
        cb.semantic = GDXShaderConstantBufferSlot::Pass;
        cb.bindingIndex = constantBufferBindingIndex;
        cb.bindingGroup = GDXBindingGroup::Pass;
        cb.resourceClass = GDXBoundResourceClass::ConstantBuffer;
        cb.visibility = GDXShaderStageVisibility::AllGraphics;
        cb.enabled = true;
        cb.required = true;
        cb.scope = ResourceBindingScope::Pass;
        out.AddConstantBuffer(cb);
    }

    return out;
}
