#include "GDXMainRenderPasses.h"


#include "BackendRenderPassDesc.h"
#include "Core/Debug.h"
#include "Core/GDXMathOps.h"
#include "IGDXRenderBackend.h"
#include "RenderFeatureModules.h"
#include "RenderSortKeyDebug.h"

#include <cstring>
#include <utility>
#include <vector>

namespace GDXMainRenderPasses
{
namespace
{
    using RenderPassBuilder::PostProcContext;

    template <typename TPass>
    inline void RunPassLifecycle(RFG::ViewPassData& view, const PassBuildContext& ctx)
    {
        TPass::Setup(view, ctx);
        TPass::RegisterResources(view, ctx);
        TPass::Compile(view, ctx);
        TPass::Execute(view, ctx);
    }

    template <typename... TPasses>
    inline void RunPassSequence(RFG::ViewPassData& view, const PassBuildContext& ctx)
    {
        (RunPassLifecycle<TPasses>(view, ctx), ...);
    }

    inline void ConfigureCommonExecuteInputs(RFG::ViewPassData& view, bool presentAfterExecute)
    {
        view.execute.Reset();
        view.execute.frame = view.prepared.frame;
        view.execute.presentation.presentAfterExecute = presentAfterExecute;
    }

    inline void BuildShadowPassExecuteInput(RFG::ViewPassData& view)
    {
        RenderFeatureModules::ShadowFeatureBuilder::BuildPass(view);
    }

    inline void BuildGraphicsPassExecuteInputs(
        RFG::ViewPassData&          view,
        const RenderPassTargetDesc& targetDesc,
        bool                        appendGraphicsVisibleSet,
        bool                        appendShadowVisibleSet,
        bool                        enableParticles)
    {
        RenderFeatureModules::DepthFeatureBuilder::BuildPass(view, targetDesc);

        view.execute.opaquePass.Reset();
        view.execute.opaquePass.enabled = view.featurePlan.enableOpaquePass;
        view.execute.opaquePass.desc = BackendRenderPassDesc::Graphics(
            targetDesc,
            &view.execute.frame,
            RenderPass::Opaque,
            true,
            false,
            view.execute.depthPass.enabled);
        view.execute.opaquePass.appendGraphicsVisibleSet = appendGraphicsVisibleSet;
        view.execute.opaquePass.appendShadowVisibleSet = appendShadowVisibleSet;

        RenderFeatureModules::MotionVectorsFeatureBuilder::BuildPass(view, targetDesc);
        RenderFeatureModules::ParticleFeatureBuilder::BuildPass(view, targetDesc);
        RenderFeatureModules::TransparencyFeatureBuilder::BuildPasses(view, targetDesc);

        if (!enableParticles)
            view.execute.particlePass.Reset();
    }

    inline void BuildExecutionQueues(RFG::ViewPassData& view, const RenderPassBuilder::DebugAppendFn& debugFn)
    {
        view.execute.shadowQueue = view.renderQueues.shadowDepthQueue;
        RenderFeatureModules::DepthFeatureBuilder::FinalizeQueue(view);
        view.execute.opaqueQueue = view.renderQueues.opaqueQueue;

        const RFG::PassExec& opaqueExec = view.execute.opaquePass;
        RenderViewData graphicsViewForDebug = view.prepared.graphicsView;
        graphicsViewForDebug.frame = view.realCameraFrame;

        if (debugFn)
        {
            if (opaqueExec.appendGraphicsVisibleSet)
                debugFn(view.execute.opaqueQueue, view.graphicsVisibleSet, graphicsViewForDebug, &view.stats);

            if (opaqueExec.appendShadowVisibleSet && view.prepared.shadowEnabled)
                debugFn(view.execute.opaqueQueue, view.shadowVisibleSet, view.prepared.shadowView, &view.stats);
        }

        if (opaqueExec.sortQueueBeforeExecute)
            view.execute.opaqueQueue.Sort();

        RenderFeatureModules::MotionVectorsFeatureBuilder::FinalizeQueue(view);
        view.execute.particleQueue = view.renderQueues.particleQueue;
        RenderFeatureModules::TransparencyFeatureBuilder::FinalizeQueues(view);

        if (RenderSortKeyDebug::IsEnabled())
        {
            const size_t maxCommands = RenderSortKeyDebug::GetOptions().maxCommandsPerQueue;
            view.execute.opaqueQueue.DebugDump("Opaque", maxCommands);
            view.execute.shadowQueue.DebugDump("ShadowDepth", maxCommands);
            if (!view.execute.particleQueue.Empty())
            {
                Debug::Log(
                    GDX_SRC_LOC,
                    "ParticleQueue: alpha=", static_cast<uint32_t>(view.execute.particleQueue.AlphaCount()),
                    " additive=", static_cast<uint32_t>(view.execute.particleQueue.AdditiveCount()));
            }
        }
    }

