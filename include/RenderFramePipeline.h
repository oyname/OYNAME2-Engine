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
#include "ECS/Registry.h"
#include "GDXTextureResource.h"
#include "Particles/IGDXParticleRenderer.h"
#include "ParticleCommandList.h"

#include <vector>
#include <string>
#include <functional>
#include <cstdint>
#include <cstdint>


struct RenderFeatureViewPlan
{
    bool enableShadowPass = false;
    bool enableOpaquePass = false;
    bool enableTransparentPass = false;
    bool enableDistortionPass = false;
    bool enableParticlePass = false;
    bool enableDepthPass = false;
    bool enableMotionVectorsPass = false;
    bool hasDistortionQueue = false;
    bool hasMotionVectorQueue = false;
    bool hasDepthQueue = false;
};

// ---------------------------------------------------------------------------
// Frame Graph Resource Model
// ---------------------------------------------------------------------------

using FGResourceID = uint32_t;
static constexpr FGResourceID FG_INVALID_RESOURCE = UINT32_MAX;
static constexpr uint32_t FG_INVALID_NODE = UINT32_MAX;

enum class FGResourceLifetime : uint8_t
{
    Imported = 0,
    Transient = 1,
};

enum class FGResourceKind : uint8_t
{
    Unknown      = 0,
    Backbuffer   = 1,
    Texture      = 2,
    RenderTarget = 3,
    Depth        = 4,
    Shadow       = 5,
    History      = 6,
};

const char* FGResourceLifetimeToString(FGResourceLifetime lifetime);
const char* FGResourceKindToString(FGResourceKind kind);
const char* FGResourceFormatToString(GDXTextureFormat format);

enum class FGResourceStateSource : uint8_t
{
    Unknown = 0,
    TransientCommon,
    BackbufferPresent,
    ImportedFirstUse,
    InferredFallback,
};

const char* FGResourceStateSourceToString(FGResourceStateSource source);

enum class FGShadowResourcePolicy : uint8_t
{
    LocalPerView = 0,
    GlobalSharedMainView = 1,
};

const char* FGShadowResourcePolicyToString(FGShadowResourcePolicy policy);

struct FGResourceDesc
{
    FGResourceID       id           = FG_INVALID_RESOURCE;
    TextureHandle      texture      = TextureHandle::Invalid();
    RenderTargetHandle renderTarget = RenderTargetHandle::Invalid();
    std::string        debugName    = "";
    FGResourceLifetime lifetime     = FGResourceLifetime::Imported;
    FGResourceKind     kind         = FGResourceKind::Unknown;
    uint32_t           width        = 0u;
    uint32_t           height       = 0u;
    GDXTextureFormat   format       = GDXTextureFormat::Unknown;
    uint32_t           producerNode = FG_INVALID_NODE;
    uint32_t           firstUseNode = FG_INVALID_NODE;
    uint32_t           lastUseNode  = FG_INVALID_NODE;
    ResourceState      plannedInitialState = ResourceState::Unknown;
    ResourceState      plannedFinalState = ResourceState::Unknown;
    FGResourceStateSource plannedInitialStateSource = FGResourceStateSource::Unknown;
    bool               externalOutput = false; // marks resources that are consumed outside the graph (swapchain, RTT targets, etc.)

    bool IsImported() const { return lifetime == FGResourceLifetime::Imported; }
    bool IsTransient() const { return lifetime == FGResourceLifetime::Transient; }
    bool HasProducer() const { return producerNode != FG_INVALID_NODE; }
    bool HasUses() const { return firstUseNode != FG_INVALID_NODE; }
};

enum class FGResourceAccessMode : uint8_t
{
    Read = 0,
    Write,
    ReadWrite,
};

enum class FGResourceAccessType : uint8_t
{
    // Graphics
    RenderTarget = 0,
    DepthStencil,
    ShaderResource,
    UnorderedAccess,

    // Transfer
    CopySource,
    CopyDest,

    // Presentation
    PresentSource,

    // Persistent frame-to-frame resources (temporal AA, history buffers, etc.)
    History,
};

