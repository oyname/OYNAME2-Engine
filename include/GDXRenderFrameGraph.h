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
#include "Core/Debug.h"
#include "ECS/Registry.h"

#include <vector>
#include <cstdint>

class IGDXRenderBackend;

// RFG::ExecContext is defined in RenderFramePipeline.h

class GDXRenderFrameGraph
{
public:
    struct BuildContext
    {
        ResourceStore<GDXRenderTargetResource, RenderTargetTag>* rtStore = nullptr;
        RenderTargetHandle mainScenePostProcessTarget = RenderTargetHandle::Invalid();
    };

    void Build(RFG::PipelineData& pipeline, const BuildContext& ctx);
    void Execute(RFG::PipelineData& pipeline, const RFG::ExecContext& ctx);
    void InvalidateCache() { m_cachedTopologyKey = UINT64_MAX; }

private:
    uint64_t              m_cachedTopologyKey = UINT64_MAX;
    std::vector<uint32_t> m_cachedExecutionOrder;

    // Schlüssel aus dem tatsächlich gebauten Graph ableiten — nicht aus einer
    // Vorhersage. Dadurch sind Änderungen an node-Anzahl, Ressourcen oder
    // Pass-Bereitschaft automatisch abgedeckt.
    static uint64_t ComputeGraphStructureKey(const RFG::FrameGraph& fg);

    void Finalize(RFG::FrameGraph& fg);
    void BuildDependencies(RFG::FrameGraph& fg);
    void ComputeResourceLifetimes(RFG::FrameGraph& fg) const;
    void PlanResourceStates(RFG::FrameGraph& fg) const;
    void ComputeReachabilityFromSinks(RFG::FrameGraph& fg, std::vector<uint8_t>& liveNodes, std::vector<uint8_t>& liveResources) const;
    void CompactToLiveSubgraph(RFG::FrameGraph& fg, const std::vector<uint8_t>& liveNodes, const std::vector<uint8_t>& liveResources) const;
    bool BuildExecutionOrder(RFG::FrameGraph& fg) const;
    bool Validate(RFG::FrameGraph& fg) const;

    void ExecuteNode(RFG::Node& node, const RFG::ExecContext& ctx);

    static bool HasDependency(const RFG::Node& node, uint32_t dep);
    static void AddDependency(RFG::Node& node, uint32_t dep);
};
