#include "RenderPassBuilder.h"
#include "IGDXRenderBackend.h"
#include "BackendRenderPassDesc.h"
#include "Core/Debug.h"
#include "RenderGatherSystem.h"

namespace RenderPassBuilder
{

namespace
{
    bool HasActivePostProcessPass(const PostProcContext& ppCtx)
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

    RenderTargetHandle* ResolveSceneTargetSlot(RFG::ViewPassData& view, const PostProcContext& ppCtx)
    {
        if (view.prepared.graphicsView.type == RenderViewType::RenderTarget)
        {
            if (!ppCtx.rttSceneTargets)
                return nullptr;
            return &(*ppCtx.rttSceneTargets)[view.prepared.graphicsView.renderTarget];
        }
        return ppCtx.mainSceneTarget;
    }

    bool IsRequiredSlotAvailable(
        const PostProcessInputSlot& slot,
        const GDXRenderTargetResource& sceneRt)
    {
        switch (slot.semantic)
        {
        case PostProcessInputSemantic::SceneColor:
        case PostProcessInputSemantic::OriginalSceneColor:
            return sceneRt.exposedTexture.IsValid();

        case PostProcessInputSemantic::SceneDepth:
            return sceneRt.exposedDepthTexture.IsValid();

        case PostProcessInputSemantic::SceneNormals:
            return false;

        case PostProcessInputSemantic::Custom:
            return slot.customTexture.IsValid();

        default:
            return false;
        }
    }

    bool PassCanExecuteForSceneTarget(
        const PostProcessResource& pass,
        const GDXRenderTargetResource& sceneRt)
    {
        const std::vector<PostProcessInputSlot> resolvedInputs =
            pass.inputs.empty()
                ? BuildDefaultPostProcessInputs(pass.desc.inputSlots)
                : pass.inputs;

        for (const PostProcessInputSlot& slot : resolvedInputs)
        {
            if (!slot.required)
                continue;

            if (!IsRequiredSlotAvailable(slot, sceneRt))
                return false;
        }

        return true;
    }

    bool HasExecutablePostProcessPassForView(
        const PostProcContext& ppCtx,
        const GDXRenderTargetResource& sceneRt)
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
            Debug::LogWarning(
                GDX_SRC_LOC,
                L"PrepareViewPostProcess: Kein aktiver PostProcess-Pass ist fuer diesen View ausfuehrbar. Fallback auf direkten Graphics-Pass.");
        }

        return false;
    }

    bool PrepareViewPostProcess(
        RFG::ViewPassData& view,
        const PostProcContext& ppCtx,
        bool outputToBackbuffer)
    {
        if (!ppCtx.backend || !ppCtx.rtStore || !ppCtx.texStore)
            return false;

        // Diagnoseschalter: Main-View direkt ins Backbuffer rendern, um den
        // Presentation-/PostProcess-Pfad als Fehlerquelle auszuschliessen.
        if (outputToBackbuffer)
        {
            //Debug::Log(
            //    GDX_SRC_LOC,
            //    L"PrepareViewPostProcess: Main-View-PostProcess testweise deaktiviert. Direkter Graphics-Pass ins Backbuffer.");
            return false;
        }

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
        view.execute.presentation.postProcess.execInputs.sceneNormals = TextureHandle::Invalid();
        view.execute.presentation.postProcess.execInputs.cameraNearPlane = view.prepared.frame.cameraNearPlane;
        view.execute.presentation.postProcess.execInputs.cameraFarPlane = view.prepared.frame.cameraFarPlane;
        view.execute.presentation.postProcess.execInputs.cameraIsOrtho = (view.prepared.frame.cameraProjectionFlags & 1u) ? 1u : 0u;
        view.execute.presentation.postProcess.execInputs.depthDebugFlags = 1u;
        view.execute.presentation.postProcess.outputToBackbuffer = outputToBackbuffer;
        view.execute.presentation.postProcess.outputTarget =
            outputToBackbuffer ? RenderTargetHandle::Invalid()
                               : view.prepared.graphicsView.renderTarget;

        BuildGraphicsPassExecuteInput(
            view,
            RenderPassTargetDesc::Offscreen(
                sceneTarget,
                view.prepared.clearDesc,
                view.prepared.frame.viewportWidth,
                view.prepared.frame.viewportHeight,
                outputToBackbuffer ? L"MainScenePostProcess" : L"RTTScenePostProcess"),
            true,
            outputToBackbuffer ? true : view.prepared.shadowEnabled);

        return true;
    }
}

// ---------------------------------------------------------------------------
void ConfigureCommonExecuteInputs(RFG::ViewPassData& view, bool presentAfterExecute)
{
    view.execute.Reset();
    view.execute.frame = view.prepared.frame;
    view.execute.presentation.presentAfterExecute = presentAfterExecute;
}

