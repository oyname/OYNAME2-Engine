#include "GDXRenderFrameGraph.h"
#include "IGDXRenderBackend.h"
#include "Debug.h"

#include <algorithm>
#include <vector>

// ---------------------------------------------------------------------------
// Hilfsfunktionen
// ---------------------------------------------------------------------------

bool GDXRenderFrameGraph::HasDependency(const RFG::Node& node, uint32_t dep)
{
    for (uint32_t existing : node.dependencies)
        if (existing == dep) return true;
    return false;
}

void GDXRenderFrameGraph::AddDependency(RFG::Node& node, uint32_t dep)
{
    if (!HasDependency(node, dep))
        node.dependencies.push_back(dep);
}

// ---------------------------------------------------------------------------
// ComputeTopologyKey — uint64_t kodiert die Graphstruktur.
// Ändert sich nur wenn RTT-Views hinzukommen/wegfallen oder Passes en/disabled werden.
// ---------------------------------------------------------------------------

uint64_t GDXRenderFrameGraph::ComputeTopologyKey(const RFG::PipelineData& pipeline) const
{
    // bits  0-5 : RTT-Count (max 28)
    // bits  6-61: 2 Bit pro RTT-View (bit0=shadow, bit1=graphics)
    // bit     62: main shadow enabled
    // bit     63: main postProcess enabled
    uint64_t key = 0;
    const uint32_t rttCount = static_cast<uint32_t>(
        (std::min)(pipeline.rttViews.size(), size_t{ 28u }));
    key |= static_cast<uint64_t>(rttCount);

    for (uint32_t i = 0u; i < rttCount; ++i)
    {
        const auto& v = pipeline.rttViews[i];
        const uint64_t bits = (v.execute.shadowPass.enabled  ? 1ull : 0ull)
                            | (v.execute.graphicsPass.enabled ? 2ull : 0ull);
        key |= bits << (6u + i * 2u);
    }

    if (pipeline.mainView.execute.shadowPass.enabled)
        key |= (1ull << 62u);
    if (pipeline.mainView.execute.presentation.postProcess.enabled)
        key |= (1ull << 63u);

    return key;
}

// ---------------------------------------------------------------------------
// Build — Ressourcen registrieren, Nodes aufbauen, Topology cachen.
// ---------------------------------------------------------------------------

