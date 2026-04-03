#include "ParticleCommandList.h"
#include "CullGatherSystem.h"
#include <algorithm>
namespace CullGather
{

// ---------------------------------------------------------------------------
// Single-view primitives
// ---------------------------------------------------------------------------

void CullGraphics(const Context& ctx, RFG::ViewPassData& view, JobSystem* js)
{
    view.graphicsVisibleSet = {};
    ctx.culling->BuildVisibleSet(
        *ctx.registry, view.prepared.graphicsView, view.graphicsVisibleSet, js);
    view.stats.graphicsCulling = view.graphicsVisibleSet.stats;
}

void CullShadow(const Context& ctx, RFG::ViewPassData& view, JobSystem* js)
{
    view.shadowVisibleSet    = {};
    view.stats.shadowCulling = {};
    if (!view.prepared.shadowEnabled) return;

    // Kein sichtbarer Receiver → Shadow-Pass vollständig überspringen.
    const bool hasReceiver = std::any_of(
        view.graphicsVisibleSet.candidates.begin(),
        view.graphicsVisibleSet.candidates.end(),
        [](const VisibleRenderCandidate& c){ return c.receiveShadows; });
    if (!hasReceiver) return;

    ctx.culling->BuildVisibleSet(
        *ctx.registry, view.prepared.shadowView, view.shadowVisibleSet, js);
    view.stats.shadowCulling = view.shadowVisibleSet.stats;
}

void GatherGraphics(const Context& ctx, RFG::ViewPassData& view, JobSystem* js)
{
    view.graphicsGatherChunks.clear();
    ctx.gather->GatherVisibleSetChunks(
        view.graphicsVisibleSet, view.prepared.frame,
        *ctx.meshStore, *ctx.matStore, *ctx.shaderStore,
        ctx.resolveShader, view.graphicsGatherChunks,
        &view.prepared.gatherOptions, js);
}

void GatherShadow(const Context& ctx, RFG::ViewPassData& view, JobSystem* js)
{
    view.shadowGatherChunks.clear();
    if (!view.prepared.shadowEnabled) return;

    RenderGatherOptions shadowOpts   = view.prepared.gatherOptions;
    shadowOpts.gatherOpaque          = false;
    shadowOpts.gatherTransparent     = false;
    shadowOpts.gatherShadows         = true;

    ctx.gather->GatherShadowVisibleSetChunks(
        view.shadowVisibleSet, view.prepared.frame,
        *ctx.meshStore, *ctx.matStore, *ctx.shaderStore,
        ctx.resolveShader, view.shadowGatherChunks,
        &shadowOpts, js);
}

void FinalizeQueues(const Context& ctx, RFG::ViewPassData& view)
{
    // Partikel werden bereits vor der Queue-Finalisierung als eigene ICommandList vorbereitet.
    // Diese Queue darf hier nicht verlorengehen, sonst bleibt der
    // Partikel-Pass später trotz aktiver Partikel leer.
    const ParticleCommandList preservedParticleQueue = view.renderQueues.particleQueue;

    view.renderQueues.Clear();
    view.renderQueues.particleQueue = preservedParticleQueue;

    // Haupt-Gather liefert jetzt bereits fachlich getrennte Listen.
    ctx.gather->MergeVisibleSetChunks(
        view.graphicsGatherChunks,
        view.renderQueues.depthQueue,
        view.renderQueues.opaqueQueue,
        view.renderQueues.transparentQueue,
        view.renderQueues.distortionQueue,
        view.renderQueues.motionVectorQueue);

    RenderGatherSystem::SortRenderQueue(view.renderQueues.depthQueue);
    RenderGatherSystem::SortRenderQueue(view.renderQueues.opaqueQueue);
    RenderGatherSystem::SortRenderQueue(view.renderQueues.transparentQueue);
    RenderGatherSystem::SortRenderQueue(view.renderQueues.distortionQueue);
    RenderGatherSystem::SortRenderQueue(view.renderQueues.motionVectorQueue);

    if (view.prepared.shadowEnabled)
    {
        ctx.gather->MergeShadowVisibleSetChunks(
            view.shadowGatherChunks,
            view.renderQueues.shadowDepthQueue);
        RenderGatherSystem::SortRenderQueue(view.renderQueues.shadowDepthQueue);
    }
    else
    {
        view.renderQueues.shadowDepthQueue.Clear();
    }

    // Legacy-Spiegelung für bestehende Aufrufer.
    view.opaqueQueue = view.renderQueues.opaqueQueue;
    view.transparentQueue = view.renderQueues.transparentQueue;
    view.shadowQueue = view.renderQueues.shadowDepthQueue;
}

// ---------------------------------------------------------------------------
// Frame-level
// ---------------------------------------------------------------------------

void CullGatherMainView(const Context& ctx, RFG::ViewPassData& view)
{
    // Main View nutzt das Job-System für innere Parallelität.
    CullGraphics(ctx, view, ctx.jobSystem);
    CullShadow  (ctx, view, ctx.jobSystem);
    GatherGraphics(ctx, view, ctx.jobSystem);
    GatherShadow  (ctx, view, ctx.jobSystem);
}

void CullGatherRTTViews(const Context& ctx, std::vector<RFG::ViewPassData>& views)
{
    // Outer-Parallelismus über Views; inner js = nullptr um nested ParallelFor zu vermeiden.
    ctx.jobSystem->ParallelFor(views.size(), [&](size_t begin, size_t end)
    {
        for (size_t vi = begin; vi < end; ++vi)
        {
            RFG::ViewPassData& v = views[vi];
            const GDXRenderTargetResource* rt =
                ctx.rtStore->Get(v.prepared.graphicsView.renderTarget);
            if (!rt || !rt->ready) continue;
            CullGraphics  (ctx, v, nullptr);
            CullShadow    (ctx, v, nullptr);
            GatherGraphics(ctx, v, nullptr);
            GatherShadow  (ctx, v, nullptr);
        }
    }, 1u);
}

void FinalizeFrameQueues(const Context& ctx, RFG::PipelineData& pipeline)
{
    ctx.jobSystem->ParallelFor(pipeline.rttViews.size(), [&](size_t begin, size_t end)
    {
        for (size_t vi = begin; vi < end; ++vi)
        {
            RFG::ViewPassData& v = pipeline.rttViews[vi];
            const GDXRenderTargetResource* rt =
                ctx.rtStore->Get(v.prepared.graphicsView.renderTarget);
            if (!rt || !rt->ready) continue;
            FinalizeQueues(ctx, v);
        }
    }, 1u);

    FinalizeQueues(ctx, pipeline.mainView);
}

} // namespace CullGather
