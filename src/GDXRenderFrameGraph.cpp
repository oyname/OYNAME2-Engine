#include "GDXRenderFrameGraph.h"
#include "IGDXRenderBackend.h"
#include "Core/Debug.h"

#include "GDXResourceState.h"

namespace
{
    const char* FGResourceStateToString(ResourceState state)
    {
        switch (state)
        {
        case ResourceState::Unknown:         return "Unknown";
        case ResourceState::Common:          return "Common";
        case ResourceState::ShaderRead:      return "ShaderRead";
        case ResourceState::RenderTarget:    return "RenderTarget";
        case ResourceState::DepthWrite:      return "DepthWrite";
        case ResourceState::DepthRead:       return "DepthRead";
        case ResourceState::UnorderedAccess: return "UnorderedAccess";
        case ResourceState::CopySource:      return "CopySource";
        case ResourceState::CopyDest:        return "CopyDest";
        case ResourceState::Present:         return "Present";
        default:                             return "<invalid>";
        }
    }

    ResourceState InferInitialStateForResource(const FGResourceDesc& resource)
    {
        switch (resource.kind)
        {
        case FGResourceKind::Backbuffer:   return ResourceState::Present;
        case FGResourceKind::Depth:        return ResourceState::ShaderRead;
        case FGResourceKind::Shadow:       return ResourceState::ShaderRead;
        case FGResourceKind::RenderTarget: return ResourceState::ShaderRead;
        case FGResourceKind::Texture:      return ResourceState::ShaderRead;
        case FGResourceKind::History:      return ResourceState::ShaderRead;
        case FGResourceKind::Unknown:
        default:                          return ResourceState::Common;
        }
    }

    ResourceState InferStableFinalStateForResource(const FGResourceDesc& resource, ResourceState currentState)
    {
        if (resource.kind == FGResourceKind::Backbuffer)
            return ResourceState::Present;

        if (resource.IsImported())
            return InferInitialStateForResource(resource);

        return currentState;
    }
}

#include <algorithm>
#include <cassert>
#include <cstring>
#include <vector>

// ---------------------------------------------------------------------------
// Hilfsfunktionen
// ---------------------------------------------------------------------------

namespace
{
    struct DepthDebugParams
    {
        float nearPlane = 0.1f;
        float farPlane = 1000.0f;
        uint32_t isOrtho = 0u;
        uint32_t flags = 1u;
    };

    void UpdatePerViewPostProcessConstants(
        const RFG::ExecuteData& exec,
        const std::vector<PostProcessHandle>& passOrder,
        ResourceStore<PostProcessResource, PostProcessTag>& postStore)
    {
        const DepthDebugParams depthParams{
            exec.presentation.postProcess.execInputs.cameraNearPlane,
            exec.presentation.postProcess.execInputs.cameraFarPlane,
            exec.presentation.postProcess.execInputs.cameraIsOrtho,
            exec.presentation.postProcess.execInputs.depthDebugFlags
        };

        for (const PostProcessHandle handle : passOrder)
        {
            PostProcessResource* pass = postStore.Get(handle);
            if (!pass || !pass->ready || !pass->enabled)
                continue;

            if (pass->desc.pixelShaderFile == L"PostProcessDepthDebugPS.hlsl")
            {
                if (pass->constantBufferBytes >= sizeof(DepthDebugParams) &&
                    pass->constantData.size() >= sizeof(DepthDebugParams))
                {
                    std::memcpy(pass->constantData.data(), &depthParams, sizeof(DepthDebugParams));
                    pass->cpuDirty = true;
                }
                continue;
            }

            if (pass->desc.pixelShaderFile == L"PostProcessGTAOPS.hlsl")
            {
                if (pass->constantBufferBytes >= sizeof(GTAOParams) &&
                    pass->constantData.size() >= sizeof(GTAOParams))
                {
                    GTAOParams* params = reinterpret_cast<GTAOParams*>(pass->constantData.data());
                    params->nearPlane = exec.presentation.postProcess.execInputs.cameraNearPlane;
                    params->farPlane = exec.presentation.postProcess.execInputs.cameraFarPlane;
                    params->projScaleX = exec.presentation.postProcess.execInputs.cameraProjScaleX;
                    params->projScaleY = exec.presentation.postProcess.execInputs.cameraProjScaleY;
                    params->cameraIsOrtho = exec.presentation.postProcess.execInputs.cameraIsOrtho;
                    pass->cpuDirty = true;
                }
                continue;
            }

            if (pass->desc.pixelShaderFile == L"PostProcessGTAOBlurPS.hlsl")
            {
                if (pass->constantBufferBytes >= sizeof(GTAOBlurParams) &&
                    pass->constantData.size() >= sizeof(GTAOBlurParams))
                {
                    GTAOBlurParams* params = reinterpret_cast<GTAOBlurParams*>(pass->constantData.data());
                    params->nearPlane = exec.presentation.postProcess.execInputs.cameraNearPlane;
                    params->farPlane = exec.presentation.postProcess.execInputs.cameraFarPlane;
                    params->cameraIsOrtho = exec.presentation.postProcess.execInputs.cameraIsOrtho;
                    pass->cpuDirty = true;
                }
                continue;
            }

            if (pass->desc.pixelShaderFile == L"PostProcessDepthFogPS.hlsl")
            {
                if (pass->constantBufferBytes >= sizeof(FogParams) &&
                    pass->constantData.size() >= sizeof(FogParams))
                {
                    FogParams* params = reinterpret_cast<FogParams*>(pass->constantData.data());
                    params->cameraNearPlane = exec.presentation.postProcess.execInputs.cameraNearPlane;
                    params->cameraFarPlane  = exec.presentation.postProcess.execInputs.cameraFarPlane;
                    params->projScaleX      = exec.presentation.postProcess.execInputs.cameraProjScaleX;
                    params->projScaleY      = exec.presentation.postProcess.execInputs.cameraProjScaleY;
                    params->cameraIsOrtho   = exec.presentation.postProcess.execInputs.cameraIsOrtho;
                    std::memcpy(params->invView, &exec.presentation.postProcess.execInputs.invViewMatrix, sizeof(params->invView));
                    pass->cpuDirty = true;
                }
                continue;
            }

            if (pass->desc.pixelShaderFile == L"PostProcessVolumetricFogPS.hlsl")
            {
                if (pass->constantBufferBytes >= sizeof(VolumetricFogParams) &&
                    pass->constantData.size() >= sizeof(VolumetricFogParams))
                {
                    VolumetricFogParams* params = reinterpret_cast<VolumetricFogParams*>(pass->constantData.data());
                    params->cameraNearPlane = exec.presentation.postProcess.execInputs.cameraNearPlane;
                    params->cameraFarPlane  = exec.presentation.postProcess.execInputs.cameraFarPlane;
                    params->projScaleX      = exec.presentation.postProcess.execInputs.cameraProjScaleX;
                    params->projScaleY      = exec.presentation.postProcess.execInputs.cameraProjScaleY;
                    params->cameraIsOrtho   = exec.presentation.postProcess.execInputs.cameraIsOrtho;
                    params->cascadeCount    = exec.presentation.postProcess.execInputs.shadowCascadeCount;

                    params->cameraPos[0] = exec.presentation.postProcess.execInputs.cameraPos.x;
                    params->cameraPos[1] = exec.presentation.postProcess.execInputs.cameraPos.y;
                    params->cameraPos[2] = exec.presentation.postProcess.execInputs.cameraPos.z;
                    params->cameraPos[3] = 0.0f;

                    params->lightDir[0] = exec.presentation.postProcess.execInputs.shadowLightDir.x;
                    params->lightDir[1] = exec.presentation.postProcess.execInputs.shadowLightDir.y;
                    params->lightDir[2] = exec.presentation.postProcess.execInputs.shadowLightDir.z;
                    params->lightDir[3] = 0.0f;

                    std::memcpy(params->invView, &exec.presentation.postProcess.execInputs.invViewMatrix, sizeof(params->invView));
                    for (uint32_t c = 0u; c < 4u; ++c)
                    {
                        std::memcpy(params->cascadeViewProj[c], &exec.presentation.postProcess.execInputs.shadowCascadeViewProj[c], sizeof(params->cascadeViewProj[c]));
                        params->cascadeSplits[c] = exec.presentation.postProcess.execInputs.shadowCascadeSplits[c];
                    }
                    pass->cpuDirty = true;
                }
                continue;
            }
        }
    }
}


const char* FGResourceLifetimeToString(FGResourceLifetime lifetime)
{
    switch (lifetime)
    {
    case FGResourceLifetime::Imported:  return "Imported";
    case FGResourceLifetime::Transient: return "Transient";
    default:                            return "InvalidLifetime";
    }
}

const char* FGResourceKindToString(FGResourceKind kind)
{
    switch (kind)
    {
    case FGResourceKind::Unknown:      return "Unknown";
    case FGResourceKind::Backbuffer:   return "Backbuffer";
    case FGResourceKind::Texture:      return "Texture";
    case FGResourceKind::RenderTarget: return "RenderTarget";
    case FGResourceKind::Depth:        return "Depth";
    case FGResourceKind::Shadow:       return "Shadow";
    case FGResourceKind::History:      return "History";
    default:                           return "InvalidKind";
    }
}

const char* FGResourceFormatToString(GDXTextureFormat format)
{
    switch (format)
    {
    case GDXTextureFormat::Unknown:             return "Unknown";
    case GDXTextureFormat::RGBA8_UNORM:         return "RGBA8_UNORM";
    case GDXTextureFormat::RGBA8_UNORM_SRGB:    return "RGBA8_UNORM_SRGB";
    case GDXTextureFormat::RGBA16_FLOAT:        return "RGBA16_FLOAT";
    case GDXTextureFormat::RG16_FLOAT:          return "RG16_FLOAT";
    case GDXTextureFormat::D24_UNORM_S8_UINT:   return "D24_UNORM_S8_UINT";
    case GDXTextureFormat::D32_FLOAT:           return "D32_FLOAT";
    default:                                    return "InvalidFormat";
    }
}

const char* FGResourceStateSourceToString(FGResourceStateSource source)
{
    switch (source)
    {
    case FGResourceStateSource::Unknown:          return "Unknown";
    case FGResourceStateSource::TransientCommon:  return "TransientCommon";
    case FGResourceStateSource::BackbufferPresent:return "BackbufferPresent";
    case FGResourceStateSource::ImportedFirstUse: return "ImportedFirstUse";
    case FGResourceStateSource::InferredFallback: return "InferredFallback";
    default:                                      return "InvalidStateSource";
    }
}

const char* FGShadowResourcePolicyToString(FGShadowResourcePolicy policy)
{
    switch (policy)
    {
    case FGShadowResourcePolicy::LocalPerView:        return "LocalPerView";
    case FGShadowResourcePolicy::GlobalSharedMainView:return "GlobalSharedMainView";
    default:                                          return "InvalidShadowPolicy";
    }
}

static bool MatrixEquals(const Matrix4& a, const Matrix4& b)
{
    return std::memcmp(&a, &b, sizeof(Matrix4)) == 0;
}

