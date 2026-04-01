#include "GDXRenderFrameGraph.h"
#include "IGDXRenderBackend.h"
#include "Core/Debug.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <vector>

// ---------------------------------------------------------------------------
// Hilfsfunktionen
// ---------------------------------------------------------------------------

namespace
{
    struct DepthDebugParams
    {
        float nearPlane = 0.1f;
        float farPlane = 1000.0f;
        uint32_t isOrtho = 0u;
        uint32_t flags = 1u;
    };

    void UpdatePerViewPostProcessConstants(
        const RFG::ExecuteData& exec,
        const std::vector<PostProcessHandle>& passOrder,
        ResourceStore<PostProcessResource, PostProcessTag>& postStore)
    {
        const DepthDebugParams depthParams{
            exec.presentation.postProcess.execInputs.cameraNearPlane,
            exec.presentation.postProcess.execInputs.cameraFarPlane,
            exec.presentation.postProcess.execInputs.cameraIsOrtho,
            exec.presentation.postProcess.execInputs.depthDebugFlags
        };

        for (const PostProcessHandle handle : passOrder)
        {
            PostProcessResource* pass = postStore.Get(handle);
            if (!pass || !pass->ready || !pass->enabled)
                continue;

            if (pass->desc.pixelShaderFile == L"PostProcessDepthDebugPS.hlsl")
            {
                if (pass->constantBufferBytes >= sizeof(DepthDebugParams) &&
                    pass->constantData.size() >= sizeof(DepthDebugParams))
                {
                    std::memcpy(pass->constantData.data(), &depthParams, sizeof(DepthDebugParams));
                    pass->cpuDirty = true;
                }
                continue;
            }

            if (pass->desc.pixelShaderFile == L"PostProcessGTAOPS.hlsl")
            {
                if (pass->constantBufferBytes >= sizeof(GTAOParams) &&
                    pass->constantData.size() >= sizeof(GTAOParams))
                {
                    GTAOParams* params = reinterpret_cast<GTAOParams*>(pass->constantData.data());
                    params->nearPlane = exec.presentation.postProcess.execInputs.cameraNearPlane;
                    params->farPlane = exec.presentation.postProcess.execInputs.cameraFarPlane;
                    params->projScaleX = exec.presentation.postProcess.execInputs.cameraProjScaleX;
                    params->projScaleY = exec.presentation.postProcess.execInputs.cameraProjScaleY;
                    params->cameraIsOrtho = exec.presentation.postProcess.execInputs.cameraIsOrtho;
                    pass->cpuDirty = true;
                }
                continue;
            }

            if (pass->desc.pixelShaderFile == L"PostProcessGTAOBlurPS.hlsl")
            {
                if (pass->constantBufferBytes >= sizeof(GTAOBlurParams) &&
                    pass->constantData.size() >= sizeof(GTAOBlurParams))
                {
                    GTAOBlurParams* params = reinterpret_cast<GTAOBlurParams*>(pass->constantData.data());
                    params->nearPlane = exec.presentation.postProcess.execInputs.cameraNearPlane;
                    params->farPlane = exec.presentation.postProcess.execInputs.cameraFarPlane;
                    params->cameraIsOrtho = exec.presentation.postProcess.execInputs.cameraIsOrtho;
                    pass->cpuDirty = true;
                }
                continue;
            }

            if (pass->desc.pixelShaderFile == L"PostProcessDepthFogPS.hlsl")
            {
                if (pass->constantBufferBytes >= sizeof(FogParams) &&
                    pass->constantData.size() >= sizeof(FogParams))
                {
                    FogParams* params = reinterpret_cast<FogParams*>(pass->constantData.data());
                    params->cameraNearPlane = exec.presentation.postProcess.execInputs.cameraNearPlane;
                    params->cameraFarPlane  = exec.presentation.postProcess.execInputs.cameraFarPlane;
                    params->projScaleX      = exec.presentation.postProcess.execInputs.cameraProjScaleX;
                    params->projScaleY      = exec.presentation.postProcess.execInputs.cameraProjScaleY;
                    params->cameraIsOrtho   = exec.presentation.postProcess.execInputs.cameraIsOrtho;
                    std::memcpy(params->invView, &exec.presentation.postProcess.execInputs.invViewMatrix, sizeof(params->invView));
                    pass->cpuDirty = true;
                }
                continue;
            }

            if (pass->desc.pixelShaderFile == L"PostProcessVolumetricFogPS.hlsl")
            {
                if (pass->constantBufferBytes >= sizeof(VolumetricFogParams) &&
                    pass->constantData.size() >= sizeof(VolumetricFogParams))
                {
                    VolumetricFogParams* params = reinterpret_cast<VolumetricFogParams*>(pass->constantData.data());
                    params->cameraNearPlane = exec.presentation.postProcess.execInputs.cameraNearPlane;
                    params->cameraFarPlane  = exec.presentation.postProcess.execInputs.cameraFarPlane;
                    params->projScaleX      = exec.presentation.postProcess.execInputs.cameraProjScaleX;
                    params->projScaleY      = exec.presentation.postProcess.execInputs.cameraProjScaleY;
                    params->cameraIsOrtho   = exec.presentation.postProcess.execInputs.cameraIsOrtho;
                    params->cascadeCount    = exec.presentation.postProcess.execInputs.shadowCascadeCount;

                    params->cameraPos[0] = exec.presentation.postProcess.execInputs.cameraPos.x;
                    params->cameraPos[1] = exec.presentation.postProcess.execInputs.cameraPos.y;
                    params->cameraPos[2] = exec.presentation.postProcess.execInputs.cameraPos.z;
                    params->cameraPos[3] = 0.0f;

                    params->lightDir[0] = exec.presentation.postProcess.execInputs.shadowLightDir.x;
                    params->lightDir[1] = exec.presentation.postProcess.execInputs.shadowLightDir.y;
                    params->lightDir[2] = exec.presentation.postProcess.execInputs.shadowLightDir.z;
                    params->lightDir[3] = 0.0f;

                    std::memcpy(params->invView, &exec.presentation.postProcess.execInputs.invViewMatrix, sizeof(params->invView));
                    for (uint32_t c = 0u; c < 4u; ++c)
                    {
                        std::memcpy(params->cascadeViewProj[c], &exec.presentation.postProcess.execInputs.shadowCascadeViewProj[c], sizeof(params->cascadeViewProj[c]));
                        params->cascadeSplits[c] = exec.presentation.postProcess.execInputs.shadowCascadeSplits[c];
                    }
                    pass->cpuDirty = true;
                }
                continue;
            }
        }
    }
}


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
// ExecuteFn-Factories — eliminieren doppelte Lambda-Körper in Build.
// Capture: nur exec-Pointer, kein Zustand der Graph-Klasse.
// ---------------------------------------------------------------------------

