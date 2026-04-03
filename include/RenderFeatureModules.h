#pragma once

#include "RenderFramePipeline.h"
#include "RenderSortKeyDebug.h"

namespace RenderFeatureModules
{
    enum class FeatureModuleKind : uint8_t
    {
        Shadows = 0,
        Transparency = 1,
        Particles = 2,
        Distortion = 3,
        MotionVectors = 4,
        PostProcess = 5,
    };

    struct ViewFeatureContext
    {
        bool requestParticles = false;
    };

    inline RenderPassClearDesc MakeLoadClearDesc()
    {
        RenderPassClearDesc clear{};
        clear.clearColorEnabled = false;
        clear.clearDepthEnabled = false;
        clear.clearStencilEnabled = false;
        return clear;
    }

    struct ShadowFeatureBuilder
    {
        static inline void Plan(const RFG::ViewPassData& view, RenderFeatureViewPlan& outPlan)
        {
            outPlan.enableShadowPass =
                view.prepared.shadowEnabled &&
                !view.renderQueues.shadowDepthQueue.Empty() &&
                (!view.renderQueues.opaqueQueue.Empty() ||
                 !view.renderQueues.transparentQueue.Empty() ||
                 !view.renderQueues.distortionQueue.Empty());
        }

        static inline void BuildPass(RFG::ViewPassData& view)
        {
            view.execute.shadowPass.Reset();
            view.execute.shadowPass.enabled = view.featurePlan.enableShadowPass;
            if (view.execute.shadowPass.enabled)
                view.execute.shadowPass.desc = BackendRenderPassDesc::Shadow(view.execute.frame);
        }
    };

    struct DepthFeatureBuilder
    {
        static inline void Plan(const RFG::ViewPassData& view, RenderFeatureViewPlan& outPlan)
        {
            outPlan.hasDepthQueue = !view.renderQueues.depthQueue.Empty();
            outPlan.enableDepthPass = outPlan.hasDepthQueue;
        }

        static inline void BuildPass(RFG::ViewPassData& view, const RenderPassTargetDesc& targetDesc)
        {
            view.execute.depthPass.Reset();
            view.execute.depthPass.enabled = view.featurePlan.enableDepthPass && !targetDesc.useBackbuffer && targetDesc.renderTarget.IsValid();
            if (!view.execute.depthPass.enabled)
                return;

            RenderPassTargetDesc passTarget = targetDesc;
            passTarget.clear = view.prepared.clearDesc;
            passTarget.clear.clearColorEnabled = false;
            view.execute.depthPass.desc = BackendRenderPassDesc::Graphics(
                passTarget,
                &view.execute.frame,
                RenderPass::Depth,
                false,
                false);
            view.execute.depthPass.sortQueueBeforeExecute = true;
        }

        static inline void FinalizeQueue(RFG::ViewPassData& view)
        {
            view.execute.depthQueue = view.renderQueues.depthQueue;
            if (view.execute.depthPass.sortQueueBeforeExecute)
                view.execute.depthQueue.Sort();

            if (RenderSortKeyDebug::IsEnabled())
                view.execute.depthQueue.DebugDump("Depth", RenderSortKeyDebug::GetOptions().maxCommandsPerQueue);
        }
    };

    struct MotionVectorsFeatureBuilder
    {
        static inline void Plan(const RFG::ViewPassData& view, RenderFeatureViewPlan& outPlan)
        {
            outPlan.hasMotionVectorQueue = !view.renderQueues.motionVectorQueue.Empty();
            outPlan.enableMotionVectorsPass = outPlan.hasMotionVectorQueue;
        }

        static inline void BuildPass(RFG::ViewPassData& view, const RenderPassTargetDesc& targetDesc)
        {
            view.execute.motionVectorsPass.Reset();
            view.execute.motionVectorsPass.enabled = view.featurePlan.enableMotionVectorsPass && !targetDesc.useBackbuffer && targetDesc.renderTarget.IsValid();
            if (!view.execute.motionVectorsPass.enabled)
                return;

            const RenderPassTargetDesc loadTarget =
                RenderPassTargetDesc::Offscreen(
                    targetDesc.renderTarget,
                    MakeLoadClearDesc(),
                    targetDesc.viewportWidth,
                    targetDesc.viewportHeight,
                    targetDesc.debugName);

            view.execute.motionVectorsPass.desc = BackendRenderPassDesc::Graphics(
                loadTarget,
                &view.execute.frame,
                RenderPass::MotionVectors,
                false,
                true);
            view.execute.motionVectorsPass.sortQueueBeforeExecute = true;
        }