void GDXRenderFrameGraph::Build(RFG::PipelineData& pipeline, const BuildContext& ctx)
{
    pipeline.frameGraph.Reset();

    // --- Schritt 1: Ressourcen registrieren ---
    const FGResourceID shadowMapID  = pipeline.frameGraph.RegisterResource(
        TextureHandle::Invalid(), RenderTargetHandle::Invalid(), "ShadowMap");
    const FGResourceID backbufferID = pipeline.frameGraph.RegisterResource(
        TextureHandle::Invalid(), RenderTargetHandle::Invalid(), "Backbuffer");

    const bool hasPostProcess =
        pipeline.mainView.execute.presentation.postProcess.enabled &&
        pipeline.mainView.execute.presentation.postProcess.sceneTexture.IsValid();

    const FGResourceID mainSceneID = hasPostProcess
        ? pipeline.frameGraph.RegisterResource(
            pipeline.mainView.execute.presentation.postProcess.sceneTexture,
            ctx.mainScenePostProcessTarget, "MainSceneColor")
        : FG_INVALID_RESOURCE;

    const uint32_t rttCount = static_cast<uint32_t>(pipeline.rttViews.size());
    std::vector<FGResourceID> rttColorIDs(rttCount, FG_INVALID_RESOURCE);
    for (uint32_t i = 0u; i < rttCount; ++i)
    {
        const RFG::ViewPassData& view = pipeline.rttViews[i];
        const GDXRenderTargetResource* rt =
            ctx.rtStore ? ctx.rtStore->Get(view.prepared.graphicsView.renderTarget) : nullptr;
        if (!rt || !rt->ready) continue;
        rttColorIDs[i] = pipeline.frameGraph.RegisterResource(
            TextureHandle::Invalid(), view.prepared.graphicsView.renderTarget, "RTT");
    }

    // --- Schritt 2: Nodes bauen ---
    for (uint32_t i = 0u; i < rttCount; ++i)
    {
        RFG::ViewPassData& view = pipeline.rttViews[i];
        const GDXRenderTargetResource* rt =
            ctx.rtStore ? ctx.rtStore->Get(view.prepared.graphicsView.renderTarget) : nullptr;
        if (!rt || !rt->ready) continue;

        if (view.execute.shadowPass.enabled)
        {
            RFG::Node node{};
            node.kind       = RFG::NodeKind::RenderTargetShadow;
            node.executeInput = &view.execute;
            node.statsOutput  = &view.stats;
            node.viewIndex  = i;
            node.enabled    = true;
            node.countedAsRenderTargetView = true;
            node.writes.push_back(shadowMapID);
            pipeline.frameGraph.nodes.push_back(std::move(node));
        }

        if (view.execute.graphicsPass.enabled)
        {
            RFG::Node node{};
            node.kind       = RFG::NodeKind::RenderTargetGraphics;
            node.executeInput = &view.execute;
            node.statsOutput  = &view.stats;
            node.viewIndex  = i;
            node.enabled    = true;
            node.countedAsRenderTargetView = true;
            if (view.execute.shadowPass.enabled)
                node.reads.push_back(shadowMapID);
            if (rttColorIDs[i] != FG_INVALID_RESOURCE)
                node.writes.push_back(rttColorIDs[i]);
            pipeline.frameGraph.nodes.push_back(std::move(node));
        }
    }

    if (pipeline.mainView.execute.shadowPass.enabled)
    {
        RFG::Node node{};
        node.kind       = RFG::NodeKind::MainShadow;
        node.executeInput = &pipeline.mainView.execute;
        node.statsOutput  = &pipeline.mainView.stats;
        node.enabled    = true;
        node.writes.push_back(shadowMapID);
        pipeline.frameGraph.nodes.push_back(std::move(node));
    }

    if (pipeline.mainView.execute.graphicsPass.enabled)
    {
        RFG::Node node{};
        node.kind       = RFG::NodeKind::MainGraphics;
        node.executeInput = &pipeline.mainView.execute;
        node.statsOutput  = &pipeline.mainView.stats;
        node.enabled    = true;
        if (pipeline.mainView.execute.shadowPass.enabled)
            node.reads.push_back(shadowMapID);
        for (uint32_t i = 0u; i < rttCount; ++i)
            if (rttColorIDs[i] != FG_INVALID_RESOURCE)
                node.reads.push_back(rttColorIDs[i]);
        node.writes.push_back(hasPostProcess ? mainSceneID : backbufferID);
        pipeline.frameGraph.nodes.push_back(std::move(node));
    }

    if (hasPostProcess)
    {
        RFG::Node node{};
        node.kind       = RFG::NodeKind::MainPresentation;
        node.executeInput = &pipeline.mainView.execute;
        node.statsOutput  = &pipeline.mainView.stats;
        node.enabled    = true;
        node.reads.push_back(mainSceneID);
        node.writes.push_back(backbufferID);
        pipeline.frameGraph.nodes.push_back(std::move(node));
    }

    // --- Schritt 3: Topology-Cache ---
    const uint64_t newKey = ComputeTopologyKey(pipeline);
    if (newKey == m_cachedTopologyKey && !m_cachedExecutionOrder.empty())
    {
        pipeline.frameGraph.executionOrder = m_cachedExecutionOrder;
        pipeline.frameGraph.validation.valid = true;
    }
    else
    {
        Finalize(pipeline.frameGraph);
        if (pipeline.frameGraph.validation.valid)
        {
            m_cachedTopologyKey    = newKey;
            m_cachedExecutionOrder = pipeline.frameGraph.executionOrder;
        }
        else
        {
            m_cachedTopologyKey = UINT64_MAX;
        }
    }
}

// ---------------------------------------------------------------------------
// Finalize — Dependencies + Order + Validation.
// ---------------------------------------------------------------------------

bool GDXRenderFrameGraph::Finalize(RFG::FrameGraph& fg)
{
    BuildDependencies(fg);
    BuildExecutionOrder(fg);
    return Validate(fg);
}

void GDXRenderFrameGraph::BuildDependencies(RFG::FrameGraph& fg)
{
    // Vollständig resource-getrieben:
    //   RAW: Reader wartet auf letzten Writer.
    //   WAW+WAR: Writer wartet auf letzten Accessor (Read oder Write).
    for (auto& node : fg.nodes)
        node.dependencies.clear();

    const uint32_t nodeCount = static_cast<uint32_t>(fg.nodes.size());

    auto findLastWriter = [&](uint32_t before, FGResourceID rid) -> int
    {
        for (int j = static_cast<int>(before) - 1; j >= 0; --j)
            for (FGResourceID w : fg.nodes[j].writes)
                if (w == rid) return j;
        return -1;
    };

    auto findLastAccessor = [&](uint32_t before, FGResourceID rid) -> int
    {
        for (int j = static_cast<int>(before) - 1; j >= 0; --j)
        {
            const auto& prev = fg.nodes[j];
            for (FGResourceID w : prev.writes) if (w == rid) return j;
            for (FGResourceID r : prev.reads)  if (r == rid) return j;
        }
        return -1;
    };

    for (uint32_t i = 0u; i < nodeCount; ++i)
    {
        RFG::Node& node = fg.nodes[i];
        for (FGResourceID rid : node.reads)
        {
            const int j = findLastWriter(i, rid);
            if (j >= 0) AddDependency(node, static_cast<uint32_t>(j));
        }
        for (FGResourceID rid : node.writes)
        {
            const int j = findLastAccessor(i, rid);
            if (j >= 0) AddDependency(node, static_cast<uint32_t>(j));
        }
    }
}