static std::function<void(const RFG::ExecContext&, RFG::ViewStats*)>
MakeShadowExecFn(const RFG::ExecuteData* exec)
{
    return [exec](const RFG::ExecContext& c, RFG::ViewStats* s)
    {
        if (c.backend && exec->shadowPass.enabled)
        {
            c.backend->ExecuteShadowPass(exec->shadowPass.desc, *c.registry,
                exec->shadowQueue, *c.meshStore, *c.matStore, *c.shaderStore, *c.texStore);
            s->shadowPassExecuted = true;
        }
    };
}

static std::function<void(const RFG::ExecContext&, RFG::ViewStats*)>
MakeGraphicsExecFn(const RFG::ExecuteData* exec)
{
    return [exec](const RFG::ExecContext& c, RFG::ViewStats* s)
    {
        if (c.backend && exec->graphicsPass.enabled)
        {
            // Attach pre-split queues onto the pass descriptor.
            // The backend must not re-split these — it executes them as-is.
            BackendRenderPassDesc desc = exec->graphicsPass.desc;
            desc.opaqueList = &exec->opaqueQueue;
            desc.alphaList  = &exec->alphaQueue;

            c.backend->ExecuteRenderPass(desc, *c.registry,
                exec->opaqueQueue, exec->alphaQueue,
                *c.meshStore, *c.matStore, *c.shaderStore, *c.texStore, *c.rtStore);
            s->graphicsPassExecuted = true;
        }
    };
}