void BuildShadowPassExecuteInput(RFG::ViewPassData& view)
{
    view.execute.shadowPass.Reset();
    view.execute.shadowPass.enabled =
        view.prepared.shadowEnabled && !view.shadowQueue.Empty();
    if (!view.execute.shadowPass.enabled) return;

    view.execute.shadowPass.desc = BackendRenderPassDesc::Shadow(view.execute.frame);
}

void BuildGraphicsPassExecuteInput(
    RFG::ViewPassData&          view,
    const RenderPassTargetDesc& targetDesc,
    bool                        appendGraphicsVisibleSet,
    bool                        appendShadowVisibleSet)
{
    view.execute.graphicsPass.Reset();
    view.execute.graphicsPass.enabled = true;
    view.execute.graphicsPass.desc    = BackendRenderPassDesc::Graphics(
        targetDesc,
        &view.execute.frame,
        RenderPass::Opaque);
    view.execute.graphicsPass.appendGraphicsVisibleSet = appendGraphicsVisibleSet;
    view.execute.graphicsPass.appendShadowVisibleSet   = appendShadowVisibleSet;
    view.execute.graphicsPass.sortQueueBeforeExecute   = true;
}

void BuildExecutionQueues(RFG::ViewPassData& view, const DebugAppendFn& debugFn)
{
    view.execute.shadowQueue = view.shadowQueue;
    view.execute.opaqueQueue = view.opaqueQueue;
    view.execute.alphaQueue  = view.transparentQueue;

    const RFG::PassExec& passExec = view.execute.graphicsPass;
    RenderViewData graphicsViewForDebug = view.prepared.graphicsView;
    graphicsViewForDebug.frame = view.realCameraFrame;

    if (debugFn)
    {
        if (passExec.appendGraphicsVisibleSet)
            debugFn(view.execute.opaqueQueue, view.graphicsVisibleSet, graphicsViewForDebug, &view.stats);

        if (passExec.appendShadowVisibleSet && view.prepared.shadowEnabled)
            debugFn(view.execute.opaqueQueue, view.shadowVisibleSet, view.prepared.shadowView, &view.stats);
    }

    if (passExec.sortQueueBeforeExecute)
    {
        view.execute.opaqueQueue.Sort();
        view.execute.alphaQueue.Sort();
    }
}

bool PrepareMainViewPostProcess(RFG::ViewPassData& view, const PostProcContext& ppCtx)
{
    return PrepareViewPostProcess(view, ppCtx, /*outputToBackbuffer=*/true);
}

void BuildMainViewExecuteInputs(
    RFG::ViewPassData&     view,
    const PostProcContext& ppCtx,
    const DebugAppendFn&   debugFn)
{
    ConfigureCommonExecuteInputs(view, /*presentAfterExecute=*/true);
    BuildShadowPassExecuteInput(view);
    if (!PrepareMainViewPostProcess(view, ppCtx))
        BuildGraphicsPassExecuteInput(view, view.prepared.graphicsTargetDesc, true, true);
    BuildExecutionQueues(view, debugFn);
}

void BuildRTTExecuteInputs(
    std::vector<RFG::ViewPassData>&                           views,
    ResourceStore<GDXRenderTargetResource, RenderTargetTag>& rtStore,
    const PostProcContext&                                    ppCtx,
    const DebugAppendFn&                                      debugFn)
{
    for (auto& view : views)
    {
        ConfigureCommonExecuteInputs(view, /*presentAfterExecute=*/false);

        const GDXRenderTargetResource* rt = rtStore.Get(view.prepared.graphicsView.renderTarget);
        if (!rt || !rt->ready) continue;

        BuildShadowPassExecuteInput(view);
        if (!PrepareViewPostProcess(view, ppCtx, /*outputToBackbuffer=*/false))
        {
            BuildGraphicsPassExecuteInput(
                view,
                view.prepared.graphicsTargetDesc,
                true,
                view.prepared.shadowEnabled);
        }
        BuildExecutionQueues(view, debugFn);
    }
}

void BuildFrameExecuteInputs(
    RFG::PipelineData&                                       pipeline,
    ResourceStore<GDXRenderTargetResource, RenderTargetTag>& rtStore,
    const PostProcContext&                                   ppCtx,
    const DebugAppendFn&                                     debugFn)
{
    BuildRTTExecuteInputs(pipeline.rttViews, rtStore, ppCtx, debugFn);
    BuildMainViewExecuteInputs(pipeline.mainView, ppCtx, debugFn);
}

} // namespace RenderPassBuilder