    inline bool HasActivePostProcessPass(const PostProcContext& ppCtx)
    {
        if (!ppCtx.postProcessStore || !ppCtx.passOrder)
            return false;

        for (const PostProcessHandle h : *ppCtx.passOrder)
        {
            const PostProcessResource* pass = ppCtx.postProcessStore->Get(h);
            if (pass && pass->ready && pass->enabled)
                return true;
        }
        return false;
    }

    inline RenderTargetHandle* ResolveSceneTargetSlot(RFG::ViewPassData& view, const PostProcContext& ppCtx)
    {
        if (view.prepared.graphicsView.type == RenderViewType::RenderTarget)
        {
            if (!ppCtx.rttSceneTargets)
                return nullptr;
            return &(*ppCtx.rttSceneTargets)[view.prepared.graphicsView.renderTarget];
        }
        return ppCtx.mainSceneTarget;
    }

    inline bool IsRequiredSlotAvailable(const PostProcessInputSlot& slot, const GDXRenderTargetResource& sceneRt)
    {
        switch (slot.semantic)
        {
        case PostProcessInputSemantic::SceneColor:
        case PostProcessInputSemantic::OriginalSceneColor:
            return sceneRt.exposedTexture.IsValid();
        case PostProcessInputSemantic::SceneDepth:
            return sceneRt.exposedDepthTexture.IsValid();
        case PostProcessInputSemantic::SceneNormals:
            return sceneRt.exposedNormalsTexture.IsValid();
        case PostProcessInputSemantic::Custom:
            return slot.customTexture.IsValid();
        default:
            return false;
        }
    }

    inline bool PassCanExecuteForSceneTarget(const PostProcessResource& pass, const GDXRenderTargetResource& sceneRt)
    {
        const std::vector<PostProcessInputSlot> resolvedInputs =
            pass.inputs.empty() ? BuildDefaultPostProcessInputs(pass.desc.inputSlots) : pass.inputs;

        for (const PostProcessInputSlot& slot : resolvedInputs)
        {
            if (!slot.required)
                continue;
            if (!IsRequiredSlotAvailable(slot, sceneRt))
                return false;
        }
        return true;
    }

    inline bool HasExecutablePostProcessPassForView(const PostProcContext& ppCtx, const GDXRenderTargetResource& sceneRt)
    {
        if (!ppCtx.postProcessStore || !ppCtx.passOrder)
            return false;

        bool hadActivePass = false;
        for (const PostProcessHandle h : *ppCtx.passOrder)
        {
            const PostProcessResource* pass = ppCtx.postProcessStore->Get(h);
            if (!pass || !pass->ready || !pass->enabled)
                continue;

            hadActivePass = true;
            if (PassCanExecuteForSceneTarget(*pass, sceneRt))
                return true;
        }

        if (hadActivePass)
        {
            Debug::LogWarning(GDX_SRC_LOC,
                L"PostProcessPresentationPass: no active post-process pass can execute for this view. Falling back to direct scene output.");
        }
        return false;
    }

