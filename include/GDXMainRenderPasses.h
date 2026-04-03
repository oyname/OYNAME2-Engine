#pragma once

#include "RenderPassBuilder.h"

namespace GDXMainRenderPasses
{
    struct PassBuildState
    {
        bool postProcessPrepared = false;
    };

    struct PassBuildContext
    {
        const RenderPassBuilder::PostProcContext* postProcContext = nullptr;
        const RenderPassBuilder::DebugAppendFn*   debugAppendFn = nullptr;
        PassBuildState*                           state = nullptr;
        bool presentAfterExecute = false;
        bool enableParticles = false;
        bool isMainView = false;
    };

    struct CommonViewPass
    {
        static void Setup(RFG::ViewPassData& view, const PassBuildContext& ctx);
        static void RegisterResources(RFG::ViewPassData& view, const PassBuildContext& ctx);
        static void Compile(RFG::ViewPassData& view, const PassBuildContext& ctx);
        static void Execute(RFG::ViewPassData& view, const PassBuildContext& ctx);
    };

    struct ShadowPass
    {
        static void Setup(RFG::ViewPassData& view, const PassBuildContext& ctx);
        static void RegisterResources(RFG::ViewPassData& view, const PassBuildContext& ctx);
        static void Compile(RFG::ViewPassData& view, const PassBuildContext& ctx);
        static void Execute(RFG::ViewPassData& view, const PassBuildContext& ctx);
    };

    struct MainSceneOpaquePass
    {
        static void Setup(RFG::ViewPassData& view, const PassBuildContext& ctx);
        static void RegisterResources(RFG::ViewPassData& view, const PassBuildContext& ctx);
        static void Compile(RFG::ViewPassData& view, const PassBuildContext& ctx);
        static void Execute(RFG::ViewPassData& view, const PassBuildContext& ctx);
    };

    struct ParticlePass
    {
        static void Setup(RFG::ViewPassData& view, const PassBuildContext& ctx);
        static void RegisterResources(RFG::ViewPassData& view, const PassBuildContext& ctx);
        static void Compile(RFG::ViewPassData& view, const PassBuildContext& ctx);
        static void Execute(RFG::ViewPassData& view, const PassBuildContext& ctx);
    };

    struct ForwardTransparentPass
    {
        static void Setup(RFG::ViewPassData& view, const PassBuildContext& ctx);
        static void RegisterResources(RFG::ViewPassData& view, const PassBuildContext& ctx);
        static void Compile(RFG::ViewPassData& view, const PassBuildContext& ctx);
        static void Execute(RFG::ViewPassData& view, const PassBuildContext& ctx);
    };

    struct PostProcessPresentationPass
    {
        static void Setup(RFG::ViewPassData& view, const PassBuildContext& ctx);
        static void RegisterResources(RFG::ViewPassData& view, const PassBuildContext& ctx);
        static void Compile(RFG::ViewPassData& view, const PassBuildContext& ctx);
        static void Execute(RFG::ViewPassData& view, const PassBuildContext& ctx);
    };

    struct QueueFinalizePass
    {
        static void Setup(RFG::ViewPassData& view, const PassBuildContext& ctx);
        static void RegisterResources(RFG::ViewPassData& view, const PassBuildContext& ctx);
        static void Compile(RFG::ViewPassData& view, const PassBuildContext& ctx);
        static void Execute(RFG::ViewPassData& view, const PassBuildContext& ctx);
    };

    void BuildMainViewPasses(RFG::ViewPassData& view, const PassBuildContext& ctx);
    void BuildRTTViewPasses(RFG::ViewPassData& view, const PassBuildContext& ctx);
}