inline const char* FGResourceAccessTypeToString(FGResourceAccessType t) noexcept
{
    switch (t)
    {
    case FGResourceAccessType::RenderTarget:    return "RenderTarget";
    case FGResourceAccessType::DepthStencil:    return "DepthStencil";
    case FGResourceAccessType::ShaderResource:  return "ShaderResource";
    case FGResourceAccessType::UnorderedAccess: return "UnorderedAccess";
    case FGResourceAccessType::CopySource:      return "CopySource";
    case FGResourceAccessType::CopyDest:        return "CopyDest";
    case FGResourceAccessType::PresentSource:   return "PresentSource";
    case FGResourceAccessType::History:         return "History";
    default:                                    return "<invalid>";
    }
}

struct FGResourceAccessDecl
{
    FGResourceID resource = FG_INVALID_RESOURCE;
    FGResourceAccessType type = FGResourceAccessType::ShaderResource;
    FGResourceAccessMode mode = FGResourceAccessMode::Read;
    ResourceState requiredState = ResourceState::Unknown;

    bool IsRead() const noexcept
    {
        return mode == FGResourceAccessMode::Read || mode == FGResourceAccessMode::ReadWrite;
    }
    bool IsWrite() const noexcept
    {
        return mode == FGResourceAccessMode::Write || mode == FGResourceAccessMode::ReadWrite;
    }
};

struct FGPlannedTransition
{
    FGResourceID  resource = FG_INVALID_RESOURCE;
    ResourceState before = ResourceState::Unknown;
    ResourceState after  = ResourceState::Unknown;
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

struct FrameGraph;

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
    bool          enabled      = false;
    TextureHandle sceneTexture = TextureHandle::Invalid();  // bleibt für FrameGraph-Key-Berechnung
    PostProcessExecutionInputs execInputs{};                // vollständige deklarative Inputs (Stufe B)
    std::vector<PostProcessHandle> orderedPasses{};
    std::vector<PostProcessPassConstantOverride> constantOverrides{};
    RenderTargetHandle outputTarget = RenderTargetHandle::Invalid();
    bool          outputToBackbuffer = true;

    void Reset()
    {
        enabled = false;
        sceneTexture = TextureHandle::Invalid();
        execInputs.Reset();
        orderedPasses.clear();
        constantOverrides.clear();
        outputTarget = RenderTargetHandle::Invalid();
        outputToBackbuffer = true;
    }
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
    PassExec    depthPass{};
    PassExec    opaquePass{};
    PassExec    motionVectorsPass{};
    PassExec    particlePass{};
    ParticleCommandList particleQueue{};
    PassExec    transparentPass{};
    PassExec    distortionPass{};
    RenderQueue shadowQueue{};
    RenderQueue depthQueue{};

    // Pre-split by the planning layer (BuildPreparedExecutionQueues).
    // opaqueQueue  — RenderPass::Opaque commands (depth write on).
    // alphaQueue   — RenderPass::Transparent commands (depth write off, sorted back-to-front).
    // The backend consumes these directly; it must not re-split or re-sort.
    RenderQueue opaqueQueue{};
    RenderQueue alphaQueue{};
    RenderQueue distortionQueue{};
    RenderQueue motionVectorsQueue{};

    PresentExec presentation{};

    void Reset()
    {
        frame = {};
        shadowPass.Reset();
        depthPass.Reset();
        opaquePass.Reset();
        motionVectorsPass.Reset();
        particlePass.Reset();
        particleQueue.Clear();
        transparentPass.Reset();
        distortionPass.Reset();
        shadowQueue.Clear();
        depthQueue.Clear();
        opaqueQueue.Clear(); alphaQueue.Clear(); distortionQueue.Clear(); motionVectorsQueue.Clear();
        presentation.Reset();
    }
};

struct RenderViewQueues
{
    // Gather-/RenderList-Stufe zwischen Scene/ECS und Execute.
    // Diese Queues sind pass-taugliche, bereits klassifizierte Listen.
    RenderQueue depthQueue{};
    RenderQueue opaqueQueue{};
    RenderQueue transparentQueue{};
    RenderQueue distortionQueue{};
    RenderQueue motionVectorQueue{};
    RenderQueue shadowDepthQueue{};
    ParticleCommandList particleQueue{};

