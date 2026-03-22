#pragma once

#include "RenderViewData.h"
#include "RenderQueue.h"
#include "RenderGatherSystem.h"
#include "RenderPassTargetDesc.h"
#include "BackendRenderPassDesc.h"
#include "ResourceStore.h"
#include "MeshAssetResource.h"
#include "MaterialResource.h"
#include "GDXShaderResource.h"
#include "GDXTextureResource.h"
#include "GDXRenderTargetResource.h"
#include "PostProcessResource.h"
#include "Registry.h"

#include <vector>
#include <string>
#include <functional>
#include <cstdint>
#include <cstdint>

// ---------------------------------------------------------------------------
// Frame Graph Resource Model
// ---------------------------------------------------------------------------

using FGResourceID = uint32_t;
static constexpr FGResourceID FG_INVALID_RESOURCE = UINT32_MAX;

struct FGResourceDesc
{
    FGResourceID       id           = FG_INVALID_RESOURCE;
    TextureHandle      texture      = TextureHandle::Invalid();
    RenderTargetHandle renderTarget = RenderTargetHandle::Invalid();
    const char*        debugName    = "";
};

// ---------------------------------------------------------------------------
// namespace RFG  (Render Frame Graph)
//
// Fruhere Namen              Neue Namen
//   ViewExecutionStats       → RFG::ViewStats
//   PreparedViewData         → RFG::ViewData
//   PreparedPassExecution    → RFG::PassExec
//   PreparedPostProcessExec  → RFG::PostProcExec
//   PreparedPresentationExec → RFG::PresentExec
//   PreparedExecuteData      → RFG::ExecuteData
//   ViewPassExecutionData    → RFG::ViewPassData
//   PreparedFrameGraphNodeKind→ RFG::NodeKind
//   PreparedFrameGraphNode   → RFG::Node
//   PreparedFrameGraphValid. → RFG::Validation
//   PreparedFrameGraph       → RFG::FrameGraph
//   RendererFramePipelineData→ RFG::PipelineData
// ---------------------------------------------------------------------------
class IGDXRenderBackend;  // forward — vollständige Definition in IGDXRenderBackend.h

namespace RFG
{

struct ViewStats
{
    ViewCullingStats graphicsCulling{};
    ViewCullingStats shadowCulling{};
    uint32_t drawCalls = 0u;
    uint32_t renderCommands = 0u;
    uint32_t lightCount = 0u;
    uint32_t debugBoundsDraws = 0u;
    uint32_t debugFrustumDraws = 0u;
    bool graphicsPassExecuted = false;
    bool shadowPassExecuted = false;
    bool presentationExecuted = false;
    bool countedAsRenderTargetView = false;

    void Reset()
    {
        graphicsCulling = {};
        shadowCulling = {};
        drawCalls = renderCommands = lightCount = 0u;
        debugBoundsDraws = debugFrustumDraws = 0u;
        graphicsPassExecuted = shadowPassExecuted = false;
        presentationExecuted = countedAsRenderTargetView = false;
    }
};

struct ViewData
{
    RenderViewData      graphicsView{};
    RenderViewData      shadowView{};
    RenderGatherOptions gatherOptions{};
    FrameData           frame{};
    bool                shadowEnabled = false;
    RenderPassClearDesc clearDesc{};
    RenderPassTargetDesc graphicsTargetDesc{};

    void Reset()
    {
        graphicsView = {}; shadowView = {}; gatherOptions = {}; frame = {};
        shadowEnabled = false; clearDesc = {}; graphicsTargetDesc = {};
    }
};

struct PassExec
{
    bool enabled = false;
    BackendRenderPassDesc desc{};
    bool appendGraphicsVisibleSet = false;
    bool appendShadowVisibleSet   = false;
    bool sortQueueBeforeExecute   = false;

    void Reset()
    {
        enabled = false; desc = {};
        appendGraphicsVisibleSet = appendShadowVisibleSet = sortQueueBeforeExecute = false;
    }
};

struct PostProcExec
{
    bool          enabled     = false;
    TextureHandle sceneTexture = TextureHandle::Invalid();

    void Reset() { enabled = false; sceneTexture = TextureHandle::Invalid(); }
};

struct PresentExec
{
    bool        presentAfterExecute = false;
    PostProcExec postProcess{};

    void Reset() { presentAfterExecute = false; postProcess.Reset(); }
};

struct ExecuteData
{
    // Eingefrorener Snapshot aus Prepare — Execute liest nur hier.
    FrameData frame{};

    PassExec    shadowPass{};
    PassExec    graphicsPass{};
    RenderQueue shadowQueue{};
    RenderQueue graphicsQueue{};
    PresentExec presentation{};

