#include "RenderPassBuilder.h"
#include "GDXMainRenderPasses.h"


namespace RenderPassBuilder
{

void BuildMainViewExecuteInputs(
    RFG::ViewPassData&     view,
    const PostProcContext& ppCtx,
    const DebugAppendFn&   debugFn,
    bool                   enableParticles)
{
    GDXMainRenderPasses::PassBuildState state{};
    const GDXMainRenderPasses::PassBuildContext ctx{
        &ppCtx,
        &debugFn,
        &state,
        true,
        enableParticles,
        true
    };
    GDXMainRenderPasses::BuildMainViewPasses(view, ctx);
}

void BuildRTTExecuteInputs(
    std::vector<RFG::ViewPassData>&                           views,
    ResourceStore<GDXRenderTargetResource, RenderTargetTag>& rtStore,
    const PostProcContext&                                    ppCtx,
    const DebugAppendFn&                                      debugFn,
    bool                                                      enableParticles)
{
    (void)rtStore;
    for (auto& view : views)
    {
        const bool requestParticles = enableParticles && !view.renderQueues.particleQueue.Empty();

        GDXMainRenderPasses::PassBuildState state{};
        const GDXMainRenderPasses::PassBuildContext ctx{
            &ppCtx,
            &debugFn,
            &state,
            false,
            requestParticles,
            false
        };
        GDXMainRenderPasses::BuildRTTViewPasses(view, ctx);
    }
}

void BuildFrameExecuteInputs(
    RFG::PipelineData&                                       pipeline,
    ResourceStore<GDXRenderTargetResource, RenderTargetTag>& rtStore,
    const PostProcContext&                                   ppCtx,
    const DebugAppendFn&                                     debugFn,
    bool                                                     enableParticlesInMainView)
{
    BuildRTTExecuteInputs(pipeline.rttViews, rtStore, ppCtx, debugFn, enableParticlesInMainView);
    BuildMainViewExecuteInputs(pipeline.mainView, ppCtx, debugFn, enableParticlesInMainView);
}

} // namespace RenderPassBuilder