    void Clear()
    {
        depthQueue.Clear();
        opaqueQueue.Clear();
        transparentQueue.Clear();
        distortionQueue.Clear();
        motionVectorQueue.Clear();
        shadowDepthQueue.Clear();
        particleQueue.Clear();
    }
};

struct ViewPassData
{
    ViewData    prepared{};
    ExecuteData execute{};
    ViewStats   stats{};
    FGShadowResourcePolicy shadowResourcePolicy = FGShadowResourcePolicy::LocalPerView;
    VisibleSet  graphicsVisibleSet{};
    VisibleSet  shadowVisibleSet{};
    std::vector<RenderGatherSystem::GatherChunkResult> graphicsGatherChunks{};
    std::vector<RenderGatherSystem::GatherChunkResult> shadowGatherChunks{};
    RenderViewQueues renderQueues{};

    // Legacy-Kompatibilität: bestehende Codepfade können noch direkt auf diese
    // Queues zugreifen, die FinalizeQueues aus renderQueues gespiegelt werden.
    RenderQueue opaqueQueue{};
    RenderQueue transparentQueue{};
    RenderQueue shadowQueue{};
    RenderQueue depthQueue{};
    RenderFeatureViewPlan featurePlan{};

    // Echte Kamera-FrameData — unverändert von CaptureFrameSnapshot.
    // Wird von AppendDebugVisibleSet für das Frustum genutzt wenn
    // Debug-Kamera aktiv ist und prepared.frame überschrieben wurde.
    FrameData   realCameraFrame{};

    void Reset()
    {
        prepared.Reset(); execute.Reset(); stats.Reset();
        shadowResourcePolicy = FGShadowResourcePolicy::LocalPerView;
        graphicsVisibleSet = {}; shadowVisibleSet = {};
        graphicsGatherChunks.clear(); shadowGatherChunks.clear();
        renderQueues.Clear();
        opaqueQueue.Clear(); transparentQueue.Clear(); shadowQueue.Clear();
        featurePlan = {};
        realCameraFrame = {};
    }

    // opaqueQueue and transparentQueue are kept separate intentionally.
    // BuildExecutionQueues() copies the gathered lists directly into
    // execute.opaqueQueue / execute.alphaQueue — no merge needed.
};

enum class PassType : uint8_t
{
    Graphics  = 0,
    Compute   = 1,
    Transfer  = 2,
    Present   = 3,
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
    const FrameGraph* frameGraph = nullptr;

    const std::vector<PostProcessHandle>* postProcessPassOrder = nullptr;
    const std::vector<BackendPlannedTransition>* beginTransitions = nullptr;
    const std::vector<BackendPlannedTransition>* endTransitions = nullptr;
};

struct Node
{
    PassType passType = PassType::Graphics;
    std::string debugName{};

    const ExecuteData* executeInput = nullptr;
    ViewStats*         statsOutput  = nullptr;

    uint32_t viewIndex = 0u;
    bool     enabled   = false;
    bool     countedAsRenderTargetView = false;
    bool     updateFrameConstants = true; // false für Presentation-Nodes
    FGShadowResourcePolicy shadowResourcePolicy = FGShadowResourcePolicy::LocalPerView;

    // Execute-Logik direkt im Node — kein switch/PassType mehr in ExecuteNode.
    std::function<void(const ExecContext&, ViewStats*)> executeFn;

    std::vector<FGResourceAccessDecl> accesses{};
    std::vector<uint32_t>     dependencies{};
    std::vector<FGPlannedTransition> beginTransitions{};
    std::vector<FGPlannedTransition> endTransitions{};

    void AddAccess(FGResourceID resource,
                   FGResourceAccessType type,
                   FGResourceAccessMode mode,
                   ResourceState requiredState) noexcept
    {
        accesses.push_back({ resource, type, mode, requiredState });
    }