static bool CanShareMainViewShadow(const RFG::ViewPassData& consumer,
                                   const RFG::ViewPassData& mainView,
                                   std::string* outReason)
{
    if (consumer.shadowResourcePolicy != FGShadowResourcePolicy::GlobalSharedMainView)
    {
        if (outReason) *outReason = "view did not request GlobalSharedMainView";
        return false;
    }

    if (consumer.execute.shadowPass.enabled)
    {
        if (outReason) *outReason = "consumer has its own shadow pass and must stay local";
        return false;
    }

    if (!mainView.execute.shadowPass.enabled)
    {
        if (outReason) *outReason = "main view has no shadow producer";
        return false;
    }

    const FrameData& c = consumer.execute.frame;
    const FrameData& m = mainView.execute.frame;

    if (!c.hasShadowPass || !m.hasShadowPass)
    {
        if (outReason) *outReason = "shadow data is not enabled on both views";
        return false;
    }

    if (c.shadowCascadeCount != m.shadowCascadeCount)
    {
        if (outReason) *outReason = "cascade count differs from main view";
        return false;
    }

    if (c.shadowCasterMask != m.shadowCasterMask)
    {
        if (outReason) *outReason = "shadow caster mask differs from main view";
        return false;
    }

    if (!MatrixEquals(c.shadowViewProjMatrix, m.shadowViewProjMatrix))
    {
        if (outReason) *outReason = "shadow view-projection differs from main view";
        return false;
    }

    for (uint32_t i = 0u; i < c.shadowCascadeCount && i < MAX_SHADOW_CASCADES; ++i)
    {
        if (c.shadowCascadeSplits[i] != m.shadowCascadeSplits[i])
        {
            if (outReason) *outReason = "cascade splits differ from main view";
            return false;
        }

        if (!MatrixEquals(c.shadowCascadeViewProj[i], m.shadowCascadeViewProj[i]))
        {
            if (outReason) *outReason = "cascade matrices differ from main view";
            return false;
        }
    }

    if (outReason) *outReason = "main-view shadow can be shared";
    return true;
}

bool GDXRenderFrameGraph::HasDependency(const RFG::Node& node, uint32_t dep)
{
    for (uint32_t existing : node.dependencies)
        if (existing == dep) return true;
    return false;
}

void GDXRenderFrameGraph::AddDependency(RFG::Node& node, uint32_t dep)
{
    if (!HasDependency(node, dep))
        node.dependencies.push_back(dep);
}

// ---------------------------------------------------------------------------
// ExecuteFn-Factories — eliminieren doppelte Lambda-Körper in Build.
// Capture: nur exec-Pointer, kein Zustand der Graph-Klasse.
// ---------------------------------------------------------------------------

static std::function<void(const RFG::ExecContext&, RFG::ViewStats*)>
MakeShadowExecFn(const RFG::ExecuteData* exec)
{
    return [exec](const RFG::ExecContext& c, RFG::ViewStats* s)
    {
        if (c.backend && exec->shadowPass.enabled)
        {
            const std::vector<BackendPlannedTransition> emptyTransitions{};
            c.backend->ExecuteShadowPass(exec->shadowPass.desc,
                c.beginTransitions ? *c.beginTransitions : emptyTransitions,
                c.endTransitions ? *c.endTransitions : emptyTransitions,
                *c.registry, exec->shadowQueue, *c.meshStore, *c.matStore, *c.shaderStore, *c.texStore);
            s->shadowPassExecuted = true;
        }
    };
}

static std::function<void(const RFG::ExecContext&, RFG::ViewStats*)>
MakeGraphicsExecFn(const RFG::PassExec* passExec,
                   const ICommandList* opaqueList,
                   const ICommandList* alphaList)
{
    return [passExec, opaqueList, alphaList](const RFG::ExecContext& c, RFG::ViewStats* s)
    {
        if (c.backend && passExec && passExec->enabled)
        {
            BackendRenderPassDesc desc = passExec->desc;
            desc.opaqueList = opaqueList;
            desc.alphaList  = alphaList;

            static const RenderQueue kEmptyQueue{};
            const ICommandList& opaqueRef = opaqueList ? *opaqueList : static_cast<const ICommandList&>(kEmptyQueue);
            const ICommandList& alphaRef  = alphaList  ? *alphaList  : static_cast<const ICommandList&>(kEmptyQueue);

            const std::vector<BackendPlannedTransition> emptyTransitions{};
            c.backend->ExecuteRenderPass(desc,
                c.beginTransitions ? *c.beginTransitions : emptyTransitions,
                c.endTransitions ? *c.endTransitions : emptyTransitions,
                *c.registry, opaqueRef, alphaRef,
                *c.meshStore, *c.matStore, *c.shaderStore, *c.texStore, *c.rtStore);
            s->graphicsPassExecuted = true;
        }
    };
}

// ---------------------------------------------------------------------------
// ComputeGraphStructureKey — FNV-1a über die tatsächlich gebauten Nodes.
//
// Schlüssel wird nach dem Node-Build aus fg.nodes abgeleitet, nicht aus
// einer Vorhersage. Damit sind alle strukturellen Änderungen abgedeckt:
//   - Node-Anzahl
//   - Node-Kind
//   - Ressourcen-IDs (reads/writes)
//   - Anzahl der reads/writes pro Node
//   - RT-Bereitschaft (beeinflusst ob ein Node überhaupt gebaut wird)
//   - postProcess-Validity (sceneTexture.IsValid() bestimmt Node-Existenz)
//   - mainView.graphicsPass.enabled (fehlte im alten Predictive-Key)
//
// UINT64_MAX als Sentinel bleibt unverändert (fg mit 0 Nodes kann diesen
// Wert nicht erzeugen, da der erste mix(0) eine andere Ausgabe produziert).
// ---------------------------------------------------------------------------

uint64_t GDXRenderFrameGraph::ComputeGraphStructureKey(const RFG::FrameGraph& fg)
{
    // FNV-1a 64-bit
    uint64_t h = 14695981039346656037ull;
    auto mix = [&](uint64_t v)
    {
        h ^= v;
        h *= 1099511628211ull;
    };

    mix(static_cast<uint64_t>(fg.nodes.size()));
    for (const RFG::Node& node : fg.nodes)
    {
        mix(static_cast<uint64_t>(node.passType));
        mix(static_cast<uint64_t>(node.accesses.size()));
        for (const FGResourceAccessDecl& a : node.accesses)
        {
            mix(static_cast<uint64_t>(a.resource));
            mix(static_cast<uint64_t>(a.type));
            mix(static_cast<uint64_t>(a.mode));
            mix(static_cast<uint64_t>(a.requiredState));
        }
    }
    return h;
}

// ---------------------------------------------------------------------------
// Build — Ressourcen registrieren, Nodes aufbauen, Topology cachen.
// ---------------------------------------------------------------------------

