#pragma once

#include "RenderPassTargetDesc.h"
#include "FrameData.h"
#include "RenderCommand.h"

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

    static BackendRenderPassDesc Shadow(const FrameData& inFrame)
    {
        BackendRenderPassDesc d{};
        d.kind = Kind::Shadow;
        d.pass = RenderPass::Shadow;
        d.frame = &inFrame;
        return d;
    }

    static BackendRenderPassDesc Graphics(const RenderPassTargetDesc& inTarget,
                                          const FrameData* inFrame = nullptr,
                                          RenderPass inPass = RenderPass::Opaque)
    {
        BackendRenderPassDesc d{};
        d.kind = Kind::Graphics;
        d.pass = inPass;
        d.target = inTarget;
        d.frame = inFrame;
        return d;
    }
};