// ---------------------------------------------------------------------------
// ComputeGraphStructureKey — FNV-1a über die tatsächlich gebauten Nodes.
//
// Schlüssel wird nach dem Node-Build aus fg.nodes abgeleitet, nicht aus
// einer Vorhersage. Damit sind alle strukturellen Änderungen abgedeckt:
//   - Node-Anzahl
//   - Node-Kind
//   - Ressourcen-IDs (reads/writes)
//   - Anzahl der reads/writes pro Node
//   - RT-Bereitschaft (beeinflusst ob ein Node überhaupt gebaut wird)
//   - postProcess-Validity (sceneTexture.IsValid() bestimmt Node-Existenz)
//   - mainView.graphicsPass.enabled (fehlte im alten Predictive-Key)
//
// UINT64_MAX als Sentinel bleibt unverändert (fg mit 0 Nodes kann diesen
// Wert nicht erzeugen, da der erste mix(0) eine andere Ausgabe produziert).
// ---------------------------------------------------------------------------

uint64_t GDXRenderFrameGraph::ComputeGraphStructureKey(const RFG::FrameGraph& fg)
{
    // FNV-1a 64-bit
    uint64_t h = 14695981039346656037ull;
    auto mix = [&](uint64_t v)
    {
        h ^= v;
        h *= 1099511628211ull;
    };

    mix(static_cast<uint64_t>(fg.nodes.size()));
    for (const RFG::Node& node : fg.nodes)
    {
        mix(static_cast<uint64_t>(node.kind));
        mix(static_cast<uint64_t>(node.reads.size()));
        mix(static_cast<uint64_t>(node.writes.size()));
        for (FGResourceID r : node.reads)  mix(static_cast<uint64_t>(r));
        for (FGResourceID w : node.writes) mix(static_cast<uint64_t>(w));
    }
    return h;
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
    std::vector<FGResourceID> rttSceneIDs(rttCount, FG_INVALID_RESOURCE);
    for (uint32_t i = 0u; i < rttCount; ++i)
    {
        const RFG::ViewPassData& view = pipeline.rttViews[i];
        const GDXRenderTargetResource* rt =
            ctx.rtStore ? ctx.rtStore->Get(view.prepared.graphicsView.renderTarget) : nullptr;
        if (!rt || !rt->ready) continue;
        rttColorIDs[i] = pipeline.frameGraph.RegisterResource(
            TextureHandle::Invalid(), view.prepared.graphicsView.renderTarget, "RTT");

        if (view.execute.presentation.postProcess.enabled &&
            view.execute.presentation.postProcess.sceneTexture.IsValid())
        {
            rttSceneIDs[i] = pipeline.frameGraph.RegisterResource(
                view.execute.presentation.postProcess.sceneTexture,
                view.execute.graphicsPass.desc.target.renderTarget,
                "RTTSceneColor");
        }
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
            node.kind       = RFG::NodeKind::Shadow;
            node.executeInput = &view.execute;
            node.statsOutput  = &view.stats;
            node.viewIndex  = i;
            node.enabled    = true;
            node.countedAsRenderTargetView = true;
            node.writes.push_back(shadowMapID);
            node.executeFn  = MakeShadowExecFn(&view.execute);
            pipeline.frameGraph.nodes.push_back(std::move(node));
        }

        if (view.execute.graphicsPass.enabled)
        {
            RFG::Node node{};
            node.kind       = RFG::NodeKind::Graphics;
            node.executeInput = &view.execute;
            node.statsOutput  = &view.stats;
            node.viewIndex  = i;
            node.enabled    = true;
            node.countedAsRenderTargetView = true;
            if (view.execute.shadowPass.enabled)
                node.reads.push_back(shadowMapID);
            if (view.execute.presentation.postProcess.enabled && rttSceneIDs[i] != FG_INVALID_RESOURCE)
                node.writes.push_back(rttSceneIDs[i]);
            else if (rttColorIDs[i] != FG_INVALID_RESOURCE)
                node.writes.push_back(rttColorIDs[i]);
            node.executeFn  = MakeGraphicsExecFn(&view.execute);
            pipeline.frameGraph.nodes.push_back(std::move(node));
        }

        if (view.execute.presentation.postProcess.enabled &&
            view.execute.presentation.postProcess.sceneTexture.IsValid() &&
            rttSceneIDs[i] != FG_INVALID_RESOURCE &&
            rttColorIDs[i] != FG_INVALID_RESOURCE)
        {
            RFG::Node node{};
            node.kind = RFG::NodeKind::Presentation;
            node.executeInput = &view.execute;
            node.statsOutput = &view.stats;
            node.viewIndex = i;
            node.enabled = true;
            node.countedAsRenderTargetView = true;
            node.updateFrameConstants = false;
            node.reads.push_back(rttSceneIDs[i]);
            node.writes.push_back(rttColorIDs[i]);
            const RFG::ExecuteData* exec = &view.execute;
            node.executeFn = [exec](const RFG::ExecContext& c, RFG::ViewStats* s)
            {
                if (c.backend &&
                    exec->presentation.postProcess.enabled &&
                    exec->presentation.postProcess.sceneTexture.IsValid())
                {
                    assert(c.postProcessPassOrder != nullptr);
                    assert(c.postProcessStore != nullptr);
                    UpdatePerViewPostProcessConstants(*exec, *c.postProcessPassOrder, *c.postProcessStore);
                    const bool ok = c.backend->ExecutePostProcessChain(
                        *c.postProcessPassOrder, *c.postProcessStore, *c.texStore, c.rtStore,
                        exec->presentation.postProcess.execInputs,
                        exec->frame.viewportWidth,
                        exec->frame.viewportHeight,
                        exec->presentation.postProcess.outputTarget,
                        exec->presentation.postProcess.outputToBackbuffer);
                    s->presentationExecuted = ok;
                }
            };
            pipeline.frameGraph.nodes.push_back(std::move(node));
        }
    }

    if (pipeline.mainView.execute.shadowPass.enabled)
    {
        RFG::Node node{};
        node.kind       = RFG::NodeKind::Shadow;
        node.executeInput = &pipeline.mainView.execute;
        node.statsOutput  = &pipeline.mainView.stats;
        node.enabled    = true;
        node.writes.push_back(shadowMapID);
        node.executeFn  = MakeShadowExecFn(&pipeline.mainView.execute);
        pipeline.frameGraph.nodes.push_back(std::move(node));
    }

    if (pipeline.mainView.execute.graphicsPass.enabled)
    {
        RFG::Node node{};
        node.kind       = RFG::NodeKind::Graphics;
        node.executeInput = &pipeline.mainView.execute;
        node.statsOutput  = &pipeline.mainView.stats;
        node.enabled    = true;
        if (pipeline.mainView.execute.shadowPass.enabled)
            node.reads.push_back(shadowMapID);
        for (uint32_t i = 0u; i < rttCount; ++i)
            if (rttColorIDs[i] != FG_INVALID_RESOURCE)
                node.reads.push_back(rttColorIDs[i]);
        node.writes.push_back(hasPostProcess ? mainSceneID : backbufferID);
        node.executeFn  = MakeGraphicsExecFn(&pipeline.mainView.execute);
        pipeline.frameGraph.nodes.push_back(std::move(node));
    }

    if (hasPostProcess)
    {
        RFG::Node node{};
        node.kind       = RFG::NodeKind::Presentation;
        node.executeInput = &pipeline.mainView.execute;
        node.statsOutput  = &pipeline.mainView.stats;
        node.enabled    = true;
        node.updateFrameConstants = false;
        node.reads.push_back(mainSceneID);
        node.writes.push_back(backbufferID);
        const RFG::ExecuteData* exec = &pipeline.mainView.execute;
        node.executeFn = [exec](const RFG::ExecContext& c, RFG::ViewStats* s)
        {
            if (c.backend &&
                exec->presentation.postProcess.enabled &&
                exec->presentation.postProcess.sceneTexture.IsValid())
            {
                // postProcessStore/PassOrder sind immer gesetzt wenn backend gesetzt ist —
                // null hier ist ein Programmierfehler im ExecContext-Build, kein Laufzeitfall.
                assert(c.postProcessPassOrder != nullptr);
                assert(c.postProcessStore     != nullptr);
                UpdatePerViewPostProcessConstants(*exec, *c.postProcessPassOrder, *c.postProcessStore);
                const bool ok = c.backend->ExecutePostProcessChain(
                    *c.postProcessPassOrder, *c.postProcessStore, *c.texStore, c.rtStore,
                    exec->presentation.postProcess.execInputs,
                    exec->frame.viewportWidth,
                    exec->frame.viewportHeight,
                    exec->presentation.postProcess.outputTarget,
                    exec->presentation.postProcess.outputToBackbuffer);
                if (!ok)
                {
                    Debug::LogWarning(GDX_SRC_LOC,
                        L"MainView post-process chain did not execute successfully.");
                }
                s->presentationExecuted = ok;
            }
        };
        pipeline.frameGraph.nodes.push_back(std::move(node));
    }

    // --- Schritt 3: Topology-Cache ---
    // Key wird aus den tatsächlich gebauten Nodes abgeleitet (nach dem Build).
    // Damit sind alle strukturellen Unterschiede erfasst: Node-Anzahl, NodeKind,
    // reads/writes, RT-Bereitschaft, sceneTexture-Validity, graphicsPass.enabled.
    // Ein false-Cache-Hit ist damit ausgeschlossen.
    const uint64_t newKey = ComputeGraphStructureKey(pipeline.frameGraph);
    if (newKey == m_cachedTopologyKey && !m_cachedExecutionOrder.empty())
    {
        // Topology-Struktur identisch — Execution-Order wiederverwenden.
        //
        // WICHTIG: Dependencies MÜSSEN trotzdem neu gebaut werden.
        // Nodes werden jeden Frame als leere Objekte neu erstellt;
        // ohne BuildDependencies() sind node.dependencies leer, und
        // Execute() sieht einen strukturell unvollständigen Graph.
        BuildDependencies(pipeline.frameGraph);
        pipeline.frameGraph.executionOrder = m_cachedExecutionOrder;

        // Validation explizit prüfen statt blind auf true setzen.
        // Validate() ist günstig wenn kein Zyklus-Check nötig ist
        // (executionOrder ist bereits bekannt-gut vom letzten Finalize).
        Validate(pipeline.frameGraph);
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

void GDXRenderFrameGraph::Finalize(RFG::FrameGraph& fg)
{
    BuildDependencies(fg);
    BuildExecutionOrder(fg);
    Validate(fg);
    // Ergebnis liegt in fg.validation.valid — Callsite liest dort.
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
    const uint32_t nodeCount     = static_cast<uint32_t>(fg.nodes.size());
    const uint32_t resourceCount = static_cast<uint32_t>(fg.resources.size());

    auto addError = [&](std::string msg)
    {
        fg.validation.valid = false;
        fg.validation.errors.push_back(std::move(msg));
    };

    // Hilfsmakros — inline-Lambda statt Makro um Shadowing zu vermeiden.
    auto checkResourceID = [&](FGResourceID rid, uint32_t nodeIdx, const char* slot)
    {
        if (rid == FG_INVALID_RESOURCE || rid >= resourceCount)
            addError("FrameGraph " + std::string(slot) + " invalid resource " +
                     std::to_string(rid) + " at node " + std::to_string(nodeIdx));
    };

    for (uint32_t i = 0u; i < nodeCount; ++i)
    {
        const RFG::Node& node = fg.nodes[i];

        // --- executeFn vorhanden ---
        if (node.enabled && !node.executeFn)
            addError("FrameGraph enabled node " + std::to_string(i) + " has no executeFn");

        // --- Dependency-Checks ---
        for (uint32_t dep : node.dependencies)
        {
            if (dep >= nodeCount)
            { addError("FrameGraph dep OOB at node " + std::to_string(i)); continue; }
            if (dep == i)
              addError("FrameGraph self-dep at node " + std::to_string(i));
        }

        // --- Resource-Bounds: reads ---
        {
            for (uint32_t r = 0u; r < static_cast<uint32_t>(node.reads.size()); ++r)
            {
                checkResourceID(node.reads[r], i, "read");
                // Duplikat-Check innerhalb reads
                for (uint32_t r2 = r + 1u; r2 < static_cast<uint32_t>(node.reads.size()); ++r2)
                    if (node.reads[r] == node.reads[r2])
                        addError("FrameGraph duplicate read resource " +
                                 std::to_string(node.reads[r]) + " at node " + std::to_string(i));
            }
        }

        // --- Resource-Bounds: writes ---
        {
            for (uint32_t w = 0u; w < static_cast<uint32_t>(node.writes.size()); ++w)
            {
                checkResourceID(node.writes[w], i, "write");
                // Duplikat-Check innerhalb writes
                for (uint32_t w2 = w + 1u; w2 < static_cast<uint32_t>(node.writes.size()); ++w2)
                    if (node.writes[w] == node.writes[w2])
                        addError("FrameGraph duplicate write resource " +
                                 std::to_string(node.writes[w]) + " at node " + std::to_string(i));
            }
        }

        // --- RAW: jeder Read braucht einen Writer in den Deps ---
        for (FGResourceID rid : node.reads)
        {
            if (rid == FG_INVALID_RESOURCE || rid >= resourceCount) continue; // bereits gemeldet
            bool hasWriter = false;
            for (uint32_t dep : node.dependencies)
            {
                if (dep >= nodeCount) continue;
                for (FGResourceID w : fg.nodes[dep].writes)
                    if (w == rid) { hasWriter = true; break; }
                if (hasWriter) break;
            }
            if (!hasWriter)
                addError("FrameGraph RAW: resource " + std::to_string(rid) +
                         " at node " + std::to_string(i) + " has no writer dep");
        }

        // --- WAW/WAR: Writer muss letzten Accessor als Dep haben ---
        for (FGResourceID rid : node.writes)
        {
            if (rid == FG_INVALID_RESOURCE || rid >= resourceCount) continue;
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
                addError("FrameGraph WAW/WAR: resource " + std::to_string(rid) +
                         " node " + std::to_string(i) + " missing dep on " + std::to_string(lastAccessor));
        }
    }

    // --- Execution-Order vollständig ---
    if (fg.executionOrder.size() != static_cast<size_t>(nodeCount))
    { addError("FrameGraph dependency cycle detected"); return fg.validation.valid; }

    // --- Execution-Order: keine OOB, keine Duplikate, alle enthalten ---
    std::vector<int32_t> pos(nodeCount, -1);
    for (uint32_t p = 0u; p < static_cast<uint32_t>(fg.executionOrder.size()); ++p)
    {
        const uint32_t ni = fg.executionOrder[p];
        if (ni >= nodeCount) { addError("FrameGraph exec OOB at pos " + std::to_string(p)); continue; }
        if (pos[ni] != -1)  { addError("FrameGraph duplicate in executionOrder: node " + std::to_string(ni)); continue; }
        pos[ni] = static_cast<int32_t>(p);
    }
    for (uint32_t i = 0u; i < nodeCount; ++i)
        if (pos[i] == -1) addError("FrameGraph missing node " + std::to_string(i) + " in executionOrder");

    // --- Reihenfolge respektiert Deps ---
    for (uint32_t i = 0u; i < nodeCount; ++i)
    {
        if (pos[i] == -1) continue;
        for (uint32_t dep : fg.nodes[i].dependencies)
        {
            if (dep >= nodeCount || pos[dep] == -1) continue;
            if (pos[dep] >= pos[i])
                addError("FrameGraph order violation: dep " + std::to_string(dep) +
                         " must execute before node " + std::to_string(i));
        }
    }

    return fg.validation.valid;
}

// ---------------------------------------------------------------------------
// Execute
// ---------------------------------------------------------------------------

void GDXRenderFrameGraph::ExecuteNode(RFG::Node& node, const RFG::ExecContext& ctx)
{
    if (!node.enabled || !node.executeFn) return;
    RFG::ViewStats* stats = node.statsOutput;
    if (!stats) return;

    if (node.countedAsRenderTargetView)
        stats->countedAsRenderTargetView = true;

    node.executeFn(ctx, stats);
}

void GDXRenderFrameGraph::Execute(RFG::PipelineData& pipeline, const RFG::ExecContext& ctx)
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
            node.updateFrameConstants &&
            node.executeInput != lastUpdatedExec)
        {
            ctx.backend->UpdateFrameConstants(node.executeInput->frame);
            lastUpdatedExec = node.executeInput;
        }

        ExecuteNode(node, ctx);
        executed[nodeIndex] = 1u;
    }
}
