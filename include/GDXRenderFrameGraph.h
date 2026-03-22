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

    uint64_t ComputeTopologyKey(const RFG::PipelineData& pipeline) const;

    bool Finalize(RFG::FrameGraph& fg);
    void BuildDependencies(RFG::FrameGraph& fg);
    bool BuildExecutionOrder(RFG::FrameGraph& fg) const;
    bool Validate(RFG::FrameGraph& fg) const;

    void ExecuteNode(RFG::Node& node, const RFG::ExecContext& ctx);

    static bool HasDependency(const RFG::Node& node, uint32_t dep);
    static void AddDependency(RFG::Node& node, uint32_t dep);
};