void GDXRenderFrameGraph::Build(RFG::PipelineData& pipeline, const BuildContext& ctx)
{
    pipeline.frameGraph.Reset();

    auto clampExtent = [](float value) -> uint32_t
    {
        return value > 0.0f ? static_cast<uint32_t>(value) : 0u;
    };

    auto tryGetRenderTargetMeta = [&](RenderTargetHandle handle, uint32_t& outWidth, uint32_t& outHeight, GDXTextureFormat& outFormat) -> bool
    {
        outWidth = 0u;
        outHeight = 0u;
        outFormat = GDXTextureFormat::Unknown;

        if (!ctx.rtStore || !handle.IsValid())
            return false;

        const GDXRenderTargetResource* rt = ctx.rtStore->Get(handle);
        if (!rt)
            return false;

        outWidth = rt->width;
        outHeight = rt->height;
        outFormat = rt->colorFormat;
        return true;
    };

    const uint32_t backbufferWidth = clampExtent(pipeline.mainView.execute.frame.viewportWidth);
    const uint32_t backbufferHeight = clampExtent(pipeline.mainView.execute.frame.viewportHeight);

    auto resolveShadowExtent = [&](const RFG::ViewPassData& view, uint32_t& outWidth, uint32_t& outHeight)
    {
        outWidth = clampExtent(view.prepared.shadowView.frame.viewportWidth);
        outHeight = clampExtent(view.prepared.shadowView.frame.viewportHeight);
        if (outWidth == 0u || outHeight == 0u)
        {
            outWidth = clampExtent(view.prepared.frame.viewportWidth);
            outHeight = clampExtent(view.prepared.frame.viewportHeight);
        }
    };

    uint32_t mainShadowWidth = 0u;
    uint32_t mainShadowHeight = 0u;
    resolveShadowExtent(pipeline.mainView, mainShadowWidth, mainShadowHeight);

    uint32_t mainSceneWidth = 0u;
    uint32_t mainSceneHeight = 0u;
    GDXTextureFormat mainSceneFormat = GDXTextureFormat::Unknown;
    (void)tryGetRenderTargetMeta(ctx.mainScenePostProcessTarget, mainSceneWidth, mainSceneHeight, mainSceneFormat);

    const GDXRenderTargetResource* mainSceneRt =
        (ctx.rtStore && ctx.mainScenePostProcessTarget.IsValid())
            ? ctx.rtStore->Get(ctx.mainScenePostProcessTarget)
            : nullptr;

    // --- Schritt 1: Ressourcen registrieren ---
    const FGResourceID mainShadowMapID = pipeline.mainView.execute.shadowPass.enabled
        ? pipeline.frameGraph.RegisterTransientResource(
            TextureHandle::Invalid(), RenderTargetHandle::Invalid(), "ShadowMap.MainView", FGResourceKind::Shadow,
            mainShadowWidth, mainShadowHeight, GDXTextureFormat::D32_FLOAT)
        : FG_INVALID_RESOURCE;

    const FGResourceID backbufferID = pipeline.frameGraph.RegisterImportedResource(
        TextureHandle::Invalid(), RenderTargetHandle::Invalid(), "Backbuffer", FGResourceKind::Backbuffer,
        backbufferWidth, backbufferHeight, GDXTextureFormat::RGBA8_UNORM, true);

    const bool hasPostProcess =
        pipeline.mainView.execute.presentation.postProcess.enabled &&
        pipeline.mainView.execute.presentation.postProcess.sceneTexture.IsValid();

    const FGResourceID mainSceneID = hasPostProcess
        ? pipeline.frameGraph.RegisterGraphOwnedResource(
            pipeline.mainView.execute.presentation.postProcess.sceneTexture,
            ctx.mainScenePostProcessTarget, "MainSceneColor", FGResourceKind::RenderTarget,
            mainSceneWidth, mainSceneHeight, mainSceneFormat)
        : FG_INVALID_RESOURCE;

    const FGResourceID mainSceneDepthID =
        (hasPostProcess && mainSceneRt && mainSceneRt->ready && mainSceneRt->exposedDepthTexture.IsValid())
        ? pipeline.frameGraph.RegisterGraphOwnedResource(
            mainSceneRt->exposedDepthTexture,
            RenderTargetHandle::Invalid(), "MainSceneDepth", FGResourceKind::Depth,
            mainSceneWidth, mainSceneHeight, GDXTextureFormat::D24_UNORM_S8_UINT)
        : FG_INVALID_RESOURCE;

    const FGResourceID mainSceneNormalsID =
        (hasPostProcess && mainSceneRt && mainSceneRt->ready && mainSceneRt->exposedNormalsTexture.IsValid())
        ? pipeline.frameGraph.RegisterGraphOwnedResource(
            mainSceneRt->exposedNormalsTexture,
            RenderTargetHandle::Invalid(), "MainSceneNormals", FGResourceKind::RenderTarget,
            mainSceneWidth, mainSceneHeight, GDXTextureFormat::RGBA8_UNORM)
        : FG_INVALID_RESOURCE;

    const FGResourceID mainSceneMotionVectorsID =
        (hasPostProcess && mainSceneRt && mainSceneRt->ready && mainSceneRt->exposedMotionVectorsTexture.IsValid())
        ? pipeline.frameGraph.RegisterGraphOwnedResource(
            mainSceneRt->exposedMotionVectorsTexture,
            RenderTargetHandle::Invalid(), "MainSceneMotionVectors", FGResourceKind::RenderTarget,
            mainSceneWidth, mainSceneHeight, GDXTextureFormat::RG16_FLOAT)
        : FG_INVALID_RESOURCE;

    const uint32_t rttCount = static_cast<uint32_t>(pipeline.rttViews.size());
    std::vector<FGResourceID> rttColorIDs(rttCount, FG_INVALID_RESOURCE);
    std::vector<FGResourceID> rttSceneIDs(rttCount, FG_INVALID_RESOURCE);
    std::vector<FGResourceID> rttSceneDepthIDs(rttCount, FG_INVALID_RESOURCE);
    std::vector<FGResourceID> rttSceneNormalsIDs(rttCount, FG_INVALID_RESOURCE);
    std::vector<FGResourceID> rttSceneMotionVectorsIDs(rttCount, FG_INVALID_RESOURCE);
    std::vector<FGResourceID> rttShadowIDs(rttCount, FG_INVALID_RESOURCE);
    std::vector<bool>         rttReadsGlobalMainShadow(rttCount, false);
    for (uint32_t i = 0u; i < rttCount; ++i)
    {
        const RFG::ViewPassData& view = pipeline.rttViews[i];
        const RenderTargetHandle rttHandle = view.prepared.graphicsView.renderTarget;

        uint32_t rttWidth = 0u;
        uint32_t rttHeight = 0u;
        GDXTextureFormat rttFormat = GDXTextureFormat::Unknown;
        if (!tryGetRenderTargetMeta(rttHandle, rttWidth, rttHeight, rttFormat))
        {
            rttWidth = clampExtent(view.execute.frame.viewportWidth);
            rttHeight = clampExtent(view.execute.frame.viewportHeight);
        }

        const GDXRenderTargetResource* rt =
            ctx.rtStore ? ctx.rtStore->Get(rttHandle) : nullptr;
        if (!rt || !rt->ready) continue;

        rttColorIDs[i] = pipeline.frameGraph.RegisterGraphOwnedResource(
            TextureHandle::Invalid(), rttHandle, "RTT", FGResourceKind::RenderTarget,
            rttWidth, rttHeight, rttFormat, true);

        if (view.execute.shadowPass.enabled)
        {
            uint32_t rttShadowWidth = 0u;
            uint32_t rttShadowHeight = 0u;
            resolveShadowExtent(view, rttShadowWidth, rttShadowHeight);
            const std::string shadowName = std::string("ShadowMap.RTT[") + std::to_string(i) + "]";
            rttShadowIDs[i] = pipeline.frameGraph.RegisterTransientResource(
                TextureHandle::Invalid(), RenderTargetHandle::Invalid(), shadowName.c_str(), FGResourceKind::Shadow,
                rttShadowWidth, rttShadowHeight, GDXTextureFormat::D32_FLOAT);
        }
        else if (view.shadowResourcePolicy == FGShadowResourcePolicy::GlobalSharedMainView)
        {
            std::string reason;
            const bool canShare = CanShareMainViewShadow(view, pipeline.mainView, &reason);
            if (!canShare)
            {
                pipeline.frameGraph.validation.valid = false;
                pipeline.frameGraph.validation.errors.push_back(
                    std::string("RTT view ") + std::to_string(i) +
                    " requested shadow policy " + FGShadowResourcePolicyToString(view.shadowResourcePolicy) +
                    " but this is not allowed: " + reason);
            }
            else
            {
                rttReadsGlobalMainShadow[i] = true;
            }
        }

        if (view.execute.presentation.postProcess.enabled &&
            view.execute.presentation.postProcess.sceneTexture.IsValid())
        {
            rttSceneIDs[i] = pipeline.frameGraph.RegisterGraphOwnedResource(
                view.execute.presentation.postProcess.sceneTexture,
                view.execute.opaquePass.desc.target.renderTarget,
                "RTTSceneColor", FGResourceKind::RenderTarget,
                rttWidth, rttHeight, rttFormat);

            if (rt->exposedDepthTexture.IsValid())
            {
                rttSceneDepthIDs[i] = pipeline.frameGraph.RegisterGraphOwnedResource(
                    rt->exposedDepthTexture,
                    RenderTargetHandle::Invalid(), "RTTSceneDepth", FGResourceKind::Depth,
                    rttWidth, rttHeight, GDXTextureFormat::D24_UNORM_S8_UINT);
            }

            if (rt->exposedNormalsTexture.IsValid())
            {
                rttSceneNormalsIDs[i] = pipeline.frameGraph.RegisterGraphOwnedResource(
                    rt->exposedNormalsTexture,
                    RenderTargetHandle::Invalid(), "RTTSceneNormals", FGResourceKind::RenderTarget,
                    rttWidth, rttHeight, GDXTextureFormat::RGBA8_UNORM);
            }

            if (rt->exposedMotionVectorsTexture.IsValid())
            {
                rttSceneMotionVectorsIDs[i] = pipeline.frameGraph.RegisterGraphOwnedResource(
                    rt->exposedMotionVectorsTexture,
                    RenderTargetHandle::Invalid(), "RTTSceneMotionVectors", FGResourceKind::RenderTarget,
                    rttWidth, rttHeight, GDXTextureFormat::RG16_FLOAT);
            }
        }
    }

    // --- Schritt 2: Nodes bauen ---
    for (uint32_t i = 0u; i < rttCount; ++i)
    {
        RFG::ViewPassData& view = pipeline.rttViews[i];
        const GDXRenderTargetResource* rt =
            ctx.rtStore ? ctx.rtStore->Get(view.prepared.graphicsView.renderTarget) : nullptr;
        if (!rt || !rt->ready) continue;

        if (view.execute.shadowPass.enabled)
        {
            RFG::Node node{};
            node.passType   = RFG::PassType::Graphics;
            node.debugName  = "Shadow.RTT";
            node.executeInput = &view.execute;
            node.statsOutput  = &view.stats;
            node.viewIndex  = i;
            node.enabled    = true;
            node.countedAsRenderTargetView = true;
            node.shadowResourcePolicy = view.shadowResourcePolicy;
            node.AddDepthWrite(rttShadowIDs[i]);
            node.executeFn  = MakeShadowExecFn(&view.execute);
            pipeline.frameGraph.nodes.push_back(std::move(node));
        }

        if (view.execute.depthPass.enabled && !view.execute.depthQueue.Empty() &&
            rttSceneDepthIDs[i] != FG_INVALID_RESOURCE)
        {
            RFG::Node node{};
            node.passType   = RFG::PassType::Graphics;
            node.debugName  = "DepthPrepass.RTT";
            node.executeInput = &view.execute;
            node.statsOutput  = &view.stats;
            node.viewIndex  = i;
            node.enabled    = true;
            node.countedAsRenderTargetView = true;
            node.shadowResourcePolicy = view.shadowResourcePolicy;
            node.AddDepthWrite(rttSceneDepthIDs[i]);
            node.executeFn  = MakeGraphicsExecFn(&view.execute.depthPass, &view.execute.depthQueue, nullptr);
            pipeline.frameGraph.nodes.push_back(std::move(node));
        }

        if (view.execute.opaquePass.enabled)
        {
            RFG::Node node{};
            node.passType   = RFG::PassType::Graphics;
            node.debugName  = "Opaque.RTT";
            node.executeInput = &view.execute;
            node.statsOutput  = &view.stats;
            node.viewIndex  = i;
            node.enabled    = true;
            node.countedAsRenderTargetView = true;
            node.shadowResourcePolicy = view.shadowResourcePolicy;
            if (view.execute.shadowPass.enabled && rttShadowIDs[i] != FG_INVALID_RESOURCE)
                node.AddSRV(rttShadowIDs[i]);
            else if (rttReadsGlobalMainShadow[i] && mainShadowMapID != FG_INVALID_RESOURCE)
                node.AddSRV(mainShadowMapID);
            if (view.execute.presentation.postProcess.enabled && rttSceneIDs[i] != FG_INVALID_RESOURCE)
            {
                node.AddRenderTarget(rttSceneIDs[i]);
                if (rttSceneDepthIDs[i] != FG_INVALID_RESOURCE)
                {
                    if (view.execute.depthPass.enabled && !view.execute.depthQueue.Empty())
                        node.AddDepthRead(rttSceneDepthIDs[i]);
                    else
                        node.AddDepthWrite(rttSceneDepthIDs[i]);
                }
                if (rttSceneNormalsIDs[i] != FG_INVALID_RESOURCE)
                    node.AddRenderTarget(rttSceneNormalsIDs[i]);
            }
            else if (rttColorIDs[i] != FG_INVALID_RESOURCE)
                node.AddRenderTarget(rttColorIDs[i]);
            node.executeFn  = MakeGraphicsExecFn(&view.execute.opaquePass, &view.execute.opaqueQueue, nullptr);
            pipeline.frameGraph.nodes.push_back(std::move(node));
        }


        if (view.execute.motionVectorsPass.enabled && !view.execute.motionVectorsQueue.Empty() &&
            rttSceneMotionVectorsIDs[i] != FG_INVALID_RESOURCE && rttSceneDepthIDs[i] != FG_INVALID_RESOURCE)
        {
            RFG::Node node{};
            node.passType   = RFG::PassType::Graphics;
            node.debugName  = "MotionVectors.RTT";
            node.executeInput = &view.execute;
            node.statsOutput  = &view.stats;
            node.viewIndex  = i;
            node.enabled    = true;
            node.countedAsRenderTargetView = true;
            node.shadowResourcePolicy = view.shadowResourcePolicy;
            node.AddRenderTarget(rttSceneMotionVectorsIDs[i]);
            node.AddDepthRead(rttSceneDepthIDs[i]);
            node.executeFn  = MakeGraphicsExecFn(&view.execute.motionVectorsPass, &view.execute.motionVectorsQueue, nullptr);
            pipeline.frameGraph.nodes.push_back(std::move(node));
        }

        if (view.execute.particlePass.enabled && !view.execute.particleQueue.Empty())
        {
            RFG::Node node{};
            node.passType   = RFG::PassType::Graphics;
            node.debugName  = "Particles.RTT";
            node.executeInput = &view.execute;
            node.statsOutput  = &view.stats;
            node.viewIndex  = i;
            node.enabled    = true;
            node.countedAsRenderTargetView = true;
            node.shadowResourcePolicy = view.shadowResourcePolicy;
            if (view.execute.shadowPass.enabled && rttShadowIDs[i] != FG_INVALID_RESOURCE)
                node.AddSRV(rttShadowIDs[i]);
            else if (rttReadsGlobalMainShadow[i] && mainShadowMapID != FG_INVALID_RESOURCE)
                node.AddSRV(mainShadowMapID);
            if (view.execute.presentation.postProcess.enabled && rttSceneIDs[i] != FG_INVALID_RESOURCE)
            {
                node.AddRenderTarget(rttSceneIDs[i]);
                if (rttSceneDepthIDs[i] != FG_INVALID_RESOURCE)
                    node.AddDepthRead(rttSceneDepthIDs[i]);
            }
            else if (rttColorIDs[i] != FG_INVALID_RESOURCE)
                node.AddRenderTarget(rttColorIDs[i]);
            node.executeFn  = MakeGraphicsExecFn(&view.execute.particlePass, nullptr, &view.execute.particleQueue);
            pipeline.frameGraph.nodes.push_back(std::move(node));
        }

        if (view.execute.transparentPass.enabled && !view.execute.alphaQueue.Empty())
        {
            RFG::Node node{};
            node.passType   = RFG::PassType::Graphics;
            node.debugName  = "Transparent.RTT";
            node.executeInput = &view.execute;
            node.statsOutput  = &view.stats;
            node.viewIndex  = i;
            node.enabled    = true;
            node.countedAsRenderTargetView = true;
            node.shadowResourcePolicy = view.shadowResourcePolicy;
            if (view.execute.shadowPass.enabled && rttShadowIDs[i] != FG_INVALID_RESOURCE)
                node.AddSRV(rttShadowIDs[i]);
            else if (rttReadsGlobalMainShadow[i] && mainShadowMapID != FG_INVALID_RESOURCE)
                node.AddSRV(mainShadowMapID);
            if (view.execute.presentation.postProcess.enabled && rttSceneIDs[i] != FG_INVALID_RESOURCE)
            {
                node.AddRenderTarget(rttSceneIDs[i]);
                if (rttSceneDepthIDs[i] != FG_INVALID_RESOURCE)
                    node.AddDepthRead(rttSceneDepthIDs[i]);
            }
            else if (rttColorIDs[i] != FG_INVALID_RESOURCE)
                node.AddRenderTarget(rttColorIDs[i]);
            node.executeFn  = MakeGraphicsExecFn(&view.execute.transparentPass, nullptr, &view.execute.alphaQueue);
            pipeline.frameGraph.nodes.push_back(std::move(node));
        }

        if (view.execute.distortionPass.enabled && !view.execute.distortionQueue.Empty())
        {
            RFG::Node node{};
            node.passType   = RFG::PassType::Graphics;
            node.debugName  = "Distortion.RTT";
            node.executeInput = &view.execute;
            node.statsOutput  = &view.stats;
            node.viewIndex  = i;
            node.enabled    = true;
            node.countedAsRenderTargetView = true;
            node.shadowResourcePolicy = view.shadowResourcePolicy;
            if (view.execute.shadowPass.enabled && rttShadowIDs[i] != FG_INVALID_RESOURCE)
                node.AddSRV(rttShadowIDs[i]);
            else if (rttReadsGlobalMainShadow[i] && mainShadowMapID != FG_INVALID_RESOURCE)
                node.AddSRV(mainShadowMapID);
            if (view.execute.presentation.postProcess.enabled && rttSceneIDs[i] != FG_INVALID_RESOURCE)
            {
                node.AddRenderTarget(rttSceneIDs[i]);
                if (rttSceneDepthIDs[i] != FG_INVALID_RESOURCE)
                    node.AddDepthRead(rttSceneDepthIDs[i]);
            }
            else if (rttColorIDs[i] != FG_INVALID_RESOURCE)
                node.AddRenderTarget(rttColorIDs[i]);
            node.executeFn  = MakeGraphicsExecFn(&view.execute.distortionPass, nullptr, &view.execute.distortionQueue);
            pipeline.frameGraph.nodes.push_back(std::move(node));
        }

        if (view.execute.presentation.postProcess.enabled &&
            view.execute.presentation.postProcess.sceneTexture.IsValid() &&
            rttSceneIDs[i] != FG_INVALID_RESOURCE &&
            rttColorIDs[i] != FG_INVALID_RESOURCE)
        {
            RFG::Node node{};
            node.passType  = RFG::PassType::Graphics;
            node.debugName = "PostProcess.RTT";
            node.executeInput = &view.execute;
            node.statsOutput = &view.stats;
            node.viewIndex = i;
            node.enabled = true;
            node.countedAsRenderTargetView = true;
            node.shadowResourcePolicy = view.shadowResourcePolicy;
            node.updateFrameConstants = false;
            node.AddSRV(rttSceneIDs[i]);
            if (rttSceneDepthIDs[i] != FG_INVALID_RESOURCE)
                node.AddSRV(rttSceneDepthIDs[i]);
            if (rttSceneNormalsIDs[i] != FG_INVALID_RESOURCE)
                node.AddSRV(rttSceneNormalsIDs[i]);
            node.AddRenderTarget(rttColorIDs[i]);
            const RFG::ExecuteData* exec = &view.execute;
            node.executeFn = [exec](const RFG::ExecContext& c, RFG::ViewStats* s)
            {
                if (c.backend &&
                    exec->presentation.postProcess.enabled &&
                    exec->presentation.postProcess.sceneTexture.IsValid())
                {
                    assert(c.postProcessStore != nullptr);
                    const bool ok = c.backend->ExecutePostProcessChain(
                        exec->presentation.postProcess.orderedPasses,
                        &exec->presentation.postProcess.constantOverrides,
                        *c.postProcessStore, *c.texStore, c.rtStore,
                        exec->presentation.postProcess.execInputs,
                        exec->frame.viewportWidth,
                        exec->frame.viewportHeight,
                        exec->presentation.postProcess.outputTarget,
                        exec->presentation.postProcess.outputToBackbuffer);
                    s->presentationExecuted = ok;
                }
            };
            pipeline.frameGraph.nodes.push_back(std::move(node));
        }
    }

    if (pipeline.mainView.execute.shadowPass.enabled)
    {
        RFG::Node node{};
        node.passType   = RFG::PassType::Graphics;
        node.debugName  = "Shadow.Main";
        node.executeInput = &pipeline.mainView.execute;
        node.statsOutput  = &pipeline.mainView.stats;
        node.enabled    = true;
        node.shadowResourcePolicy = FGShadowResourcePolicy::LocalPerView;
        node.AddDepthWrite(mainShadowMapID);
        node.executeFn  = MakeShadowExecFn(&pipeline.mainView.execute);
        pipeline.frameGraph.nodes.push_back(std::move(node));
    }

    if (pipeline.mainView.execute.depthPass.enabled && !pipeline.mainView.execute.depthQueue.Empty() &&
        hasPostProcess && mainSceneDepthID != FG_INVALID_RESOURCE)
    {
        RFG::Node node{};
        node.passType   = RFG::PassType::Graphics;
        node.debugName  = "DepthPrepass.Main";
        node.executeInput = &pipeline.mainView.execute;
        node.statsOutput  = &pipeline.mainView.stats;
        node.enabled    = true;
        node.shadowResourcePolicy = FGShadowResourcePolicy::LocalPerView;
        node.AddDepthWrite(mainSceneDepthID);
        node.executeFn  = MakeGraphicsExecFn(&pipeline.mainView.execute.depthPass, &pipeline.mainView.execute.depthQueue, nullptr);
        pipeline.frameGraph.nodes.push_back(std::move(node));
    }

    if (pipeline.mainView.execute.opaquePass.enabled)
    {
        RFG::Node node{};
        node.passType   = RFG::PassType::Graphics;
        node.debugName  = "Opaque.Main";
        node.executeInput = &pipeline.mainView.execute;
        node.statsOutput  = &pipeline.mainView.stats;
        node.enabled    = true;
        node.shadowResourcePolicy = FGShadowResourcePolicy::LocalPerView;
        if (pipeline.mainView.execute.shadowPass.enabled && mainShadowMapID != FG_INVALID_RESOURCE)
            node.AddSRV(mainShadowMapID);
        for (uint32_t i = 0u; i < rttCount; ++i)
            if (rttColorIDs[i] != FG_INVALID_RESOURCE)
                node.AddSRV(rttColorIDs[i]);
        node.AddRenderTarget(hasPostProcess ? mainSceneID : backbufferID);
        if (hasPostProcess)
        {
            if (mainSceneDepthID != FG_INVALID_RESOURCE)
            {
                if (pipeline.mainView.execute.depthPass.enabled && !pipeline.mainView.execute.depthQueue.Empty())
                    node.AddDepthRead(mainSceneDepthID);
                else
                    node.AddDepthWrite(mainSceneDepthID);
            }
            if (mainSceneNormalsID != FG_INVALID_RESOURCE)
                node.AddRenderTarget(mainSceneNormalsID);
        }
        node.executeFn  = MakeGraphicsExecFn(&pipeline.mainView.execute.opaquePass, &pipeline.mainView.execute.opaqueQueue, nullptr);
        pipeline.frameGraph.nodes.push_back(std::move(node));
    }


    if (pipeline.mainView.execute.motionVectorsPass.enabled && !pipeline.mainView.execute.motionVectorsQueue.Empty() &&
        hasPostProcess && mainSceneMotionVectorsID != FG_INVALID_RESOURCE && mainSceneDepthID != FG_INVALID_RESOURCE)
    {
        RFG::Node node{};
        node.passType   = RFG::PassType::Graphics;
        node.debugName  = "MotionVectors.Main";
        node.executeInput = &pipeline.mainView.execute;
        node.statsOutput  = &pipeline.mainView.stats;
        node.enabled    = true;
        node.shadowResourcePolicy = FGShadowResourcePolicy::LocalPerView;
        node.AddRenderTarget(mainSceneMotionVectorsID);
        node.AddDepthRead(mainSceneDepthID);
        node.executeFn  = MakeGraphicsExecFn(&pipeline.mainView.execute.motionVectorsPass, &pipeline.mainView.execute.motionVectorsQueue, nullptr);
        pipeline.frameGraph.nodes.push_back(std::move(node));
    }

    if (pipeline.mainView.execute.particlePass.enabled && !pipeline.mainView.execute.particleQueue.Empty())
    {
        RFG::Node node{};
        node.passType   = RFG::PassType::Graphics;
        node.debugName  = "Particles.Main";
        node.executeInput = &pipeline.mainView.execute;
        node.statsOutput  = &pipeline.mainView.stats;
        node.enabled    = true;
        node.shadowResourcePolicy = FGShadowResourcePolicy::LocalPerView;
        if (pipeline.mainView.execute.shadowPass.enabled && mainShadowMapID != FG_INVALID_RESOURCE)
            node.AddSRV(mainShadowMapID);
        node.AddRenderTarget(hasPostProcess ? mainSceneID : backbufferID);
        if (hasPostProcess && mainSceneDepthID != FG_INVALID_RESOURCE)
            node.AddDepthRead(mainSceneDepthID);
        node.executeFn = MakeGraphicsExecFn(&pipeline.mainView.execute.particlePass, nullptr, &pipeline.mainView.execute.particleQueue);
        pipeline.frameGraph.nodes.push_back(std::move(node));
    }

    if (pipeline.mainView.execute.transparentPass.enabled && !pipeline.mainView.execute.alphaQueue.Empty())
    {
        RFG::Node node{};
        node.passType   = RFG::PassType::Graphics;
        node.debugName  = "Transparent.Main";
        node.executeInput = &pipeline.mainView.execute;
        node.statsOutput  = &pipeline.mainView.stats;
        node.enabled    = true;
        node.shadowResourcePolicy = FGShadowResourcePolicy::LocalPerView;
        if (pipeline.mainView.execute.shadowPass.enabled && mainShadowMapID != FG_INVALID_RESOURCE)
            node.AddSRV(mainShadowMapID);
        for (uint32_t i = 0u; i < rttCount; ++i)
            if (rttColorIDs[i] != FG_INVALID_RESOURCE)
                node.AddSRV(rttColorIDs[i]);
        node.AddRenderTarget(hasPostProcess ? mainSceneID : backbufferID);
        if (hasPostProcess && mainSceneDepthID != FG_INVALID_RESOURCE)
            node.AddDepthRead(mainSceneDepthID);
        node.executeFn  = MakeGraphicsExecFn(&pipeline.mainView.execute.transparentPass, nullptr, &pipeline.mainView.execute.alphaQueue);
        pipeline.frameGraph.nodes.push_back(std::move(node));
    }

    if (pipeline.mainView.execute.distortionPass.enabled && !pipeline.mainView.execute.distortionQueue.Empty())
    {
        RFG::Node node{};
        node.passType   = RFG::PassType::Graphics;
        node.debugName  = "Distortion.Main";
        node.executeInput = &pipeline.mainView.execute;
        node.statsOutput  = &pipeline.mainView.stats;
        node.enabled    = true;
        node.shadowResourcePolicy = FGShadowResourcePolicy::LocalPerView;
        if (pipeline.mainView.execute.shadowPass.enabled && mainShadowMapID != FG_INVALID_RESOURCE)
            node.AddSRV(mainShadowMapID);
        for (uint32_t i = 0u; i < rttCount; ++i)
            if (rttColorIDs[i] != FG_INVALID_RESOURCE)
                node.AddSRV(rttColorIDs[i]);
        node.AddRenderTarget(hasPostProcess ? mainSceneID : backbufferID);
        if (hasPostProcess && mainSceneDepthID != FG_INVALID_RESOURCE)
            node.AddDepthRead(mainSceneDepthID);
        node.executeFn  = MakeGraphicsExecFn(&pipeline.mainView.execute.distortionPass, nullptr, &pipeline.mainView.execute.distortionQueue);
        pipeline.frameGraph.nodes.push_back(std::move(node));
    }

    if (hasPostProcess)
    {
        RFG::Node node{};
        node.passType  = RFG::PassType::Graphics;
        node.debugName = "PostProcess.Main";
        node.executeInput = &pipeline.mainView.execute;
        node.statsOutput  = &pipeline.mainView.stats;
        node.enabled    = true;
        node.shadowResourcePolicy = FGShadowResourcePolicy::LocalPerView;
        node.updateFrameConstants = false;
        node.AddSRV(mainSceneID);
        if (mainSceneDepthID != FG_INVALID_RESOURCE)
            node.AddSRV(mainSceneDepthID);
        if (mainSceneNormalsID != FG_INVALID_RESOURCE)
            node.AddSRV(mainSceneNormalsID);
        node.AddRenderTarget(backbufferID);
        const RFG::ExecuteData* exec = &pipeline.mainView.execute;
        node.executeFn = [exec](const RFG::ExecContext& c, RFG::ViewStats* s)
        {
            if (c.backend &&
                exec->presentation.postProcess.enabled &&
                exec->presentation.postProcess.sceneTexture.IsValid())
            {
                // postProcessStore/PassOrder sind immer gesetzt wenn backend gesetzt ist —
                // null hier ist ein Programmierfehler im ExecContext-Build, kein Laufzeitfall.
                assert(c.postProcessStore != nullptr);
                const bool ok = c.backend->ExecutePostProcessChain(
                    exec->presentation.postProcess.orderedPasses,
                    &exec->presentation.postProcess.constantOverrides,
                    *c.postProcessStore, *c.texStore, c.rtStore,
                    exec->presentation.postProcess.execInputs,
                    exec->frame.viewportWidth,
                    exec->frame.viewportHeight,
                    exec->presentation.postProcess.outputTarget,
                    exec->presentation.postProcess.outputToBackbuffer);
                if (!ok)
                {
                    Debug::LogWarning(GDX_SRC_LOC,
                        L"MainView post-process chain did not execute successfully.");
                }
                s->presentationExecuted = ok;
            }
        };
        pipeline.frameGraph.nodes.push_back(std::move(node));
    }

    // Explicit present pass (no rendering): consumes the swapchain/backbuffer in PRESENT state.
    // This removes the implicit "present outside of the graph" special path.
    if (pipeline.mainView.execute.presentation.presentAfterExecute && backbufferID != FG_INVALID_RESOURCE)
    {
        RFG::Node node{};
        node.passType = RFG::PassType::Present;
        node.debugName = "Present";
        node.executeInput = &pipeline.mainView.execute;
        node.statsOutput  = &pipeline.mainView.stats;
        node.enabled = true;
        node.updateFrameConstants = false;
        node.AddPresentSource(backbufferID);
        node.executeFn = [](const RFG::ExecContext& c, RFG::ViewStats* s)
        {
            if (c.backend)
            {
                c.backend->Present(true);
                s->presentationExecuted = true;
            }
        };
        pipeline.frameGraph.nodes.push_back(std::move(node));
    }

    // --- Schritt 3: Topology-Cache ---
    // Key wird aus den tatsächlich gebauten Nodes abgeleitet (nach dem Build).
    // Damit sind alle strukturellen Unterschiede erfasst: Node-Anzahl, NodeKind,
    // reads/writes, RT-Bereitschaft, sceneTexture-Validity, graphicsPass.enabled.
    // Ein false-Cache-Hit ist damit ausgeschlossen.
    std::vector<uint8_t> liveNodes;
    std::vector<uint8_t> liveResources;
    BuildDependencies(pipeline.frameGraph);
    ComputeReachabilityFromSinks(pipeline.frameGraph, liveNodes, liveResources);
    CompactToLiveSubgraph(pipeline.frameGraph, liveNodes, liveResources);
    BuildDependencies(pipeline.frameGraph);

    const uint64_t newKey = ComputeGraphStructureKey(pipeline.frameGraph);
    if (newKey == m_cachedTopologyKey && !m_cachedExecutionOrder.empty())
    {
        // Topology-Struktur identisch — Execution-Order wiederverwenden.
        //
        // WICHTIG: Der Key muss auf dem bereits auf Live-Nodes reduzierten
        // Graphen basieren. Der gecachte Execution-Order stammt ebenfalls aus
        // genau diesem kompaktierten Graph. Ohne Reachability+Compaction im
        // Cache-Hit-Pfad kann executionOrder.size() < fg.nodes.size() werden,
        // obwohl gar kein echter Zyklus existiert.
        pipeline.frameGraph.executionOrder = m_cachedExecutionOrder;

        // Lifetime-Metadaten hängen an der finalen Execution-Order und müssen
        // auch im Cache-Hit-Pfad neu berechnet werden.
        ComputeResourceLifetimes(pipeline.frameGraph);
        PlanResourceStates(pipeline.frameGraph);

        // Validation explizit prüfen statt blind auf true setzen.
        // Validate() ist günstig wenn kein Zyklus-Check nötig ist
        // (executionOrder ist bereits bekannt-gut vom letzten Finalize).
        Validate(pipeline.frameGraph);
    }
    else
    {
        BuildExecutionOrder(pipeline.frameGraph);
        ComputeResourceLifetimes(pipeline.frameGraph);
        PlanResourceStates(pipeline.frameGraph);
        Validate(pipeline.frameGraph);
        if (pipeline.frameGraph.validation.valid)
        {
            m_cachedTopologyKey    = newKey;
            m_cachedExecutionOrder = pipeline.frameGraph.executionOrder;
        }
        else
        {
            m_cachedTopologyKey = UINT64_MAX;
            m_cachedExecutionOrder.clear();
        }
    }
}

