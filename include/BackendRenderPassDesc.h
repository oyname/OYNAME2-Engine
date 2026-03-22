#pragma once

#include "RenderPassTargetDesc.h"
#include "FrameData.h"
#include "RenderCommand.h"

// Forward declarations — avoid pulling in full ICommandList.h here.
class ICommandList;

struct BackendRenderPassDesc
{
    enum class Kind
    {
        Graphics,
        Shadow
    };

    Kind kind = Kind::Graphics;
    RenderPass pass = RenderPass::Opaque;
    RenderPassTargetDesc target{};
    const FrameData* frame = nullptr;

    // Graphics pass: pre-split by the planning layer.
    // opaqueList  — opaque + alpha-tested draws (depth write on).
    // alphaList   — transparent draws (depth write off, back-to-front sorted).
    // Both are nullptr for Shadow passes.
    // This split is a render-planning decision; the backend only executes.
    const ICommandList* opaqueList = nullptr;
    const ICommandList* alphaList  = nullptr;

    static BackendRenderPassDesc Shadow(const FrameData& inFrame)
    {
        BackendRenderPassDesc d{};
        d.kind  = Kind::Shadow;
        d.pass  = RenderPass::Shadow;
        d.frame = &inFrame;
        return d;
    }

    static BackendRenderPassDesc Graphics(const RenderPassTargetDesc& inTarget,
                                          const FrameData* inFrame = nullptr,
                                          RenderPass inPass = RenderPass::Opaque)
    {
        BackendRenderPassDesc d{};
        d.kind   = Kind::Graphics;
        d.pass   = inPass;
        d.target = inTarget;
        d.frame  = inFrame;
        return d;
    }
};
