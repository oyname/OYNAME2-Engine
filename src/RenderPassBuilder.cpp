#include "RenderPassBuilder.h"
#include "IGDXRenderBackend.h"
#include "BackendRenderPassDesc.h"
#include "RenderGatherSystem.h"

namespace RenderPassBuilder
{

// ---------------------------------------------------------------------------
void ConfigureCommonExecuteInputs(RFG::ViewPassData& view, bool presentAfterExecute)
{
    view.execute.Reset();
    // FrameData einfrieren — Execute liest ausschließlich aus execute.frame,
    // nie aus view.prepared.frame.
    view.execute.frame = view.prepared.frame;
    view.execute.presentation.presentAfterExecute = presentAfterExecute;
}

// ---------------------------------------------------------------------------
void BuildShadowPassExecuteInput(RFG::ViewPassData& view)
{
    view.execute.shadowPass.Reset();
    view.execute.shadowPass.enabled =
        view.prepared.shadowEnabled && !view.shadowQueue.Empty();
    if (!view.execute.shadowPass.enabled) return;

    view.execute.shadowPass.desc = BackendRenderPassDesc::Shadow(view.prepared.frame);
}

// ---------------------------------------------------------------------------
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
        &view.prepared.frame,
        RenderPass::Opaque);
    view.execute.graphicsPass.appendGraphicsVisibleSet = appendGraphicsVisibleSet;
    view.execute.graphicsPass.appendShadowVisibleSet   = appendShadowVisibleSet;
    view.execute.graphicsPass.sortQueueBeforeExecute   = true;
}

// ---------------------------------------------------------------------------
void BuildExecutionQueues(RFG::ViewPassData& view, const DebugAppendFn& debugFn)
{
    view.execute.shadowQueue = view.shadowQueue;

    // Pre-split: Planning-Layer entscheidet Opaque vs. Alpha.
    // Backend empfängt fertige, getrennte Queues — kein Re-Split im Backend.
    view.execute.opaqueQueue = view.opaqueQueue;
    view.execute.alphaQueue  = view.transparentQueue;

    const RFG::PassExec& passExec = view.execute.graphicsPass;

    // Frustum-Zeichnung nutzt die ECHTE Kamera-Matrix (realCameraFrame),
    // nicht die möglicherweise überschriebene Debug-Kamera-Matrix.
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

// ---------------------------------------------------------------------------
bool PrepareMainViewPostProcess(RFG::ViewPassData& view, const PostProcContext& ppCtx)
{
    if (!ppCtx.backend || !ppCtx.postProcessStore || !ppCtx.passOrder ||
        !ppCtx.rtStore  || !ppCtx.texStore        || !ppCtx.mainSceneTarget)
        return false;

    // Prüfe ob mindestens ein Pass tatsächlich ready und enabled ist.
    bool hasActivePass = false;
    for (const PostProcessHandle h : *ppCtx.passOrder)
    {
        const PostProcessResource* pass = ppCtx.postProcessStore->Get(h);
        if (pass && pass->ready && pass->enabled) { hasActivePass = true; break; }
    }
    if (!hasActivePass) return false;

    const uint32_t targetWidth  = static_cast<uint32_t>(
        view.prepared.frame.viewportWidth  > 1.0f ? view.prepared.frame.viewportWidth  : 1.0f);
    const uint32_t targetHeight = static_cast<uint32_t>(
        view.prepared.frame.viewportHeight > 1.0f ? view.prepared.frame.viewportHeight : 1.0f);

    RenderTargetHandle& mainTarget = *ppCtx.mainSceneTarget;
    GDXRenderTargetResource* existing = mainTarget.IsValid() ? ppCtx.rtStore->Get(mainTarget) : nullptr;

    const bool needsNew = !existing || existing->width != targetWidth || existing->height != targetHeight;
    if (needsNew)
    {
        if (mainTarget.IsValid())
            ppCtx.backend->DestroyRenderTarget(mainTarget, *ppCtx.rtStore, *ppCtx.texStore);

        mainTarget = ppCtx.backend->CreateRenderTarget(
            *ppCtx.rtStore, *ppCtx.texStore,
            targetWidth, targetHeight,
            L"MainScenePostProcess",
            GDXTextureFormat::RGBA16_FLOAT);
    }

    if (!mainTarget.IsValid()) return false;

    // Exposed texture — aus dem frisch (neu-)erzeugten RT holen.
    TextureHandle sceneTexture = TextureHandle::Invalid();
    if (const GDXRenderTargetResource* rt = ppCtx.rtStore->Get(mainTarget))
        sceneTexture = rt->exposedTexture;

    if (!sceneTexture.IsValid()) return false;

    view.execute.presentation.postProcess.enabled     = true;
    view.execute.presentation.postProcess.sceneTexture = sceneTexture;

    BuildGraphicsPassExecuteInput(
        view,
        RenderPassTargetDesc::Offscreen(
            mainTarget,
            view.prepared.clearDesc,
            view.prepared.frame.viewportWidth,
            view.prepared.frame.viewportHeight,
            L"MainScenePostProcess"),
        true, true);

    return true;
}

// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
void BuildRTTExecuteInputs(
    std::vector<RFG::ViewPassData>&                          views,
    ResourceStore<GDXRenderTargetResource, RenderTargetTag>& rtStore,
    const DebugAppendFn&                                     debugFn)
{
    for (auto& view : views)
    {
        ConfigureCommonExecuteInputs(view, /*presentAfterExecute=*/false);

        const GDXRenderTargetResource* rt = rtStore.Get(view.prepared.graphicsView.renderTarget);
        if (!rt || !rt->ready) continue;

        BuildShadowPassExecuteInput(view);
        BuildGraphicsPassExecuteInput(
            view,
            view.prepared.graphicsTargetDesc,
            true,
            view.prepared.shadowEnabled);
        BuildExecutionQueues(view, debugFn);
    }
}

// ---------------------------------------------------------------------------
void BuildFrameExecuteInputs(
    RFG::PipelineData&                                       pipeline,
    ResourceStore<GDXRenderTargetResource, RenderTargetTag>& rtStore,
    const PostProcContext&                                   ppCtx,
    const DebugAppendFn&                                     debugFn)
{
    BuildRTTExecuteInputs(pipeline.rttViews, rtStore, debugFn);
    BuildMainViewExecuteInputs(pipeline.mainView, ppCtx, debugFn);
}

} // namespace RenderPassBuilder