    // Convenience helpers – keep call sites readable.
    void AddRenderTarget(FGResourceID resource) { AddAccess(resource, FGResourceAccessType::RenderTarget, FGResourceAccessMode::Write, ResourceState::RenderTarget); }
    void AddDepthWrite(FGResourceID resource)   { AddAccess(resource, FGResourceAccessType::DepthStencil, FGResourceAccessMode::Write, ResourceState::DepthWrite); }
    void AddDepthRead(FGResourceID resource)    { AddAccess(resource, FGResourceAccessType::DepthStencil, FGResourceAccessMode::Read,  ResourceState::DepthRead); }
    void AddSRV(FGResourceID resource)          { AddAccess(resource, FGResourceAccessType::ShaderResource, FGResourceAccessMode::Read, ResourceState::ShaderRead); }
    void AddUAV(FGResourceID resource, FGResourceAccessMode mode = FGResourceAccessMode::ReadWrite)
    {
        AddAccess(resource, FGResourceAccessType::UnorderedAccess, mode, ResourceState::UnorderedAccess);
    }
    void AddCopySource(FGResourceID resource)   { AddAccess(resource, FGResourceAccessType::CopySource, FGResourceAccessMode::Read,  ResourceState::CopySource); }
    void AddCopyDest(FGResourceID resource)     { AddAccess(resource, FGResourceAccessType::CopyDest,   FGResourceAccessMode::Write, ResourceState::CopyDest); }
    void AddPresentSource(FGResourceID resource){ AddAccess(resource, FGResourceAccessType::PresentSource, FGResourceAccessMode::Read, ResourceState::Present); }
    void AddHistoryRead(FGResourceID resource)  { AddAccess(resource, FGResourceAccessType::History, FGResourceAccessMode::Read, ResourceState::ShaderRead); }
    void AddHistoryWrite(FGResourceID resource) { AddAccess(resource, FGResourceAccessType::History, FGResourceAccessMode::Write, ResourceState::RenderTarget); }

    void Reset()
    {
        passType = PassType::Graphics;
        debugName.clear();
        executeInput = nullptr; statsOutput = nullptr;
        viewIndex = 0u; enabled = false;
        countedAsRenderTargetView = false; updateFrameConstants = true;
        shadowResourcePolicy = FGShadowResourcePolicy::LocalPerView;
        executeFn = nullptr;
        accesses.clear(); dependencies.clear();
        beginTransitions.clear(); endTransitions.clear();
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

    FGResourceID RegisterImportedResource(
        TextureHandle tex,
        RenderTargetHandle rt,
        const char* name,
        FGResourceKind kind,
        uint32_t width = 0u,
        uint32_t height = 0u,
        GDXTextureFormat format = GDXTextureFormat::Unknown,
        bool externalOutput = false)
    {
        const FGResourceID id = static_cast<FGResourceID>(resources.size());
        FGResourceDesc d{};
        d.id = id;
        d.texture = tex;
        d.renderTarget = rt;
        d.debugName = name ? name : "";
        d.lifetime = FGResourceLifetime::Imported;
        d.kind = kind;
        d.width = width;
        d.height = height;
        d.format = format;
        d.externalOutput = externalOutput;
        resources.push_back(d);
        return id;
    }

    FGResourceID RegisterTransientResource(
        TextureHandle tex,
        RenderTargetHandle rt,
        const char* name,
        FGResourceKind kind,
        uint32_t width = 0u,
        uint32_t height = 0u,
        GDXTextureFormat format = GDXTextureFormat::Unknown)
    {
        const FGResourceID id = static_cast<FGResourceID>(resources.size());
        FGResourceDesc d{};
        d.id = id;
        d.texture = tex;
        d.renderTarget = rt;
        d.debugName = name ? name : "";
        d.lifetime = FGResourceLifetime::Transient;
        d.kind = kind;
        d.width = width;
        d.height = height;
        d.format = format;
        resources.push_back(d);
        return id;
    }

    FGResourceID RegisterGraphOwnedResource(
        TextureHandle tex,
        RenderTargetHandle rt,
        const char* name,
        FGResourceKind kind,
        uint32_t width = 0u,
        uint32_t height = 0u,
        GDXTextureFormat format = GDXTextureFormat::Unknown,
        bool externalOutput = false)
    {
        const FGResourceID id = RegisterImportedResource(tex, rt, name, kind, width, height, format, externalOutput);
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