    inline std::vector<PostProcessPassConstantOverride> BuildPerViewConstantOverrides(
        const RFG::ExecuteData& exec,
        const PostProcContext& ppCtx)
    {
        std::vector<PostProcessPassConstantOverride> overrides;
        if (!ppCtx.postProcessStore || !ppCtx.passOrder)
            return overrides;

        struct DepthDebugParams
        {
            float nearPlane = 0.1f;
            float farPlane = 1000.0f;
            uint32_t isOrtho = 0u;
            uint32_t flags = 1u;
        } depthParams{
            exec.presentation.postProcess.execInputs.cameraNearPlane,
            exec.presentation.postProcess.execInputs.cameraFarPlane,
            exec.presentation.postProcess.execInputs.cameraIsOrtho,
            exec.presentation.postProcess.execInputs.depthDebugFlags
        };

        for (const PostProcessHandle handle : *ppCtx.passOrder)
        {
            const PostProcessResource* pass = ppCtx.postProcessStore->Get(handle);
            if (!pass || !pass->ready || !pass->enabled)
                continue;

            PostProcessPassConstantOverride ov{};
            ov.pass = handle;
            ov.constantData = pass->constantData;

            if (pass->desc.pixelShaderFile == L"PostProcessDepthDebugPS.hlsl")
            {
                if (pass->constantBufferBytes >= sizeof(DepthDebugParams) && ov.constantData.size() >= sizeof(DepthDebugParams))
                    std::memcpy(ov.constantData.data(), &depthParams, sizeof(DepthDebugParams));
            }
            else if (pass->desc.pixelShaderFile == L"PostProcessGTAOPS.hlsl")
            {
                if (pass->constantBufferBytes >= sizeof(GTAOParams) && ov.constantData.size() >= sizeof(GTAOParams))
                {
                    GTAOParams* params = reinterpret_cast<GTAOParams*>(ov.constantData.data());
                    params->nearPlane = exec.presentation.postProcess.execInputs.cameraNearPlane;
                    params->farPlane  = exec.presentation.postProcess.execInputs.cameraFarPlane;
                    params->projScaleX = exec.presentation.postProcess.execInputs.cameraProjScaleX;
                    params->projScaleY = exec.presentation.postProcess.execInputs.cameraProjScaleY;
                    params->cameraIsOrtho = exec.presentation.postProcess.execInputs.cameraIsOrtho;
                }
            }
            else if (pass->desc.pixelShaderFile == L"PostProcessGTAOBlurPS.hlsl")
            {
                if (pass->constantBufferBytes >= sizeof(GTAOBlurParams) && ov.constantData.size() >= sizeof(GTAOBlurParams))
                {
                    GTAOBlurParams* params = reinterpret_cast<GTAOBlurParams*>(ov.constantData.data());
                    params->nearPlane = exec.presentation.postProcess.execInputs.cameraNearPlane;
                    params->farPlane  = exec.presentation.postProcess.execInputs.cameraFarPlane;
                    params->cameraIsOrtho = exec.presentation.postProcess.execInputs.cameraIsOrtho;
                }
            }
            else if (pass->desc.pixelShaderFile == L"PostProcessDepthFogPS.hlsl")
            {
                if (pass->constantBufferBytes >= sizeof(FogParams) && ov.constantData.size() >= sizeof(FogParams))
                {
                    FogParams* params = reinterpret_cast<FogParams*>(ov.constantData.data());
                    params->cameraNearPlane = exec.presentation.postProcess.execInputs.cameraNearPlane;
                    params->cameraFarPlane  = exec.presentation.postProcess.execInputs.cameraFarPlane;
                    params->projScaleX = exec.presentation.postProcess.execInputs.cameraProjScaleX;
                    params->projScaleY = exec.presentation.postProcess.execInputs.cameraProjScaleY;
                    params->cameraIsOrtho = exec.presentation.postProcess.execInputs.cameraIsOrtho;
                }
            }
            else if (pass->desc.pixelShaderFile == L"PostProcessVolumetricFogPS.hlsl")
            {
                if (pass->constantBufferBytes >= sizeof(VolumetricFogParams) && ov.constantData.size() >= sizeof(VolumetricFogParams))
                {
                    VolumetricFogParams* params = reinterpret_cast<VolumetricFogParams*>(ov.constantData.data());
                    params->cameraNearPlane = exec.presentation.postProcess.execInputs.cameraNearPlane;
                    params->cameraFarPlane = exec.presentation.postProcess.execInputs.cameraFarPlane;
                    params->projScaleX = exec.presentation.postProcess.execInputs.cameraProjScaleX;
                    params->projScaleY = exec.presentation.postProcess.execInputs.cameraProjScaleY;
                    params->cameraIsOrtho = exec.presentation.postProcess.execInputs.cameraIsOrtho;
                    params->cascadeCount = exec.presentation.postProcess.execInputs.shadowCascadeCount;
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
                }
            }

            if (!ov.constantData.empty())
                overrides.push_back(std::move(ov));
        }

        return overrides;
    }

