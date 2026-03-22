#pragma once

// ---------------------------------------------------------------------------
// GDXRenderFrameGraph — Frame-Graph-Build und Execute.
//
// Extrahiert aus GDXECSRenderer um die God-Class aufzuteilen.
//
// Verantwortlichkeiten:
//   Build   — Ressourcen registrieren, Nodes aufbauen, Topology cachen.
//   Execute — Nodes in topologischer Reihenfolge ausführen.
//
// GDXECSRenderer besitzt eine Instanz und ruft Build/Execute pro Frame auf.
// ---------------------------------------------------------------------------

#include "RenderFramePipeline.h"
#include "GDXRenderTargetResource.h"
#include "Handle.h"
#include "ResourceStore.h"
#include "MeshAssetResource.h"
#include "MaterialResource.h"
#include "GDXShaderResource.h"
#include "GDXTextureResource.h"
#include "PostProcessResource.h"
#include "Debug.h"
#include "Registry.h"

#include <vector>
#include <cstdint>

class IGDXRenderBackend;

class GDXRenderFrameGraph
{
public:
    // Kontext für Build — nur was zum Aufbau der Ressourcen/Nodes nötig ist.
    struct BuildContext
    {
        ResourceStore<GDXRenderTargetResource, RenderTargetTag>* rtStore = nullptr;
        RenderTargetHandle mainScenePostProcessTarget = RenderTargetHandle::Invalid();
    };

    // Kontext für Execute — Backend + alle Stores.
    struct ExecContext
    {
        IGDXRenderBackend* backend = nullptr;

        ResourceStore<MeshAssetResource,       MeshTag>*        meshStore        = nullptr;
        ResourceStore<MaterialResource,         MaterialTag>*    matStore         = nullptr;
        ResourceStore<GDXShaderResource,        ShaderTag>*      shaderStore      = nullptr;
        ResourceStore<GDXTextureResource,       TextureTag>*     texStore         = nullptr;
        ResourceStore<GDXRenderTargetResource,  RenderTargetTag>* rtStore         = nullptr;
        ResourceStore<PostProcessResource,      PostProcessTag>* postProcessStore = nullptr;
        Registry* registry = nullptr;

        const std::vector<PostProcessHandle>* postProcessPassOrder = nullptr;
    };

    // Build: Ressourcen registrieren, Nodes aufbauen, Topology cachen.
    // Muss nach BuildPreparedFrameExecuteInputs und vor Execute aufgerufen werden.
    void Build(RFG::PipelineData& pipeline, const BuildContext& ctx);

    // Execute: Nodes in topologischer Reihenfolge ausführen.
    // Schreibt Stats in pipeline.mainView.stats + rttViews[i].stats.
    void Execute(RFG::PipelineData& pipeline, const ExecContext& ctx);

    // Topology-Cache explizit invalidieren (z.B. nach RTT-View-Änderung von außen).
    void InvalidateCache() { m_cachedTopologyKey = UINT64_MAX; }

private:
    // Topology-Cache — Dependencies/Sort/Validation nur bei Strukturänderung.
    uint64_t              m_cachedTopologyKey = UINT64_MAX;
    std::vector<uint32_t> m_cachedExecutionOrder;

    uint64_t ComputeTopologyKey(const RFG::PipelineData& pipeline) const;

    // Finalize: Dependencies + Execution Order + Validation in einem Zug.
    bool Finalize(RFG::FrameGraph& fg);
    void BuildDependencies(RFG::FrameGraph& fg);
    bool BuildExecutionOrder(RFG::FrameGraph& fg) const;
    bool Validate(RFG::FrameGraph& fg) const;

    // Einzelnen Node ausführen.
    void ExecuteNode(RFG::Node& node, const ExecContext& ctx);

    // Hilfsfunktionen für Dependency-Prüfung.
    static bool HasDependency(const RFG::Node& node, uint32_t dep);
    static void AddDependency(RFG::Node& node, uint32_t dep);
};
