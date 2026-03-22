#pragma once

// ---------------------------------------------------------------------------
// CullGatherSystem — Visibility Cull + Render Gather + Queue Finalize
//
// Verantwortlichkeit: Sichtbare Objekte bestimmen, RenderCommands erzeugen,
// Queues mergen und sortieren.
//
// Alle Operationen sind stateless free functions. Der Context bündelt
// Registry, Stores, JobSystem-Zeiger und die ShaderResolver-Funktion.
//
// GDXECSRenderer baut den Context und übergibt ihn — kein Renderer-Zustand
// hängt an diesen Funktionen.
// ---------------------------------------------------------------------------

#include "RenderFramePipeline.h"
#include "ViewCullingSystem.h"
#include "RenderGatherSystem.h"
#include "ResourceStore.h"
#include "MeshAssetResource.h"
#include "MaterialResource.h"
#include "GDXShaderResource.h"
#include "GDXRenderTargetResource.h"
#include "ECS/Registry.h"
#include "Core/JobSystem.h"

#include <vector>

namespace CullGather
{
    struct Context
    {
        Registry*                                                registry    = nullptr;
        ViewCullingSystem*                                       culling     = nullptr;
        RenderGatherSystem*                                      gather      = nullptr;
        JobSystem*                                               jobSystem   = nullptr;
        ResourceStore<MeshAssetResource,   MeshTag>*             meshStore   = nullptr;
        ResourceStore<MaterialResource,    MaterialTag>*          matStore    = nullptr;
        ResourceStore<GDXShaderResource,   ShaderTag>*            shaderStore = nullptr;
        ResourceStore<GDXRenderTargetResource, RenderTargetTag>* rtStore     = nullptr;
        RenderGatherSystem::ShaderResolver                       resolveShader;
    };

    // ---------------------------------------------------------------------------
    // Einzelne View-Operationen (Main und RTT nutzen dieselben Funktionen)
    // js = nullptr  → seriell (RTT inner loop, kein nested ParallelFor)
    // js = jobSystem → parallel (Main View)
    // ---------------------------------------------------------------------------
    void CullGraphics(const Context& ctx, RFG::ViewPassData& view, JobSystem* js);
    void CullShadow  (const Context& ctx, RFG::ViewPassData& view, JobSystem* js);
    void GatherGraphics(const Context& ctx, RFG::ViewPassData& view, JobSystem* js);
    void GatherShadow  (const Context& ctx, RFG::ViewPassData& view, JobSystem* js);

    // Merged chunk-Ergebnisse → opaqueQueue / transparentQueue / shadowQueue,
    // danach Sortierung.
    void FinalizeQueues(const Context& ctx, RFG::ViewPassData& view);

    // ---------------------------------------------------------------------------
    // Frame-Level-Einstiegspunkte
    // ---------------------------------------------------------------------------

    // Main View: parallel-cull + gather (nutzt ctx.jobSystem intern).
    void CullGatherMainView(const Context& ctx, RFG::ViewPassData& view);

    // RTT Views: über alle Views iterieren, pro View seriell cull+gather
    // (outer ParallelFor auf ctx.jobSystem, kein nested).
    void CullGatherRTTViews(const Context& ctx, std::vector<RFG::ViewPassData>& views);

    // Finalize für alle Views (RTT parallel + Main direkt).
    void FinalizeFrameQueues(const Context& ctx, RFG::PipelineData& pipeline);

} // namespace CullGather