bool GDXRenderFrameGraph::BuildExecutionOrder(RFG::FrameGraph& fg) const
{
    fg.executionOrder.clear();
    const uint32_t nodeCount = static_cast<uint32_t>(fg.nodes.size());
    std::vector<uint32_t> indegree(nodeCount, 0u);
    std::vector<std::vector<uint32_t>> dependents(nodeCount);

    for (uint32_t i = 0u; i < nodeCount; ++i)
        for (uint32_t dep : fg.nodes[i].dependencies)
            if (dep < nodeCount && dep != i) { ++indegree[i]; dependents[dep].push_back(i); }

    std::vector<uint32_t> ready;
    ready.reserve(nodeCount);
    for (uint32_t i = 0u; i < nodeCount; ++i)
        if (indegree[i] == 0u) ready.push_back(i);

    while (!ready.empty())
    {
        uint32_t bestPos = 0u;
        for (uint32_t k = 1u; k < static_cast<uint32_t>(ready.size()); ++k)
            if (ready[k] < ready[bestPos]) bestPos = k;

        const uint32_t nodeIndex = ready[bestPos];
        ready.erase(ready.begin() + static_cast<std::ptrdiff_t>(bestPos));
        fg.executionOrder.push_back(nodeIndex);

        for (uint32_t dep : dependents[nodeIndex])
            if (indegree[dep] > 0u && --indegree[dep] == 0u)
                ready.push_back(dep);
    }

    return fg.executionOrder.size() == nodeCount;
}

bool GDXRenderFrameGraph::Validate(RFG::FrameGraph& fg) const
{
    fg.validation.Reset();
    const uint32_t nodeCount = static_cast<uint32_t>(fg.nodes.size());

    for (uint32_t i = 0u; i < nodeCount; ++i)
    {
        const RFG::Node& node = fg.nodes[i];

        for (uint32_t dep : node.dependencies)
        {
            if (dep >= nodeCount)
            { fg.validation.valid = false; fg.validation.errors.push_back("FrameGraph dep OOB at node " + std::to_string(i)); continue; }
            if (dep == i)
            { fg.validation.valid = false; fg.validation.errors.push_back("FrameGraph self-dep at node " + std::to_string(i)); }
        }

        // RAW
        for (FGResourceID rid : node.reads)
        {
            bool hasWriter = false;
            for (uint32_t dep : node.dependencies)
            {
                if (dep >= nodeCount) continue;
                for (FGResourceID w : fg.nodes[dep].writes)
                    if (w == rid) { hasWriter = true; break; }
                if (hasWriter) break;
            }
            if (!hasWriter)
            { fg.validation.valid = false; fg.validation.errors.push_back("FrameGraph RAW: resource " + std::to_string(rid) + " at node " + std::to_string(i) + " has no writer dep"); }
        }

        // WAW/WAR — nur letzter Accessor
        for (FGResourceID rid : node.writes)
        {
            int lastAccessor = -1;
            for (int j = static_cast<int>(i) - 1; j >= 0; --j)
            {
                const auto& prev = fg.nodes[j];
                bool hit = false;
                for (FGResourceID w : prev.writes) if (w == rid) { hit = true; break; }
                if (!hit) for (FGResourceID r : prev.reads) if (r == rid) { hit = true; break; }
                if (hit) { lastAccessor = j; break; }
            }
            if (lastAccessor >= 0 && !HasDependency(node, static_cast<uint32_t>(lastAccessor)))
            { fg.validation.valid = false; fg.validation.errors.push_back("FrameGraph WAW/WAR: resource " + std::to_string(rid) + " node " + std::to_string(i) + " missing dep on " + std::to_string(lastAccessor)); }
        }
    }

    if (fg.executionOrder.size() != static_cast<size_t>(nodeCount))
    { fg.validation.valid = false; fg.validation.errors.push_back("FrameGraph dependency cycle"); return fg.validation.valid; }

    std::vector<int32_t> pos(nodeCount, -1);
    for (uint32_t p = 0u; p < static_cast<uint32_t>(fg.executionOrder.size()); ++p)
    {
        const uint32_t ni = fg.executionOrder[p];
        if (ni >= nodeCount) { fg.validation.valid = false; fg.validation.errors.push_back("FrameGraph exec OOB at pos " + std::to_string(p)); continue; }
        if (pos[ni] != -1)  { fg.validation.valid = false; fg.validation.errors.push_back("FrameGraph duplicate node " + std::to_string(ni)); continue; }
        pos[ni] = static_cast<int32_t>(p);
    }
    for (uint32_t i = 0u; i < nodeCount; ++i)
        if (pos[i] == -1) { fg.validation.valid = false; fg.validation.errors.push_back("FrameGraph missing node " + std::to_string(i)); }

    for (uint32_t i = 0u; i < nodeCount; ++i)
    {
        if (pos[i] == -1) continue;
        for (uint32_t dep : fg.nodes[i].dependencies)
        {
            if (dep >= nodeCount || pos[dep] == -1) continue;
            if (pos[dep] >= pos[i])
            { fg.validation.valid = false; fg.validation.errors.push_back("FrameGraph order violation: dep " + std::to_string(dep) + " → node " + std::to_string(i)); }
        }
    }

    return fg.validation.valid;
}