// ---------------------------------------------------------------------------
// Finalize — Dependencies + Order + Validation.
// ---------------------------------------------------------------------------

void GDXRenderFrameGraph::Finalize(RFG::FrameGraph& fg)
{
    BuildDependencies(fg);

    std::vector<uint8_t> liveNodes;
    std::vector<uint8_t> liveResources;
    ComputeReachabilityFromSinks(fg, liveNodes, liveResources);
    CompactToLiveSubgraph(fg, liveNodes, liveResources);

    BuildDependencies(fg);
    BuildExecutionOrder(fg);
    ComputeResourceLifetimes(fg);
    PlanResourceStates(fg);
    Validate(fg);
    // Ergebnis liegt in fg.validation.valid — Callsite liest dort.
}

void GDXRenderFrameGraph::BuildDependencies(RFG::FrameGraph& fg)
{
    // Vollständig resource-getrieben:
    //   RAW: Reader wartet auf letzten Writer.
    //   WAW+WAR: Writer wartet auf letzten Accessor (Read oder Write).
    for (auto& node : fg.nodes)
        node.dependencies.clear();

    const uint32_t nodeCount = static_cast<uint32_t>(fg.nodes.size());

    auto findLastWriter = [&](uint32_t before, FGResourceID rid) -> int
    {
        for (int j = static_cast<int>(before) - 1; j >= 0; --j)
        {
            const auto& prev = fg.nodes[j];
            for (const FGResourceAccessDecl& a : prev.accesses)
                if (a.resource == rid && a.IsWrite()) return j;
        }
        return -1;
    };

    auto findLastAccessor = [&](uint32_t before, FGResourceID rid) -> int
    {
        for (int j = static_cast<int>(before) - 1; j >= 0; --j)
        {
            const auto& prev = fg.nodes[j];
            for (const FGResourceAccessDecl& a : prev.accesses)
                if (a.resource == rid && (a.IsRead() || a.IsWrite())) return j;
        }
        return -1;
    };

    for (uint32_t i = 0u; i < nodeCount; ++i)
    {
        RFG::Node& node = fg.nodes[i];
        for (const FGResourceAccessDecl& a : node.accesses)
        {
            const FGResourceID rid = a.resource;
            if (rid == FG_INVALID_RESOURCE)
                continue;

            if (a.IsRead())
            {
                const int j = findLastWriter(i, rid);
                if (j >= 0) AddDependency(node, static_cast<uint32_t>(j));
            }
            if (a.IsWrite())
            {
                const int j = findLastAccessor(i, rid);
                if (j >= 0) AddDependency(node, static_cast<uint32_t>(j));
            }
        }
    }
}

