#pragma once

#include "RenderPassTargetDesc.h"
#include "FrameData.h"
#include "RenderCommand.h"
#include "GDXResourceState.h"
#include "GDXPassExecutionModel.h"
#include "GDXResourceStatePlanner.h"

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
    GDXPassExecutionInfo execution{};
    GDXPassResourceUsageSet resources{};

    // Graphics pass: pre-split by the planning layer.
    // opaqueList  — opaque + alpha-tested draws (depth write on).
    // alphaList   — transparent draws (depth write off, back-to-front sorted).
    // Both are nullptr for Shadow passes.
    const ICommandList* opaqueList = nullptr;
    const ICommandList* alphaList  = nullptr;

    void ClearResourceUsages() noexcept
    {
        resources.Clear();
    }

    void NormalizeResourceUsages() noexcept
    {
        GDXNormalizePassResourceUsageSet(resources);
    }

    bool AddResourceUsage(const GDXPassResourceUsageEntry& entry) noexcept
    {
        GDXPassResourceUsageEntry normalized = entry;
        GDXNormalizePassResourceUsageEntry(normalized);
        return resources.Add(normalized);
    }

    static BackendRenderPassDesc Shadow(const FrameData& inFrame)
    {
        BackendRenderPassDesc d{};
        d.kind  = Kind::Shadow;
        d.pass  = RenderPass::Shadow;
        d.frame = &inFrame;
        d.execution.executionClass = GDXPassExecutionClass::Shadow;
        d.execution.commandEncoding = GDXCommandEncoding::DrawQueue;
        d.resources.Add({ GDXPassResourceKind::ShadowMap, GDXPassResourceAccess::DepthTarget,
                          TextureHandle::Invalid(), RenderTargetHandle::Invalid(), L"ShadowMap",
                          ResourceState::ShaderRead, ResourceState::DepthWrite, ResourceState::ShaderRead });
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
        d.execution.executionClass = GDXPassExecutionClass::Graphics;
        d.execution.commandEncoding = GDXCommandEncoding::DrawQueue;
        d.execution.updatesFrameConstants = true;
        return d;
    }
};