// ---------------------------------------------------------------------------
// Execute
// ---------------------------------------------------------------------------

void GDXRenderFrameGraph::ExecuteNode(RFG::Node& node, const ExecContext& ctx)
{
    if (!node.enabled) return;
    const RFG::ExecuteData* exec = node.executeInput;
    RFG::ViewStats* stats = node.statsOutput;
    if (!exec || !stats) return;

    if (node.countedAsRenderTargetView)
        stats->countedAsRenderTargetView = true;

    switch (node.kind)
    {
    case RFG::NodeKind::RenderTargetShadow:
    case RFG::NodeKind::MainShadow:
        if (ctx.backend && exec->shadowPass.enabled)
        {
            ctx.backend->ExecuteRenderPass(exec->shadowPass.desc, *ctx.registry,
                exec->shadowQueue, *ctx.meshStore, *ctx.matStore, *ctx.shaderStore, *ctx.texStore, ctx.rtStore);
            stats->shadowPassExecuted = true;
        }
        break;

    case RFG::NodeKind::RenderTargetGraphics:
    case RFG::NodeKind::MainGraphics:
        if (ctx.backend && exec->graphicsPass.enabled)
        {
            ctx.backend->ExecuteRenderPass(exec->graphicsPass.desc, *ctx.registry,
                exec->graphicsQueue, *ctx.meshStore, *ctx.matStore, *ctx.shaderStore, *ctx.texStore, ctx.rtStore);
            stats->graphicsPassExecuted = true;
        }
        break;

    case RFG::NodeKind::MainPresentation:
        if (ctx.backend &&
            exec->presentation.postProcess.enabled &&
            exec->presentation.postProcess.sceneTexture.IsValid() &&
            ctx.postProcessPassOrder && ctx.postProcessStore)
        {
            ctx.backend->ExecutePostProcessChain(
                *ctx.postProcessPassOrder, *ctx.postProcessStore, *ctx.texStore,
                exec->presentation.postProcess.sceneTexture,
                exec->frame.viewportWidth,
                exec->frame.viewportHeight);
            stats->presentationExecuted = true;
        }
        break;
    }
}

void GDXRenderFrameGraph::Execute(RFG::PipelineData& pipeline, const ExecContext& ctx)
{
    if (!pipeline.frameGraph.validation.valid)
    {
        for (const auto& err : pipeline.frameGraph.validation.errors)
            DBERROR(GDX_SRC_LOC, err);
        return;
    }

    std::vector<uint8_t> executed(pipeline.frameGraph.nodes.size(), 0u);
    const RFG::ExecuteData* lastUpdatedExec = nullptr;

    for (uint32_t nodeIndex : pipeline.frameGraph.executionOrder)
    {
        if (nodeIndex >= pipeline.frameGraph.nodes.size())
        { DBERROR(GDX_SRC_LOC, "FrameGraph execution index OOB"); return; }

        RFG::Node& node = pipeline.frameGraph.nodes[nodeIndex];

        for (uint32_t dep : node.dependencies)
        {
            if (dep >= executed.size() || executed[dep] == 0u)
            { DBERROR(GDX_SRC_LOC, "FrameGraph dep not ready"); return; }
        }

        if (ctx.backend && node.enabled && node.executeInput &&
            node.kind != RFG::NodeKind::MainPresentation &&
            node.executeInput != lastUpdatedExec)
        {
            ctx.backend->UpdateFrameConstants(node.executeInput->frame);
            lastUpdatedExec = node.executeInput;
        }

        ExecuteNode(node, ctx);
        executed[nodeIndex] = 1u;
    }
}
