#pragma once

#include "Handle.h"
#include "RenderPassClearDesc.h"
#include "GDXResourceState.h"
#include <cstdint>
#include <string>
#include <utility>

struct RenderPassTargetDesc
{
    RenderTargetHandle renderTarget = RenderTargetHandle::Invalid();
    bool useBackbuffer = true;
    RenderPassClearDesc clear{};
    float viewportWidth = 0.0f;
    float viewportHeight = 0.0f;
    GDXResourceLifetime lifetime = GDXResourceLifetime::Persistent;
    GDXResourceTemporalScope temporalScope = GDXResourceTemporalScope::LongLived;
    std::wstring debugName;

    static RenderPassTargetDesc Backbuffer(float width, float height)
    {
        RenderPassTargetDesc d;
        d.useBackbuffer = true;
        d.viewportWidth = width;
        d.viewportHeight = height;
        d.lifetime = GDXResourceLifetime::External;
        d.temporalScope = GDXResourceTemporalScope::LongLived;
        d.debugName = L"Backbuffer";
        return d;
    }

    static RenderPassTargetDesc Offscreen(RenderTargetHandle target, const RenderPassClearDesc& clearDesc,
                                          float width, float height, std::wstring name = {})
    {
        RenderPassTargetDesc d;
        d.renderTarget = target;
        d.useBackbuffer = false;
        d.clear = clearDesc;
        d.viewportWidth = width;
        d.viewportHeight = height;
        d.lifetime = GDXResourceLifetime::Transient;
        d.temporalScope = GDXResourceTemporalScope::PerFrame;
        d.debugName = std::move(name);
        return d;
    }
};
