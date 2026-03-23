#pragma once

#include "PostProcessResource.h"

#include <vector>

inline TextureHandle ResolvePostProcessInputTexture(const PostProcessInputSlot& slot,
                                                    const PostProcessExecutionInputs& inputs)
{
    switch (slot.semantic)
    {
    case PostProcessInputSemantic::SceneColor: return inputs.sceneColor;
    case PostProcessInputSemantic::OriginalSceneColor: return inputs.originalSceneColor;
    case PostProcessInputSemantic::SceneDepth: return inputs.sceneDepth;
    case PostProcessInputSemantic::SceneNormals: return inputs.sceneNormals;
    case PostProcessInputSemantic::Custom: return slot.customTexture;
    default: return TextureHandle::Invalid();
    }
}

inline std::vector<ResolvedPostProcessBinding> ResolvePostProcessBindings(
    const std::vector<PostProcessInputSlot>& slots,
    const PostProcessExecutionInputs& inputs,
    bool* outHasMissingRequired = nullptr)
{
    bool hasMissingRequired = false;
    std::vector<ResolvedPostProcessBinding> bindings;
    bindings.reserve(slots.size());
    for (const auto& slot : slots)
    {
        const TextureHandle resolved = ResolvePostProcessInputTexture(slot, inputs);
        if (!resolved.IsValid() && slot.required)
            hasMissingRequired = true;
        bindings.push_back(ResolvedPostProcessBinding{ slot.name, slot.shaderRegister, resolved, slot.required });
    }
    if (outHasMissingRequired) *outHasMissingRequired = hasMissingRequired;
    return bindings;
}