void GDXRenderFrameGraph::ComputeResourceLifetimes(RFG::FrameGraph& fg) const
{
    for (FGResourceDesc& resource : fg.resources)
    {
        resource.producerNode = FG_INVALID_NODE;
        resource.firstUseNode = FG_INVALID_NODE;
        resource.lastUseNode  = FG_INVALID_NODE;
    }

    for (uint32_t orderIndex = 0u; orderIndex < static_cast<uint32_t>(fg.executionOrder.size()); ++orderIndex)
    {
        const uint32_t nodeIndex = fg.executionOrder[orderIndex];
        if (nodeIndex >= fg.nodes.size())
            continue;

        const RFG::Node& node = fg.nodes[nodeIndex];

        auto markUse = [&](FGResourceID rid, bool isWrite)
        {
            if (rid == FG_INVALID_RESOURCE || rid >= fg.resources.size())
                return;

            FGResourceDesc& resource = fg.resources[rid];
            if (resource.firstUseNode == FG_INVALID_NODE)
                resource.firstUseNode = nodeIndex;
            resource.lastUseNode = nodeIndex;

            if (isWrite && resource.producerNode == FG_INVALID_NODE)
                resource.producerNode = nodeIndex;
        };

        for (const FGResourceAccessDecl& a : node.accesses)
        {
            if (a.IsRead())
                markUse(a.resource, false);
            if (a.IsWrite())
                markUse(a.resource, true);
        }
    }
}