    void Reset()
    {
        frame = {};
        shadowPass.Reset(); graphicsPass.Reset();
        shadowQueue.Clear(); graphicsQueue.Clear();
        presentation.Reset();
    }
};

struct ViewPassData
{
    ViewData    prepared{};
    ExecuteData execute{};
    ViewStats   stats{};
    VisibleSet  graphicsVisibleSet{};
    VisibleSet  shadowVisibleSet{};
    std::vector<RenderGatherSystem::GatherChunkResult> graphicsGatherChunks{};
    std::vector<RenderGatherSystem::GatherChunkResult> shadowGatherChunks{};
    RenderQueue opaqueQueue{};
    RenderQueue transparentQueue{};
    RenderQueue shadowQueue{};

    // Echte Kamera-FrameData — unverändert von CaptureFrameSnapshot.
    // Wird von AppendDebugVisibleSet für das Frustum genutzt wenn
    // Debug-Kamera aktiv ist und prepared.frame überschrieben wurde.
    FrameData   realCameraFrame{};

    void Reset()
    {
        prepared.Reset(); execute.Reset(); stats.Reset();
        graphicsVisibleSet = {}; shadowVisibleSet = {};
        graphicsGatherChunks.clear(); shadowGatherChunks.clear();
        opaqueQueue.Clear(); transparentQueue.Clear(); shadowQueue.Clear();
        realCameraFrame = {};
    }

    RenderQueue BuildGraphicsQueue() const
    {
        RenderQueue q;
        q.commands = opaqueQueue.commands;
        q.commands.insert(q.commands.end(),
            transparentQueue.commands.begin(), transparentQueue.commands.end());
        return q;
    }
};

enum class NodeKind : uint8_t
{
    Shadow       = 0,   // ShadowPass
    Graphics     = 1,   // GraphicsPass
    Presentation = 2    // PostProcess → Backbuffer
};

// ExecContext — vollständig hier definiert damit Node::executeFn instantiiert werden kann.
// GDXRenderFrameGraph.h includet RenderFramePipeline.h, nicht umgekehrt.
//
// Invariante: Wenn backend != nullptr, müssen alle Store-Zeiger != nullptr sein.
// postProcessPassOrder/postProcessStore dürfen nur dann nullptr sein, wenn
// garantiert kein Presentation-Node mit postProcess.enabled=true im Graph ist.
struct ExecContext
{
    IGDXRenderBackend* backend = nullptr;

    ResourceStore<MeshAssetResource,      MeshTag>*         meshStore         = nullptr;
    ResourceStore<MaterialResource,        MaterialTag>*     matStore          = nullptr;
    ResourceStore<GDXShaderResource,       ShaderTag>*       shaderStore       = nullptr;
    ResourceStore<GDXTextureResource,      TextureTag>*      texStore          = nullptr;
    ResourceStore<GDXRenderTargetResource, RenderTargetTag>* rtStore           = nullptr;
    ResourceStore<PostProcessResource,     PostProcessTag>*  postProcessStore  = nullptr;
    Registry* registry = nullptr;

    const std::vector<PostProcessHandle>* postProcessPassOrder = nullptr;
};

struct Node
{
    NodeKind kind = NodeKind::Graphics;

    const ExecuteData* executeInput = nullptr;
    ViewStats*         statsOutput  = nullptr;

    uint32_t viewIndex = 0u;
    bool     enabled   = false;
    bool     countedAsRenderTargetView = false;
    bool     updateFrameConstants = true; // false für Presentation-Nodes

    // Execute-Logik direkt im Node — kein switch/NodeKind mehr in ExecuteNode.
    std::function<void(const ExecContext&, ViewStats*)> executeFn;

    std::vector<FGResourceID> reads{};
    std::vector<FGResourceID> writes{};
    std::vector<uint32_t>     dependencies{};

    void Reset()
    {
        kind = NodeKind::Graphics;
        executeInput = nullptr; statsOutput = nullptr;
        viewIndex = 0u; enabled = false;
        countedAsRenderTargetView = false; updateFrameConstants = true;
        executeFn = nullptr;
        reads.clear(); writes.clear(); dependencies.clear();
    }
};

struct Validation
{
    bool valid = true;
    std::vector<std::string> errors{};
    void Reset() { valid = true; errors.clear(); }
};

struct FrameGraph
{
    std::vector<FGResourceDesc> resources{};
    std::vector<Node>           nodes{};
    Validation                  validation{};
    std::vector<uint32_t>       executionOrder{};

    FGResourceID RegisterResource(TextureHandle tex, RenderTargetHandle rt, const char* name)
    {
        const FGResourceID id = static_cast<FGResourceID>(resources.size());
        FGResourceDesc d{};
        d.id = id; d.texture = tex; d.renderTarget = rt; d.debugName = name;
        resources.push_back(d);
        return id;
    }

    void Reset() { resources.clear(); nodes.clear(); validation.Reset(); executionOrder.clear(); }
};

struct PipelineData
{
    FrameData              frameSnapshot{};
    ViewPassData           mainView{};
    std::vector<ViewPassData> rttViews{};
    FrameGraph             frameGraph{};

    void Reset()
    {
        frameSnapshot = {};
        mainView.Reset();
        rttViews.clear();
        frameGraph.Reset();
    }
};

} // namespace RFG