        static inline void FinalizeQueue(RFG::ViewPassData& view)
        {
            view.execute.motionVectorsQueue = view.renderQueues.motionVectorQueue;
            if (view.execute.motionVectorsPass.sortQueueBeforeExecute)
                view.execute.motionVectorsQueue.Sort();

            if (RenderSortKeyDebug::IsEnabled())
                view.execute.motionVectorsQueue.DebugDump("MotionVectors", RenderSortKeyDebug::GetOptions().maxCommandsPerQueue);
        }
    };

    struct TransparencyFeatureBuilder
    {
        static inline void Plan(const RFG::ViewPassData& view, RenderFeatureViewPlan& outPlan)
        {
            outPlan.hasDistortionQueue = !view.renderQueues.distortionQueue.Empty();
            outPlan.enableTransparentPass = !view.renderQueues.transparentQueue.Empty();
            outPlan.enableDistortionPass = outPlan.hasDistortionQueue;
        }

        static inline void BuildPasses(RFG::ViewPassData& view, const RenderPassTargetDesc& targetDesc)
        {
            const RenderPassTargetDesc loadTarget =
                RenderPassTargetDesc::Offscreen(
                    targetDesc.renderTarget,
                    MakeLoadClearDesc(),
                    targetDesc.viewportWidth,
                    targetDesc.viewportHeight,
                    targetDesc.debugName);

            view.execute.transparentPass.Reset();
            view.execute.transparentPass.enabled = view.featurePlan.enableTransparentPass;
            if (view.execute.transparentPass.enabled)
            {
                view.execute.transparentPass.desc = BackendRenderPassDesc::Graphics(
                    loadTarget,
                    &view.execute.frame,
                    RenderPass::Transparent,
                    false,
                    false);
                view.execute.transparentPass.sortQueueBeforeExecute = true;
            }

            view.execute.distortionPass.Reset();
            view.execute.distortionPass.enabled = view.featurePlan.enableDistortionPass;
            if (view.execute.distortionPass.enabled)
            {
                view.execute.distortionPass.desc = BackendRenderPassDesc::Graphics(
                    loadTarget,
                    &view.execute.frame,
                    RenderPass::Distortion,
                    false,
                    false);
                view.execute.distortionPass.sortQueueBeforeExecute = true;
            }
        }

        static inline void FinalizeQueues(RFG::ViewPassData& view)
        {
            view.execute.alphaQueue = view.renderQueues.transparentQueue;
            view.execute.distortionQueue = view.renderQueues.distortionQueue;

            if (view.execute.transparentPass.sortQueueBeforeExecute)
                view.execute.alphaQueue.Sort();
            if (view.execute.distortionPass.sortQueueBeforeExecute)
                view.execute.distortionQueue.Sort();

            if (RenderSortKeyDebug::IsEnabled())
            {
                const size_t maxCommands = RenderSortKeyDebug::GetOptions().maxCommandsPerQueue;
                view.execute.alphaQueue.DebugDump("Transparent", maxCommands);
                view.execute.distortionQueue.DebugDump("Distortion", maxCommands);
            }
        }
    };

    struct ParticleFeatureBuilder
    {
        static inline void Plan(const RFG::ViewPassData& view, const ViewFeatureContext& ctx, RenderFeatureViewPlan& outPlan)
        {
            outPlan.enableParticlePass =
                ctx.requestParticles &&
                HasDrawPass(view.prepared.graphicsView.viewPassMask, DrawPassType::Particles) &&
                !view.renderQueues.particleQueue.Empty();
        }

        static inline void BuildPass(RFG::ViewPassData& view, const RenderPassTargetDesc& targetDesc)
        {
            view.execute.particlePass.Reset();
            view.execute.particlePass.enabled = view.featurePlan.enableParticlePass;
            if (!view.execute.particlePass.enabled)
                return;

            const RenderPassTargetDesc loadTarget =
                RenderPassTargetDesc::Offscreen(
                    targetDesc.renderTarget,
                    MakeLoadClearDesc(),
                    targetDesc.viewportWidth,
                    targetDesc.viewportHeight,
                    targetDesc.debugName);

            view.execute.particlePass.desc = BackendRenderPassDesc::Graphics(
                loadTarget,
                &view.execute.frame,
                RenderPass::ParticlesTransparent,
                false,
                false);
        }
    };

    inline void BuildViewFeaturePlan(
        const RFG::ViewPassData& view,
        const ViewFeatureContext& ctx,
        RenderFeatureViewPlan& outPlan)
    {
        outPlan = {};
        outPlan.enableOpaquePass = !view.renderQueues.opaqueQueue.Empty();
        ShadowFeatureBuilder::Plan(view, outPlan);
        DepthFeatureBuilder::Plan(view, outPlan);
        MotionVectorsFeatureBuilder::Plan(view, outPlan);
        TransparencyFeatureBuilder::Plan(view, outPlan);
        ParticleFeatureBuilder::Plan(view, ctx, outPlan);
    }
}