void GDXRenderFrameGraph::PlanResourceStates(RFG::FrameGraph& fg) const
{
    auto deriveInitialFromFirstUse = [&](const FGResourceDesc& res, FGResourceStateSource& outSource) -> ResourceState
    {
        outSource = FGResourceStateSource::Unknown;

        if (res.kind == FGResourceKind::Backbuffer)
        {
            outSource = FGResourceStateSource::BackbufferPresent;
            return ResourceState::Present;
        }

        if (res.IsTransient())
        {
            outSource = FGResourceStateSource::TransientCommon;
            return ResourceState::Common;
        }

        if (res.firstUseNode != FG_INVALID_NODE && res.firstUseNode < fg.nodes.size())
        {
            const RFG::Node& n = fg.nodes[res.firstUseNode];
            for (const FGResourceAccessDecl& a : n.accesses)
            {
                if (a.resource == res.id && a.requiredState != ResourceState::Unknown)
                {
                    outSource = FGResourceStateSource::ImportedFirstUse;
                    return a.requiredState;
                }
            }
        }

        outSource = FGResourceStateSource::InferredFallback;
        return InferInitialStateForResource(res);
    };

    for (FGResourceDesc& resource : fg.resources)
    {
        resource.plannedInitialState = deriveInitialFromFirstUse(resource, resource.plannedInitialStateSource);
        resource.plannedFinalState = resource.plannedInitialState;
    }

    std::vector<ResourceState> currentStates(fg.resources.size(), ResourceState::Unknown);
    for (uint32_t rid = 0u; rid < static_cast<uint32_t>(fg.resources.size()); ++rid)
        currentStates[rid] = fg.resources[rid].plannedInitialState;

    for (RFG::Node& node : fg.nodes)
    {
        node.beginTransitions.clear();
        node.endTransitions.clear();
    }

    for (uint32_t orderIndex = 0u; orderIndex < static_cast<uint32_t>(fg.executionOrder.size()); ++orderIndex)
    {
        const uint32_t nodeIndex = fg.executionOrder[orderIndex];
        if (nodeIndex >= fg.nodes.size())
            continue;

        RFG::Node& node = fg.nodes[nodeIndex];
        for (const FGResourceAccessDecl& access : node.accesses)
        {
            if (access.resource == FG_INVALID_RESOURCE || access.resource >= fg.resources.size())
                continue;
            if (access.requiredState == ResourceState::Unknown)
                continue;

            ResourceState& current = currentStates[access.resource];
            if (current == ResourceState::Unknown)
                current = fg.resources[access.resource].plannedInitialState;

            if (current != access.requiredState)
                node.beginTransitions.push_back({ access.resource, current, access.requiredState });

            current = access.requiredState;
            fg.resources[access.resource].plannedFinalState = current;
        }
    }

    for (uint32_t rid = 0u; rid < static_cast<uint32_t>(fg.resources.size()); ++rid)
    {
        FGResourceDesc& resource = fg.resources[rid];
        const ResourceState stableFinal = InferStableFinalStateForResource(resource, currentStates[rid]);
        resource.plannedFinalState = stableFinal;
        if (stableFinal == currentStates[rid])
            continue;

        for (int32_t orderIndex = static_cast<int32_t>(fg.executionOrder.size()) - 1; orderIndex >= 0; --orderIndex)
        {
            const uint32_t nodeIndex = fg.executionOrder[static_cast<uint32_t>(orderIndex)];
            if (nodeIndex >= fg.nodes.size())
                continue;

            RFG::Node& node = fg.nodes[nodeIndex];
            bool touchesResource = false;
            for (const FGResourceAccessDecl& access : node.accesses)
            {
                if (access.resource == rid)
                {
                    touchesResource = true;
                    break;
                }
            }

            if (touchesResource)
            {
                node.endTransitions.push_back({ rid, currentStates[rid], stableFinal });
                currentStates[rid] = stableFinal;
                break;
            }
        }
    }
}

void GDXRenderFrameGraph::ComputeReachabilityFromSinks(
    RFG::FrameGraph& fg,
    std::vector<uint8_t>& liveNodes,
    std::vector<uint8_t>& liveResources) const
{
    const uint32_t nodeCount = static_cast<uint32_t>(fg.nodes.size());
    const uint32_t resourceCount = static_cast<uint32_t>(fg.resources.size());

    liveNodes.assign(nodeCount, 0u);
    liveResources.assign(resourceCount, 0u);

    auto markResource = [&](FGResourceID rid) -> bool
    {
        if (rid == FG_INVALID_RESOURCE || rid >= resourceCount)
            return false;
        if (liveResources[rid] != 0u)
            return false;
        liveResources[rid] = 1u;
        return true;
    };

    // Seed: explicit external outputs (swapchain/backbuffer, RTT targets, etc.)
    for (uint32_t rid = 0u; rid < resourceCount; ++rid)
    {
        const FGResourceDesc& res = fg.resources[rid];
        if (res.externalOutput)
            liveResources[rid] = 1u;
    }

    // Seed: present passes are sinks even if they don't "write" anything.
    for (uint32_t nodeIndex = 0u; nodeIndex < nodeCount; ++nodeIndex)
    {
        const RFG::Node& node = fg.nodes[nodeIndex];
        if (node.enabled && node.passType == RFG::PassType::Present)
        {
            liveNodes[nodeIndex] = 1u;
            for (const FGResourceAccessDecl& a : node.accesses)
                (void)markResource(a.resource);
        }
    }

    bool changed = true;
    while (changed)
    {
        changed = false;

        for (uint32_t nodeIndex = 0u; nodeIndex < nodeCount; ++nodeIndex)
        {
            const RFG::Node& node = fg.nodes[nodeIndex];

            bool writesLive = false;
            for (const FGResourceAccessDecl& a : node.accesses)
            {
                if (!a.IsWrite())
                    continue;
                if (a.resource < resourceCount && liveResources[a.resource] != 0u)
                {
                    writesLive = true;
                    break;
                }
            }

            const bool isAlreadyLive = (liveNodes[nodeIndex] != 0u);
            if (!writesLive && !isAlreadyLive)
                continue;

            if (!isAlreadyLive)
            {
                liveNodes[nodeIndex] = 1u;
                changed = true;
            }

            for (const FGResourceAccessDecl& a : node.accesses)
                changed = markResource(a.resource) || changed;

            for (uint32_t dep : node.dependencies)
            {
                if (dep < nodeCount && liveNodes[dep] == 0u)
                {
                    liveNodes[dep] = 1u;
                    changed = true;
                }
            }
        }

        // Ensure resources referenced by live nodes stay live.
        for (uint32_t nodeIndex = 0u; nodeIndex < nodeCount; ++nodeIndex)
        {
            if (liveNodes[nodeIndex] == 0u)
                continue;
            const RFG::Node& node = fg.nodes[nodeIndex];
            for (const FGResourceAccessDecl& a : node.accesses)
                changed = markResource(a.resource) || changed;
        }
    }
}

void GDXRenderFrameGraph::CompactToLiveSubgraph(
    RFG::FrameGraph& fg,
    const std::vector<uint8_t>& liveNodes,
    const std::vector<uint8_t>& liveResources) const
{
    const uint32_t oldNodeCount = static_cast<uint32_t>(fg.nodes.size());
    const uint32_t oldResourceCount = static_cast<uint32_t>(fg.resources.size());

    std::vector<uint32_t> resourceRemap(oldResourceCount, FG_INVALID_RESOURCE);
    std::vector<FGResourceDesc> newResources;
    newResources.reserve(oldResourceCount);
    for (uint32_t rid = 0u; rid < oldResourceCount; ++rid)
    {
        if (rid >= liveResources.size() || liveResources[rid] == 0u)
            continue;
        resourceRemap[rid] = static_cast<uint32_t>(newResources.size());
        FGResourceDesc desc = fg.resources[rid];
        desc.id = static_cast<uint32_t>(newResources.size());
        newResources.push_back(std::move(desc));
    }

    std::vector<uint32_t> nodeRemap(oldNodeCount, FG_INVALID_NODE);
    std::vector<RFG::Node> newNodes;
    newNodes.reserve(oldNodeCount);
    for (uint32_t nid = 0u; nid < oldNodeCount; ++nid)
    {
        if (nid >= liveNodes.size() || liveNodes[nid] == 0u)
            continue;
        nodeRemap[nid] = static_cast<uint32_t>(newNodes.size());
        RFG::Node node = fg.nodes[nid];

        {
            std::vector<FGResourceAccessDecl> remapped;
            remapped.reserve(node.accesses.size());
            for (const FGResourceAccessDecl& a : node.accesses)
            {
                if (a.resource == FG_INVALID_RESOURCE || a.resource >= oldResourceCount)
                    continue;
                const uint32_t mapped = resourceRemap[a.resource];
                if (mapped == FG_INVALID_RESOURCE)
                    continue;
                FGResourceAccessDecl b = a;
                b.resource = mapped;
                remapped.push_back(b);
            }
            node.accesses.swap(remapped);
        }
        node.dependencies.clear();
        newNodes.push_back(std::move(node));
    }

    fg.resources = std::move(newResources);
    fg.nodes = std::move(newNodes);
    fg.executionOrder.clear();
}