    inline bool PrepareViewPostProcess(
        RFG::ViewPassData& view,
        const PostProcContext& ppCtx,
        bool outputToBackbuffer)
    {
        if (!ppCtx.backend || !ppCtx.rtStore || !ppCtx.texStore)
            return false;

        if (!HasActivePostProcessPass(ppCtx))
            return false;

        RenderTargetHandle* sceneTargetSlot = ResolveSceneTargetSlot(view, ppCtx);
        if (!sceneTargetSlot)
            return false;

        const uint32_t targetWidth = static_cast<uint32_t>(
            view.prepared.frame.viewportWidth > 1.0f ? view.prepared.frame.viewportWidth : 1.0f);
        const uint32_t targetHeight = static_cast<uint32_t>(
            view.prepared.frame.viewportHeight > 1.0f ? view.prepared.frame.viewportHeight : 1.0f);

        RenderTargetHandle& sceneTarget = *sceneTargetSlot;
        GDXRenderTargetResource* existing = sceneTarget.IsValid() ? ppCtx.rtStore->Get(sceneTarget) : nullptr;
        const bool needsNew = !existing || existing->width != targetWidth || existing->height != targetHeight;
        if (needsNew)
        {
            if (sceneTarget.IsValid())
                ppCtx.backend->DestroyRenderTarget(sceneTarget, *ppCtx.rtStore, *ppCtx.texStore);

            const std::wstring debugName =
                outputToBackbuffer ? L"MainScenePostProcess"
                                   : L"RTTScenePostProcess";

            sceneTarget = ppCtx.backend->CreateRenderTarget(
                *ppCtx.rtStore, *ppCtx.texStore,
                targetWidth, targetHeight,
                debugName,
                GDXTextureFormat::RGBA16_FLOAT);
        }

        if (!sceneTarget.IsValid())
            return false;

        const GDXRenderTargetResource* sceneRt = ppCtx.rtStore->Get(sceneTarget);
        if (!sceneRt || !sceneRt->exposedTexture.IsValid())
            return false;

        if (!HasExecutablePostProcessPassForView(ppCtx, *sceneRt))
            return false;

        view.execute.presentation.postProcess.enabled = true;
        view.execute.presentation.postProcess.sceneTexture = sceneRt->exposedTexture;
        view.execute.presentation.postProcess.execInputs.sceneColor = sceneRt->exposedTexture;
        view.execute.presentation.postProcess.execInputs.originalSceneColor = sceneRt->exposedTexture;
        view.execute.presentation.postProcess.execInputs.sceneDepth = sceneRt->exposedDepthTexture;
        view.execute.presentation.postProcess.execInputs.sceneNormals = sceneRt->exposedNormalsTexture;
        view.execute.presentation.postProcess.execInputs.cameraNearPlane = view.prepared.frame.cameraNearPlane;
        view.execute.presentation.postProcess.execInputs.cameraFarPlane = view.prepared.frame.cameraFarPlane;
        view.execute.presentation.postProcess.execInputs.cameraProjScaleX = view.prepared.frame.projMatrix._11;
        view.execute.presentation.postProcess.execInputs.cameraProjScaleY = view.prepared.frame.projMatrix._22;
        view.execute.presentation.postProcess.execInputs.cameraIsOrtho = (view.prepared.frame.cameraProjectionFlags & 1u) ? 1u : 0u;
        view.execute.presentation.postProcess.execInputs.depthDebugFlags = 1u;
        view.execute.presentation.postProcess.execInputs.cameraPos = view.prepared.frame.cameraPos;
        view.execute.presentation.postProcess.execInputs.invViewMatrix = KROM::Inverse(view.prepared.frame.viewMatrix);
        view.execute.presentation.postProcess.execInputs.shadowCascadeCount = view.prepared.frame.shadowCascadeCount;
        for (uint32_t c = 0u; c < 4u; ++c)
        {
            view.execute.presentation.postProcess.execInputs.shadowCascadeViewProj[c] = view.prepared.frame.shadowCascadeViewProj[c];
            view.execute.presentation.postProcess.execInputs.shadowCascadeSplits[c] = view.prepared.frame.shadowCascadeSplits[c];
        }
        view.execute.presentation.postProcess.execInputs.shadowLightDir = { 0.0f, -1.0f, 0.0f };
        for (uint32_t li = 0u; li < view.prepared.frame.lightCount; ++li)
        {
            const LightEntry& le = view.prepared.frame.lights[li];
            if (le.position.w == 0.0f && le.direction.w > 0.5f)
            {
                view.execute.presentation.postProcess.execInputs.shadowLightDir = { le.direction.x, le.direction.y, le.direction.z };
                break;
            }
        }
        if (ppCtx.passOrder)
            view.execute.presentation.postProcess.orderedPasses = *ppCtx.passOrder;
        view.execute.presentation.postProcess.constantOverrides = BuildPerViewConstantOverrides(view.execute, ppCtx);
        view.execute.presentation.postProcess.outputToBackbuffer = outputToBackbuffer;
        view.execute.presentation.postProcess.outputTarget =
            outputToBackbuffer ? RenderTargetHandle::Invalid()
                               : view.prepared.graphicsView.renderTarget;

        BuildGraphicsPassExecuteInputs(
            view,
            RenderPassTargetDesc::Offscreen(
                sceneTarget,
                view.prepared.clearDesc,
                view.prepared.frame.viewportWidth,
                view.prepared.frame.viewportHeight,
                outputToBackbuffer ? L"MainScenePostProcess" : L"RTTScenePostProcess"),
            true,
            outputToBackbuffer ? true : view.prepared.shadowEnabled,
            !view.renderQueues.particleQueue.Empty());

        return true;
    }
}

void CommonViewPass::Setup(RFG::ViewPassData& view, const PassBuildContext& ctx)
{
    ConfigureCommonExecuteInputs(view, ctx.presentAfterExecute);
}

void CommonViewPass::RegisterResources(RFG::ViewPassData&, const PassBuildContext&) {}

void CommonViewPass::Compile(RFG::ViewPassData& view, const PassBuildContext& ctx)
{
    RenderFeatureModules::BuildViewFeaturePlan(view, { ctx.enableParticles }, view.featurePlan);
}

void CommonViewPass::Execute(RFG::ViewPassData&, const PassBuildContext&) {}

void ShadowPass::Setup(RFG::ViewPassData&, const PassBuildContext&) {}
void ShadowPass::RegisterResources(RFG::ViewPassData&, const PassBuildContext&) {}
void ShadowPass::Compile(RFG::ViewPassData& view, const PassBuildContext&)
{
    BuildShadowPassExecuteInput(view);
}
void ShadowPass::Execute(RFG::ViewPassData&, const PassBuildContext&) {}

void MainSceneOpaquePass::Setup(RFG::ViewPassData&, const PassBuildContext&) {}
void MainSceneOpaquePass::RegisterResources(RFG::ViewPassData&, const PassBuildContext&) {}
void MainSceneOpaquePass::Compile(RFG::ViewPassData& view, const PassBuildContext& ctx)
{
    if (ctx.state && ctx.state->postProcessPrepared)
        return;

    BuildGraphicsPassExecuteInputs(
        view,
        view.prepared.graphicsTargetDesc,
        true,
        ctx.isMainView ? true : view.prepared.shadowEnabled,
        ctx.enableParticles);
}
void MainSceneOpaquePass::Execute(RFG::ViewPassData&, const PassBuildContext&) {}

void ParticlePass::Setup(RFG::ViewPassData&, const PassBuildContext&) {}
void ParticlePass::RegisterResources(RFG::ViewPassData&, const PassBuildContext&) {}
void ParticlePass::Compile(RFG::ViewPassData&, const PassBuildContext&) {}
void ParticlePass::Execute(RFG::ViewPassData&, const PassBuildContext&) {}

void ForwardTransparentPass::Setup(RFG::ViewPassData&, const PassBuildContext&) {}
void ForwardTransparentPass::RegisterResources(RFG::ViewPassData&, const PassBuildContext&) {}
void ForwardTransparentPass::Compile(RFG::ViewPassData&, const PassBuildContext&) {}
void ForwardTransparentPass::Execute(RFG::ViewPassData&, const PassBuildContext&) {}

void PostProcessPresentationPass::Setup(RFG::ViewPassData&, const PassBuildContext&) {}
void PostProcessPresentationPass::RegisterResources(RFG::ViewPassData& view, const PassBuildContext& ctx)
{
    if (!ctx.state)
        return;

    ctx.state->postProcessPrepared = false;
    if (!ctx.postProcContext)
        return;

    ctx.state->postProcessPrepared = PrepareViewPostProcess(view, *ctx.postProcContext, ctx.isMainView);
}
void PostProcessPresentationPass::Compile(RFG::ViewPassData&, const PassBuildContext&) {}
void PostProcessPresentationPass::Execute(RFG::ViewPassData&, const PassBuildContext&) {}

void QueueFinalizePass::Setup(RFG::ViewPassData&, const PassBuildContext&) {}
void QueueFinalizePass::RegisterResources(RFG::ViewPassData&, const PassBuildContext&) {}
void QueueFinalizePass::Compile(RFG::ViewPassData&, const PassBuildContext&) {}
void QueueFinalizePass::Execute(RFG::ViewPassData& view, const PassBuildContext& ctx)
{
    const RenderPassBuilder::DebugAppendFn emptyFn{};
    BuildExecutionQueues(view, ctx.debugAppendFn ? *ctx.debugAppendFn : emptyFn);
}

void BuildMainViewPasses(RFG::ViewPassData& view, const PassBuildContext& ctx)
{
    RunPassSequence<
        CommonViewPass,
        ShadowPass,
        PostProcessPresentationPass,
        MainSceneOpaquePass,
        ParticlePass,
        ForwardTransparentPass,
        QueueFinalizePass>(view, ctx);
}

void BuildRTTViewPasses(RFG::ViewPassData& view, const PassBuildContext& ctx)
{
    RunPassSequence<
        CommonViewPass,
        ShadowPass,
        PostProcessPresentationPass,
        MainSceneOpaquePass,
        ParticlePass,
        ForwardTransparentPass,
        QueueFinalizePass>(view, ctx);
}

} // namespace GDXMainRenderPasses

