#pragma once

#include "RenderPassTargetDesc.h"
#include "FrameData.h"

struct BackendRenderPassDesc
{
    enum class Kind
    {
        Graphics,
        Shadow
    };

    Kind kind = Kind::Graphics;
    RenderPassTargetDesc target{};
    const FrameData* frame = nullptr;

    static BackendRenderPassDesc Shadow(const FrameData& inFrame)
    {
        BackendRenderPassDesc d{};
        d.kind = Kind::Shadow;
        d.frame = &inFrame;
        return d;
    }

    static BackendRenderPassDesc Graphics(const RenderPassTargetDesc& inTarget,
                                          const FrameData* inFrame = nullptr)
    {
        BackendRenderPassDesc d{};
        d.kind = Kind::Graphics;
        d.target = inTarget;
        d.frame = inFrame;
        return d;
    }
};