bool GDXRenderFrameGraph::BuildExecutionOrder(RFG::FrameGraph& fg) const
{
    fg.executionOrder.clear();
    const uint32_t nodeCount = static_cast<uint32_t>(fg.nodes.size());
    std::vector<uint32_t> indegree(nodeCount, 0u);
    std::vector<std::vector<uint32_t>> dependents(nodeCount);

    for (uint32_t i = 0u; i < nodeCount; ++i)
        for (uint32_t dep : fg.nodes[i].dependencies)
            if (dep < nodeCount && dep != i) { ++indegree[i]; dependents[dep].push_back(i); }

    std::vector<uint32_t> ready;
    ready.reserve(nodeCount);
    for (uint32_t i = 0u; i < nodeCount; ++i)
        if (indegree[i] == 0u) ready.push_back(i);

    while (!ready.empty())
    {
        uint32_t bestPos = 0u;
        for (uint32_t k = 1u; k < static_cast<uint32_t>(ready.size()); ++k)
            if (ready[k] < ready[bestPos]) bestPos = k;

        const uint32_t nodeIndex = ready[bestPos];
        ready.erase(ready.begin() + static_cast<std::ptrdiff_t>(bestPos));
        fg.executionOrder.push_back(nodeIndex);

        for (uint32_t dep : dependents[nodeIndex])
            if (indegree[dep] > 0u && --indegree[dep] == 0u)
                ready.push_back(dep);
    }

    return fg.executionOrder.size() == nodeCount;
}

bool GDXRenderFrameGraph::Validate(RFG::FrameGraph& fg) const
{
    fg.validation.Reset();
    const uint32_t nodeCount     = static_cast<uint32_t>(fg.nodes.size());
    const uint32_t resourceCount = static_cast<uint32_t>(fg.resources.size());

    auto addError = [&](std::string msg)
    {
        fg.validation.valid = false;
        fg.validation.errors.push_back(std::move(msg));
    };

    auto describeResource = [&](FGResourceID rid) -> std::string
    {
        if (rid == FG_INVALID_RESOURCE || rid >= resourceCount)
            return "resource=" + std::to_string(rid);

        const FGResourceDesc& desc = fg.resources[rid];
        std::string text = "resource=" + std::to_string(rid);
        text += " name='";
        text += !desc.debugName.empty() ? desc.debugName : "<unnamed>";
        text += "'";
        text += " lifetime=";
        text += FGResourceLifetimeToString(desc.lifetime);
        text += " kind=";
        text += FGResourceKindToString(desc.kind);
        text += " extent=";
        text += std::to_string(desc.width);
        text += "x";
        text += std::to_string(desc.height);
        text += " format=";
        text += FGResourceFormatToString(desc.format);
        text += " producer=";
        text += (desc.producerNode == FG_INVALID_NODE) ? "none" : std::to_string(desc.producerNode);
        text += " firstUse=";
        text += (desc.firstUseNode == FG_INVALID_NODE) ? "none" : std::to_string(desc.firstUseNode);
        text += " lastUse=";
        text += (desc.lastUseNode == FG_INVALID_NODE) ? "none" : std::to_string(desc.lastUseNode);
        text += " initialState=";
        text += FGResourceStateToString(desc.plannedInitialState);
        text += " finalState=";
        text += FGResourceStateToString(desc.plannedFinalState);
        return text;
    };

    auto checkResourceID = [&](FGResourceID rid, uint32_t nodeIdx, const char* slot)
    {
        if (rid == FG_INVALID_RESOURCE || rid >= resourceCount)
            addError("FrameGraph " + std::string(slot) + " invalid resource " +
                     std::to_string(rid) + " at node " + std::to_string(nodeIdx));
    };

    for (uint32_t rid = 0u; rid < resourceCount; ++rid)
    {
        const FGResourceDesc& desc = fg.resources[rid];
        if (desc.id != rid)
            addError("FrameGraph resource id mismatch: slot " + std::to_string(rid) +
                     " stores id " + std::to_string(desc.id));

        switch (desc.lifetime)
        {
        case FGResourceLifetime::Imported:
        case FGResourceLifetime::Transient:
            break;
        default:
            addError("FrameGraph invalid lifetime on " + describeResource(rid));
            break;
        }

        if (desc.kind == FGResourceKind::Unknown)
            addError("FrameGraph missing ResourceKind on " + describeResource(rid));

        if (desc.kind == FGResourceKind::Backbuffer && desc.IsTransient())
            addError("FrameGraph backbuffer must not be transient: " + describeResource(rid));

        if (desc.kind == FGResourceKind::History && desc.IsTransient())
            addError("FrameGraph history resource must be imported/persistent: " + describeResource(rid));

        if (desc.kind == FGResourceKind::Shadow && desc.IsImported())
            addError("FrameGraph shadow resource must not be imported/shared implicitly: " + describeResource(rid));

        if (desc.kind == FGResourceKind::Shadow && desc.renderTarget.IsValid())
            addError("FrameGraph shadow resource should not masquerade as color render target: " + describeResource(rid));

        const bool expectsExtent =
            desc.kind == FGResourceKind::Backbuffer ||
            desc.kind == FGResourceKind::Texture ||
            desc.kind == FGResourceKind::RenderTarget ||
            desc.kind == FGResourceKind::Depth ||
            desc.kind == FGResourceKind::Shadow ||
            desc.kind == FGResourceKind::History;

        if (expectsExtent && (desc.width == 0u || desc.height == 0u))
            addError("FrameGraph missing extent metadata on " + describeResource(rid));

        const bool requiresKnownFormat =
            desc.kind == FGResourceKind::Backbuffer ||
            desc.kind == FGResourceKind::Texture ||
            desc.kind == FGResourceKind::RenderTarget ||
            desc.kind == FGResourceKind::History ||
            desc.kind == FGResourceKind::Shadow ||
            desc.kind == FGResourceKind::Depth;

        if (requiresKnownFormat && desc.format == GDXTextureFormat::Unknown)
            addError("FrameGraph missing format metadata on " + describeResource(rid));

        if (desc.plannedInitialState == ResourceState::Unknown)
            addError("FrameGraph missing planned initial resource state on " + describeResource(rid));

        if (desc.plannedInitialStateSource == FGResourceStateSource::Unknown)
            addError("FrameGraph missing planned initial resource state source on " + describeResource(rid));

        if (desc.plannedFinalState == ResourceState::Unknown)
            addError("FrameGraph missing planned final resource state on " + describeResource(rid));

        if (desc.HasUses() && desc.lastUseNode == FG_INVALID_NODE)
            addError("FrameGraph lifetime missing last-use on " + describeResource(rid));

        if (!desc.HasUses() && desc.lastUseNode != FG_INVALID_NODE)
            addError("FrameGraph lifetime has last-use without first-use on " + describeResource(rid));

        if (desc.firstUseNode != FG_INVALID_NODE && desc.firstUseNode >= nodeCount)
            addError("FrameGraph lifetime first-use OOB on " + describeResource(rid));

        if (desc.lastUseNode != FG_INVALID_NODE && desc.lastUseNode >= nodeCount)
            addError("FrameGraph lifetime last-use OOB on " + describeResource(rid));

        if (desc.producerNode != FG_INVALID_NODE && desc.producerNode >= nodeCount)
            addError("FrameGraph lifetime producer OOB on " + describeResource(rid));


        if (desc.producerNode != FG_INVALID_NODE && desc.firstUseNode == FG_INVALID_NODE)
            addError("FrameGraph lifetime producer without use-span on " + describeResource(rid));

        if (desc.producerNode != FG_INVALID_NODE)
        {
            bool producerWrites = false;
            if (desc.producerNode < nodeCount)
            {
                for (const FGResourceAccessDecl& a : fg.nodes[desc.producerNode].accesses)
                    if (a.resource == rid && a.IsWrite()) { producerWrites = true; break; }
            }
            if (!producerWrites)
                addError("FrameGraph lifetime producer does not write resource on " + describeResource(rid));
        }
    }

    std::vector<int32_t> execPos(nodeCount, -1);
    for (uint32_t p = 0u; p < static_cast<uint32_t>(fg.executionOrder.size()); ++p)
    {
        const uint32_t ni = fg.executionOrder[p];
        if (ni < nodeCount)
            execPos[ni] = static_cast<int32_t>(p);
    }

    for (uint32_t rid = 0u; rid < resourceCount; ++rid)
    {
        const FGResourceDesc& desc = fg.resources[rid];
        if (desc.firstUseNode != FG_INVALID_NODE && desc.lastUseNode != FG_INVALID_NODE)
        {
            if (execPos[desc.firstUseNode] == -1 || execPos[desc.lastUseNode] == -1)
            {
                addError("FrameGraph lifetime references node outside executionOrder on " + describeResource(rid));
            }
            else if (execPos[desc.firstUseNode] > execPos[desc.lastUseNode])
            {
                addError("FrameGraph lifetime first-use after last-use on " + describeResource(rid));
            }
        }

        if (desc.producerNode != FG_INVALID_NODE && desc.firstUseNode != FG_INVALID_NODE)
        {
            if (execPos[desc.producerNode] == -1 || execPos[desc.firstUseNode] == -1)
            {
                addError("FrameGraph lifetime producer not in executionOrder on " + describeResource(rid));
            }
            else if (execPos[desc.producerNode] > execPos[desc.firstUseNode])
            {
                addError("FrameGraph lifetime producer after first-use on " + describeResource(rid));
            }
        }
    }

    for (uint32_t i = 0u; i < nodeCount; ++i)
    {
        const RFG::Node& node = fg.nodes[i];

        // --- executeFn vorhanden ---
        if (node.enabled && !node.executeFn)
            addError("FrameGraph enabled node " + std::to_string(i) + " has no executeFn");

        // --- Dependency-Checks ---
        for (uint32_t dep : node.dependencies)
        {
            if (dep >= nodeCount)
            { addError("FrameGraph dep OOB at node " + std::to_string(i)); continue; }
            if (dep == i)
              addError("FrameGraph self-dep at node " + std::to_string(i));
        }

        // --- Access validation (single source of truth) ---
        {
            for (uint32_t a = 0u; a < static_cast<uint32_t>(node.accesses.size()); ++a)
            {
                const FGResourceAccessDecl& access = node.accesses[a];
                checkResourceID(access.resource, i, "access");
                if (access.resource == FG_INVALID_RESOURCE || access.resource >= resourceCount)
                    continue;
                if (access.requiredState == ResourceState::Unknown)
                {
                    addError("FrameGraph access missing required state for " + describeResource(access.resource) +
                             " at node " + std::to_string(i) +
                             " (type=" + std::string(FGResourceAccessTypeToString(access.type)) + ")");
                }

                // Duplicate resource access within a node is usually accidental.
                for (uint32_t b = a + 1u; b < static_cast<uint32_t>(node.accesses.size()); ++b)
                {
                    const auto& other = node.accesses[b];
                    if (other.resource == access.resource)
                    {
                        addError("FrameGraph duplicate access to " + describeResource(access.resource) +
                                 " at node " + std::to_string(i));
                        break;
                    }
                }
            }
        }

        bool readsMainViewShadow = false;
        bool hasLocalShadowWrite = false;
        for (const FGResourceAccessDecl& a : node.accesses)
        {
            if (a.resource >= resourceCount)
                continue;
            const FGResourceDesc& res = fg.resources[a.resource];
            if (a.IsRead() && res.debugName == "ShadowMap.MainView")
                readsMainViewShadow = true;
            if (a.IsWrite() && res.kind == FGResourceKind::Shadow)
                hasLocalShadowWrite = true;
        }

        if (node.countedAsRenderTargetView && readsMainViewShadow &&
            node.shadowResourcePolicy != FGShadowResourcePolicy::GlobalSharedMainView)
        {
            addError("FrameGraph RTT node reads main-view shadow without explicit GlobalSharedMainView policy at node " + std::to_string(i));
        }

        if (node.shadowResourcePolicy == FGShadowResourcePolicy::GlobalSharedMainView && hasLocalShadowWrite)
        {
            addError("FrameGraph node mixes GlobalSharedMainView shadow policy with local shadow production at node " + std::to_string(i));
        }

        // --- RAW: each read must be ordered after a writer OR be an imported external input ---
        for (const FGResourceAccessDecl& a : node.accesses)
        {
            if (!a.IsRead())
                continue;
            const FGResourceID rid = a.resource;
            if (rid == FG_INVALID_RESOURCE || rid >= resourceCount)
                continue;

            bool hasWriter = false;
            for (uint32_t dep : node.dependencies)
            {
                if (dep >= nodeCount) continue;
                for (const FGResourceAccessDecl& wa : fg.nodes[dep].accesses)
                {
                    if (wa.resource == rid && wa.IsWrite()) { hasWriter = true; break; }
                }
                if (hasWriter) break;
            }

            const FGResourceDesc& res = fg.resources[rid];
            const bool allowImportedRead = (res.IsImported() && !res.HasProducer());
            if (!hasWriter && !allowImportedRead)
                addError("FrameGraph RAW: " + describeResource(rid) +
                         " at node " + std::to_string(i) + " has no writer dep");
        }

        // --- WAW/WAR: writer must depend on the last accessor ---
        for (const FGResourceAccessDecl& a : node.accesses)
        {
            if (!a.IsWrite())
                continue;
            const FGResourceID rid = a.resource;
            if (rid == FG_INVALID_RESOURCE || rid >= resourceCount)
                continue;
            int lastAccessor = -1;
            for (int j = static_cast<int>(i) - 1; j >= 0; --j)
            {
                const auto& prev = fg.nodes[j];
                bool hit = false;
                for (const FGResourceAccessDecl& pa : prev.accesses)
                    if (pa.resource == rid && (pa.IsRead() || pa.IsWrite())) { hit = true; break; }
                if (hit) { lastAccessor = j; break; }
            }
            if (lastAccessor >= 0 && !HasDependency(node, static_cast<uint32_t>(lastAccessor)))
                addError("FrameGraph WAW/WAR: " + describeResource(rid) +
                         " node " + std::to_string(i) + " missing dep on " + std::to_string(lastAccessor));
        }
    }

    // --- Execution-Order vollständig ---
    if (fg.executionOrder.size() != static_cast<size_t>(nodeCount))
    
    {
        std::string detail = "FrameGraph dependency cycle detected. Nodes:";
        for (uint32_t i = 0u; i < nodeCount; ++i)
        {
            detail += " [" + std::to_string(i) + ":" + fg.nodes[i].debugName + " deps=";
            for (uint32_t d = 0u; d < static_cast<uint32_t>(fg.nodes[i].dependencies.size()); ++d)
            {
                if (d > 0u) detail += ",";
                detail += std::to_string(fg.nodes[i].dependencies[d]);
            }
            detail += "]";
        }
        addError(detail);
        return fg.validation.valid;
    }

    // --- Execution-Order: keine OOB, keine Duplikate, alle enthalten ---
    std::vector<int32_t> pos(nodeCount, -1);
    for (uint32_t p = 0u; p < static_cast<uint32_t>(fg.executionOrder.size()); ++p)
    {
        const uint32_t ni = fg.executionOrder[p];
        if (ni >= nodeCount) { addError("FrameGraph exec OOB at pos " + std::to_string(p)); continue; }
        if (pos[ni] != -1)  { addError("FrameGraph duplicate in executionOrder: node " + std::to_string(ni)); continue; }
        pos[ni] = static_cast<int32_t>(p);
    }
    for (uint32_t i = 0u; i < nodeCount; ++i)
        if (pos[i] == -1) addError("FrameGraph missing node " + std::to_string(i) + " in executionOrder");

    // --- Reihenfolge respektiert Deps ---
    for (uint32_t i = 0u; i < nodeCount; ++i)
    {
        if (pos[i] == -1) continue;
        for (uint32_t dep : fg.nodes[i].dependencies)
        {
            if (dep >= nodeCount || pos[dep] == -1) continue;
            if (pos[dep] >= pos[i])
                addError("FrameGraph order violation: dep " + std::to_string(dep) +
                         " must execute before node " + std::to_string(i));
        }
    }

    if (fg.validation.valid)
    {
        for (uint32_t rid = 0u; rid < resourceCount; ++rid)
        {
            const FGResourceDesc& desc = fg.resources[rid];
            Debug::Log(GDX_SRC_LOC,
                "FrameGraph resource state plan: id=", rid,
                " name='", desc.debugName,
                "' initial=", FGResourceStateToString(desc.plannedInitialState),
                " source=", FGResourceStateSourceToString(desc.plannedInitialStateSource),
                " final=", FGResourceStateToString(desc.plannedFinalState),
                " producer=", desc.producerNode,
                " firstUse=", desc.firstUseNode,
                " lastUse=", desc.lastUseNode);

            const bool suspiciousImportedWriteStart =
                desc.IsImported() &&
                !desc.HasProducer() &&
                desc.plannedInitialStateSource == FGResourceStateSource::ImportedFirstUse &&
                (desc.plannedInitialState == ResourceState::RenderTarget ||
                 desc.plannedInitialState == ResourceState::DepthWrite ||
                 desc.plannedInitialState == ResourceState::UnorderedAccess);

            if (suspiciousImportedWriteStart)
            {
                DBWARN(GDX_SRC_LOC,
                    "FrameGraph suspicious imported initial state: id=", rid,
                    " name='", desc.debugName,
                    "' initial=", FGResourceStateToString(desc.plannedInitialState),
                    " source=", FGResourceStateSourceToString(desc.plannedInitialStateSource),
                    " producer=", desc.producerNode,
                    " firstUse=", desc.firstUseNode,
                    " lastUse=", desc.lastUseNode);
            }
        }

        for (uint32_t i = 0u; i < nodeCount; ++i)
        {
            const RFG::Node& node = fg.nodes[i];
            for (const FGPlannedTransition& transition : node.beginTransitions)
            {
                if (transition.resource >= resourceCount)
                    continue;
                const FGResourceDesc& resource = fg.resources[transition.resource];
                Debug::Log(GDX_SRC_LOC,
                    "FrameGraph begin transition: node=", i,
                    " resource='", resource.debugName,
                    "' ", FGResourceStateToString(transition.before),
                    " -> ", FGResourceStateToString(transition.after));
            }
            for (const FGPlannedTransition& transition : node.endTransitions)
            {
                if (transition.resource >= resourceCount)
                    continue;
                const FGResourceDesc& resource = fg.resources[transition.resource];
                Debug::Log(GDX_SRC_LOC,
                    "FrameGraph end transition: node=", i,
                    " resource='", resource.debugName,
                    "' ", FGResourceStateToString(transition.before),
                    " -> ", FGResourceStateToString(transition.after));
            }
        }
    }

    return fg.validation.valid;
}

