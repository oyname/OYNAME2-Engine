#pragma once

#include "RenderPassTargetDesc.h"
#include "FrameData.h"
#include "RenderCommand.h"
#include "GDXResourceState.h"
#include "GDXPassExecutionModel.h"
#include "Particles/IGDXParticleRenderer.h"

class ICommandList;

struct BackendPlannedTransition
{
    TextureHandle texture = TextureHandle::Invalid();
    RenderTargetHandle renderTarget = RenderTargetHandle::Invalid();
    ResourceState before = ResourceState::Unknown;
    ResourceState after = ResourceState::Unknown;
    const char* debugName = "";
};

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
    bool bindNormalsTarget = false;
    const FrameData* frame = nullptr;
    GDXPassExecutionInfo execution{};

    const ICommandList* opaqueList = nullptr;
    const ICommandList* alphaList = nullptr;
    const ParticleRenderSubmission* particleSubmission = nullptr;

    static BackendRenderPassDesc Shadow(const FrameData& inFrame)
    {
        BackendRenderPassDesc d{};
        d.kind = Kind::Shadow;
        d.pass = RenderPass::Shadow;
        d.frame = &inFrame;
        d.execution.executionClass = GDXPassExecutionClass::Shadow;
        d.execution.commandEncoding = GDXCommandEncoding::DrawQueue;
        return d;
    }

    static BackendRenderPassDesc Graphics(const RenderPassTargetDesc& inTarget,
                                          const FrameData* inFrame = nullptr,
                                          RenderPass inPass = RenderPass::Opaque,
                                          bool inBindNormalsTarget = false)
    {
        BackendRenderPassDesc d{};
        d.kind = Kind::Graphics;
        d.pass = inPass;
        d.target = inTarget;
        d.bindNormalsTarget = inBindNormalsTarget;
        d.frame = inFrame;
        d.execution.executionClass = GDXPassExecutionClass::Graphics;
        d.execution.commandEncoding = GDXCommandEncoding::DrawQueue;
        d.execution.updatesFrameConstants = true;
        return d;
    }
};