// ---------------------------------------------------------------------------
// Execute
// ---------------------------------------------------------------------------

void GDXRenderFrameGraph::ExecuteNode(RFG::Node& node, const RFG::ExecContext& ctx)
{
    if (!node.enabled || !node.executeFn) return;
    RFG::ViewStats* stats = node.statsOutput;
    if (!stats) return;

    if (node.countedAsRenderTargetView)
        stats->countedAsRenderTargetView = true;

    node.executeFn(ctx, stats);
}

void GDXRenderFrameGraph::Execute(RFG::PipelineData& pipeline, const RFG::ExecContext& ctx)
{
    if (!pipeline.frameGraph.validation.valid)
    {
        for (const auto& err : pipeline.frameGraph.validation.errors)
            DBERROR(GDX_SRC_LOC, err);
        return;
    }

    std::vector<uint8_t> executed(pipeline.frameGraph.nodes.size(), 0u);
    const RFG::ExecuteData* lastUpdatedExec = nullptr;

    for (uint32_t nodeIndex : pipeline.frameGraph.executionOrder)
    {
        if (nodeIndex >= pipeline.frameGraph.nodes.size())
        { DBERROR(GDX_SRC_LOC, "FrameGraph execution index OOB"); return; }

        RFG::Node& node = pipeline.frameGraph.nodes[nodeIndex];

        for (uint32_t dep : node.dependencies)
        {
            if (dep >= executed.size() || executed[dep] == 0u)
            { DBERROR(GDX_SRC_LOC, "FrameGraph dep not ready"); return; }
        }

        if (ctx.backend && node.enabled && node.executeInput &&
            node.updateFrameConstants &&
            node.executeInput != lastUpdatedExec)
        {
            ctx.backend->UpdateFrameConstants(node.executeInput->frame);
            lastUpdatedExec = node.executeInput;
        }

        // Materialize per-node planned transitions into backend payloads.
        std::vector<BackendPlannedTransition> beginBackend;
        std::vector<BackendPlannedTransition> endBackend;
        RFG::ExecContext localCtx = ctx;
        if (ctx.backend && ctx.frameGraph)
        {
            const RFG::FrameGraph& fg = *ctx.frameGraph;
            beginBackend.reserve(node.beginTransitions.size());
            for (const FGPlannedTransition& t : node.beginTransitions)
            {
                if (t.resource == FG_INVALID_RESOURCE || t.resource >= fg.resources.size())
                    continue;
                const FGResourceDesc& res = fg.resources[t.resource];
                BackendPlannedTransition bt{};
                bt.texture = res.texture;
                bt.renderTarget = res.renderTarget;
                bt.before = t.before;
                bt.after = t.after;
                bt.debugName = res.debugName.c_str();
                beginBackend.push_back(bt);
            }

            endBackend.reserve(node.endTransitions.size());
            for (const FGPlannedTransition& t : node.endTransitions)
            {
                if (t.resource == FG_INVALID_RESOURCE || t.resource >= fg.resources.size())
                    continue;
                const FGResourceDesc& res = fg.resources[t.resource];
                BackendPlannedTransition bt{};
                bt.texture = res.texture;
                bt.renderTarget = res.renderTarget;
                bt.before = t.before;
                bt.after = t.after;
                bt.debugName = res.debugName.c_str();
                endBackend.push_back(bt);
            }

            localCtx.beginTransitions = &beginBackend;
            localCtx.endTransitions = &endBackend;
        }

        ExecuteNode(node, localCtx);
        executed[nodeIndex] = 1u;
    }
}
