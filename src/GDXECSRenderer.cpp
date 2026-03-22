#include "GDXECSRenderer.h"
#include "CameraSystem.h"
#include "GDXRenderTargetResource.h"
#include "Core/Debug.h"
#include "GDXShaderLayout.h"
#include "RenderPassTargetDesc.h"
#include "Core/GDXMath.h"
#include "BasicMeshGenerator.h"
#include "RenderViewPrep.h"
#include "RenderPassBuilder.h"
#include "CullGatherSystem.h"

#include <sstream>

namespace
{
    enum SystemResourceMask : uint64_t
    {
        SR_TRANSFORM     = 1ull << 0,
        SR_FRAME         = 1ull << 1,
        SR_RENDER_QUEUES = 1ull << 2,
        SR_BACKEND       = 1ull << 3,
        SR_STATS         = 1ull << 4,
        SR_MAIN_VIEW     = 1ull << 5,
        SR_RTT_VIEWS     = 1ull << 6,
    };
} // anonymous namespace

// ---------------------------------------------------------------------------
// Context builders
// ---------------------------------------------------------------------------

RenderViewPrep::Context GDXECSRenderer::MakeViewPrepContext() const
{
    RenderViewPrep::Context ctx{};
    ctx.registry                 = const_cast<Registry*>(&m_registry);
    ctx.rtStore                  = const_cast<ResourceStore<GDXRenderTargetResource, RenderTargetTag>*>(&m_rtStore);
    ctx.backend                  = m_backend.get();
    ctx.shadowResourcesAvailable = m_shadowResourcesAvailable;
    return ctx;
}

RenderPassBuilder::PostProcContext GDXECSRenderer::MakePostProcContext()
{
    RenderPassBuilder::PostProcContext ctx{};
    ctx.backend          = m_backend.get();
    ctx.postProcessStore = &m_postProcessStore;
    ctx.passOrder        = &m_postProcessPassOrder;
    ctx.rtStore          = &m_rtStore;
    ctx.texStore         = &m_texStore;
    ctx.mainSceneTarget  = &m_mainScenePostProcessTarget;
    return ctx;
}

CullGather::Context GDXECSRenderer::MakeCullGatherContext(
    const RenderGatherSystem::ShaderResolver& rs)
{
    CullGather::Context ctx{};
    ctx.registry     = &m_registry;
    ctx.culling      = &m_viewCullingSystem;
    ctx.gather       = &m_gatherSystem;
    ctx.jobSystem    = &m_jobSystem;
    ctx.meshStore    = &m_meshStore;
    ctx.matStore     = &m_matStore;
    ctx.shaderStore  = &m_shaderStore;
    ctx.rtStore      = &m_rtStore;
    ctx.resolveShader = rs;
    return ctx;
}

// ---------------------------------------------------------------------------
// FrameGraph build — named method
// ---------------------------------------------------------------------------

void GDXECSRenderer::BuildFrameGraph()
{
    GDXRenderFrameGraph::BuildContext bctx{};
    bctx.rtStore                    = &m_rtStore;
    bctx.mainScenePostProcessTarget = m_mainScenePostProcessTarget;
    m_frameGraph.Build(m_renderPipeline, bctx);
}

// ---------------------------------------------------------------------------
// Queue finalization — thin wrapper around CullGather::FinalizeFrameQueues
// ---------------------------------------------------------------------------

void GDXECSRenderer::FinalizeFrameQueues()
{
    // Dummy resolver — not needed for finalization, but Context requires it.
    // FinalizeQueues only calls merge/sort, not gather.
    CullGather::Context ctx{};
    ctx.gather    = &m_gatherSystem;
    ctx.jobSystem = &m_jobSystem;
    ctx.rtStore   = &m_rtStore;
    CullGather::FinalizeFrameQueues(ctx, m_renderPipeline);
}

// ---------------------------------------------------------------------------
// Debug culling -- owns m_debugCulling, stays as methods
// ---------------------------------------------------------------------------

bool GDXECSRenderer::EnsureDebugCullingResources()
{
    return m_debugCulling.EnsureResources(
        [this](MeshAssetResource mesh) { return UploadMesh(std::move(mesh)); },
        [this](MaterialResource mat)   { return CreateMaterial(std::move(mat)); },
        m_defaultShader);
}

void GDXECSRenderer::AppendDebugVisibleSet(RenderQueue& queue, const VisibleSet& set,
    const RenderViewData& view, RFG::ViewStats* viewStats)
{
    if (!EnsureDebugCullingResources()) return;
    GDXDebugCullingRenderer::RenderContext ctx{};
    ctx.matStore          = &m_matStore;
    ctx.shaderStore       = &m_shaderStore;
    ctx.defaultShader     = m_defaultShader;
    ctx.frameNumber       = m_frameNumber;
    ctx.uploadFrustumMesh = [this](MeshAssetResource mesh) { return UploadMesh(std::move(mesh)); };
    m_debugCulling.AppendVisibleSet(queue, set, view, ctx, viewStats);
}

void GDXECSRenderer::LogDebugCullingStats() const
{
    m_debugCulling.LogStats(m_renderPipeline.mainView.stats,
        [&]() -> std::vector<RFG::ViewStats>
        {
            std::vector<RFG::ViewStats> v;
            v.reserve(m_renderPipeline.rttViews.size());
            for (const auto& rv : m_renderPipeline.rttViews) v.push_back(rv.stats);
            return v;
        }(),
        m_frameNumber);
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

GDXECSRenderer::GDXECSRenderer(std::unique_ptr<IGDXRenderBackend> backend)
    : m_backend(std::move(backend))
{
}

GDXECSRenderer::~GDXECSRenderer()
{
    Shutdown();
}

bool GDXECSRenderer::Initialize()
{
    if (!m_backend) return false;
    if (!m_backend->Initialize(m_texStore)) return false;

    const auto& defaults = m_backend->GetDefaultTextures();
    m_defaultWhiteTex  = defaults.white;
    m_defaultNormalTex = defaults.normal;
    m_defaultORMTex    = defaults.orm;
    m_defaultBlackTex  = defaults.black;

    if (!LoadDefaultShaders()) return false;

    m_shadowResourcesAvailable = m_backend->HasShadowResources();
    m_initialized = true;
    return true;
}

bool GDXECSRenderer::LoadDefaultShaders()
{
    m_shaderCache.Init(m_backend.get(), &m_shaderStore);
    if (!m_shaderCache.LoadDefaults()) return false;
    m_defaultShader = m_shaderCache.DefaultShader();
    m_shadowShader  = m_shaderCache.ShadowShader();
    return true;
}

ShaderHandle GDXECSRenderer::CreateShader(
    const std::wstring& vsFile, const std::wstring& psFile, uint32_t vertexFlags)
{
    return m_shaderCache.LoadShader(vsFile, psFile, vertexFlags);
}

ShaderHandle GDXECSRenderer::CreateShader(
    const std::wstring& vsFile, const std::wstring& psFile,
    uint32_t vertexFlags, const GDXShaderLayout& layout)
{
    return m_shaderCache.LoadShader(vsFile, psFile, vertexFlags, layout);
}

ShaderHandle GDXECSRenderer::LoadShaderInternal(
    const std::wstring& vsFile, const std::wstring& psFile,
    uint32_t vertexFlags, const std::wstring& /*debugName*/,
    const GDXShaderLayout* customLayout)
{
    return m_shaderCache.LoadShader(vsFile, psFile, vertexFlags,
        customLayout ? *customLayout : GDXShaderLayouts::BuildMain(vertexFlags, false));
}

ShaderHandle GDXECSRenderer::ResolveShaderVariant(
    RenderPass pass, const SubmeshData& submesh, const MaterialResource& mat)
{
    return m_shaderCache.Resolve(pass, submesh, mat);
}

void GDXECSRenderer::LoadIBL(const std::wstring& hdrPath)
{
    if (!m_backend) return;
    m_backend->LoadIBL(hdrPath.empty() ? nullptr : hdrPath.c_str());
}

TextureHandle GDXECSRenderer::LoadTexture(const std::wstring& filePath, bool isSRGB)
{
    TextureHandle existing = TextureHandle::Invalid();
    m_texStore.ForEach([&](TextureHandle h, const GDXTextureResource& res)
    {
        if (!existing.IsValid() && res.debugName == filePath)
            existing = h;
    });
    if (existing.IsValid()) return existing;
    if (!m_backend) return m_defaultWhiteTex;
    return m_backend->CreateTexture(m_texStore, filePath, isSRGB, m_defaultWhiteTex);
}

TextureHandle GDXECSRenderer::CreateTexture(
    const ImageBuffer& image, const std::wstring& debugName, bool isSRGB)
{
    if (!m_backend || !image.IsValid()) return m_defaultWhiteTex;
    return m_backend->CreateTextureFromImage(m_texStore, image, isSRGB, debugName, m_defaultWhiteTex);
}

MeshHandle GDXECSRenderer::UploadMesh(MeshAssetResource mesh)
{
    MeshHandle h = m_meshStore.Add(std::move(mesh));
    if (auto* r = m_meshStore.Get(h); r && m_backend)
        m_backend->UploadMesh(*r);
    return h;
}

MeshHandle GDXECSRenderer::UploadMesh(MeshAssetResource mesh, RenderBoundsComponent& outBounds)
{
    outBounds = RenderBoundsComponent::MakeFromSubmeshes(mesh.submeshes);
    return UploadMesh(std::move(mesh));
}

MaterialHandle GDXECSRenderer::CreateMaterial(MaterialResource mat)
{
    if (!mat.HasConsistentTextureState())
        Debug::LogWarning(GDX_SRC_LOC, "CreateMaterial: inkonsistenter textureLayers-Zustand erkannt");
    mat.NormalizeTextureLayers();
    MaterialHandle h = m_matStore.Add(std::move(mat));
    if (auto* r = m_matStore.Get(h))
    {
        r->sortID = h.Index();
        if (m_backend) m_backend->CreateMaterialGpu(*r);
    }
    return h;
}

RenderTargetHandle GDXECSRenderer::CreateRenderTarget(
    uint32_t w, uint32_t h, const std::wstring& name, GDXTextureFormat colorFormat)
{
    if (!m_backend) return RenderTargetHandle::Invalid();
    return m_backend->CreateRenderTarget(m_rtStore, m_texStore, w, h, name, colorFormat);
}

TextureHandle GDXECSRenderer::GetRenderTargetTexture(RenderTargetHandle h)
{
    if (auto* rt = m_rtStore.Get(h)) return rt->exposedTexture;
    return m_defaultWhiteTex;
}

PostProcessHandle GDXECSRenderer::CreatePostProcessPass(const PostProcessPassDesc& desc)
{
    if (!m_backend) return PostProcessHandle::Invalid();
    PostProcessHandle h = m_backend->CreatePostProcessPass(m_postProcessStore, desc);
    if (h.IsValid()) m_postProcessPassOrder.push_back(h);
    return h;
}

bool GDXECSRenderer::SetPostProcessConstants(PostProcessHandle h, const void* data, uint32_t size)
{
    if (!m_backend) return false;
    auto* pass = m_postProcessStore.Get(h);
    if (!pass) return false;
    return m_backend->UpdatePostProcessConstants(*pass, data, size);
}

bool GDXECSRenderer::SetPostProcessEnabled(PostProcessHandle h, bool enabled)
{
    auto* pass = m_postProcessStore.Get(h);
    if (!pass) return false;
    pass->enabled = enabled;
    return true;
}

void GDXECSRenderer::ClearPostProcessPasses()
{
    if (m_backend)
    {
        m_backend->DestroyPostProcessPasses(m_postProcessStore);
    }
    else
    {
        std::vector<PostProcessHandle> stale;
        m_postProcessStore.ForEach([&stale](PostProcessHandle h, PostProcessResource&)
        { stale.push_back(h); });
        for (const PostProcessHandle h : stale)
            m_postProcessStore.Release(h);
    }
    m_postProcessPassOrder.clear();
}

void GDXECSRenderer::SetShadowMapSize(uint32_t size)
{
    if (m_backend) m_backend->SetShadowMapSize(size);
}

bool GDXECSRenderer::SupportsTextureFormat(GDXTextureFormat format) const
{
    return m_backend ? m_backend->SupportsTextureFormat(format) : false;
}

void GDXECSRenderer::SetClearColor(float r, float g, float b, float a)
{
    m_clearColor[0] = r; m_clearColor[1] = g;
    m_clearColor[2] = b; m_clearColor[3] = a;
}

// ---------------------------------------------------------------------------
// Scene Extraction -- Layer 1
// ---------------------------------------------------------------------------

void GDXECSRenderer::CaptureFrameSnapshot(FrameData& outFrame)
{
    m_framePhase = RenderFramePhase::FreezeSnapshot;

    FrameData snapshot{};
    m_persistentFrameState.ApplyTo(snapshot);
    m_cameraSystem.Update(m_registry, snapshot);

    if (m_backend)
        m_backend->ExtractLightData(m_registry, snapshot);
    else
    {
        snapshot.lightCount       = 0u;
        snapshot.hasShadowPass    = false;
        snapshot.shadowCasterMask = 0xFFFFFFFFu;
        snapshot.lightAffectMask  = 0xFFFFFFFFu;
    }

    outFrame    = snapshot;
    m_frameData = snapshot;
}

// ---------------------------------------------------------------------------
// Stats
// ---------------------------------------------------------------------------

void GDXECSRenderer::AggregatePreparedFrameStats(
    const RFG::ViewPassData& mainView,
    const std::vector<RFG::ViewPassData>& rttViews)
{
    m_stats = {};
    m_stats.drawCalls         = mainView.stats.drawCalls;
    m_stats.renderCommands    = mainView.stats.renderCommands;
    m_stats.lightCount        = mainView.stats.lightCount;
    m_stats.mainCulling       = mainView.stats.graphicsCulling;
    m_stats.shadowCulling     = mainView.stats.shadowCulling;
    m_stats.debugBoundsDraws  = mainView.stats.debugBoundsDraws;
    m_stats.debugFrustumDraws = mainView.stats.debugFrustumDraws;

    for (const auto& v : rttViews)
    {
        if (v.stats.countedAsRenderTargetView) ++m_stats.rttViewCount;
        m_stats.debugBoundsDraws  += v.stats.debugBoundsDraws;
        m_stats.debugFrustumDraws += v.stats.debugFrustumDraws;
    }
}

void GDXECSRenderer::UpdatePreparedMainViewFrameTransient(RFG::ViewPassData& preparedView)
{
    auto& frameTransient = m_frameTransients[m_currentFrameIndex];
    constexpr size_t kApproxFrameConstantsBytes  = 272u + (4u * 64u + 32u);
    constexpr size_t kApproxEntityConstantsBytes = 128u;
    (void)frameTransient.uploadArena.Allocate(kApproxFrameConstantsBytes, 16u);
    (void)frameTransient.uploadArena.Allocate(kApproxEntityConstantsBytes *
        (preparedView.opaqueQueue.Count() + preparedView.transparentQueue.Count() +
         preparedView.shadowQueue.Count()), 16u);
}

// ---------------------------------------------------------------------------
// Frame lifecycle
// ---------------------------------------------------------------------------

void GDXECSRenderer::BeginFrame()
{
    m_framePhase = RenderFramePhase::UpdateWrite;
    m_currentFrameIndex = (m_currentFrameIndex + 1u) % GDXMaxFramesInFlight;
    m_persistentFrameState.ApplyTo(m_frameData);
    m_frameContexts[m_currentFrameIndex].Begin(m_currentFrameIndex, ++m_frameNumber, &m_frameData);
    m_frameTransients[m_currentFrameIndex].BeginFrame();
    m_stats = {};
    if (m_backend) m_backend->BeginFrame(m_clearColor);
}

void GDXECSRenderer::Tick(float dt)
{
    if (m_tickCallback) m_tickCallback(dt);
}

void GDXECSRenderer::EndFrame()
{
    auto resolveShader = [this](RenderPass pass, const SubmeshData& submesh, const MaterialResource& mat)
    {
        return ResolveShaderVariant(pass, submesh, mat);
    };

    auto debugAppend = [this](
        RenderQueue& q, const VisibleSet& set,
        const RenderViewData& view, RFG::ViewStats* stats)
    {
        AppendDebugVisibleSet(q, set, view, stats);
    };

    const RenderViewPrep::Context      vpCtx  = MakeViewPrepContext();
    RenderPassBuilder::PostProcContext ppCtx   = MakePostProcContext();
    // CullGather context built here so resolveShader lifetime covers all tasks.
    const CullGather::Context          cgCtx  = MakeCullGatherContext(resolveShader);

    m_renderPipeline.Reset();
    m_systemScheduler.Clear();

    // ---- Transform ----
    m_systemScheduler.AddTask({ "Transform",
        0ull, SR_TRANSFORM,
        [this]() { m_transformSystem.Update(m_registry, &m_jobSystem); } });

    // ---- Scene Extraction ----
    m_systemScheduler.AddTask({ "Capture Frame Snapshot",
        SR_TRANSFORM | SR_FRAME, SR_FRAME | SR_STATS,
        [this]() { CaptureFrameSnapshot(m_renderPipeline.frameSnapshot); } });

    // ---- View Preparation (Layer 1) ----
    m_systemScheduler.AddTask({ "Prepare Main View",
        SR_FRAME | SR_STATS, SR_MAIN_VIEW,
        [this, &vpCtx]()
        {
            RenderViewPrep::PrepareMainView(
                vpCtx, m_renderPipeline.frameSnapshot,
                m_debugCamera, m_renderPipeline.mainView);
        } });

    m_systemScheduler.AddTask({ "Prepare RTT Views",
        SR_FRAME | SR_STATS, SR_RTT_VIEWS,
        [this, &vpCtx]()
        {
            RenderViewPrep::PrepareRTTViews(
                vpCtx, m_renderPipeline.frameSnapshot,
                m_renderPipeline.rttViews);
        } });

    // ---- Cull + Gather (via CullGather free functions) ----
    m_systemScheduler.AddTask({ "Cull+Gather RTT",
        SR_RTT_VIEWS | SR_TRANSFORM, SR_RTT_VIEWS,
        [this, &cgCtx]()
        {
            CullGather::CullGatherRTTViews(cgCtx, m_renderPipeline.rttViews);
        } });

    m_systemScheduler.AddTask({ "Cull+Gather Main",
        SR_MAIN_VIEW | SR_TRANSFORM, SR_MAIN_VIEW,
        [this, &cgCtx]()
        {
            m_framePhase = RenderFramePhase::VisibilityBuild;
            CullGather::CullGatherMainView(cgCtx, m_renderPipeline.mainView);
        } });

    // ---- Queue Finalization ----
    m_systemScheduler.AddTask({ "Finalize Frame Queues",
        SR_MAIN_VIEW | SR_RTT_VIEWS | SR_TRANSFORM, SR_RENDER_QUEUES,
        [this]() { FinalizeFrameQueues(); } });

    // ---- Execute-Input Build (Layer 2) ----
    m_systemScheduler.AddTask({ "Build Frame Execute Inputs",
        SR_RENDER_QUEUES | SR_BACKEND, SR_RENDER_QUEUES,
        [this, &ppCtx, &debugAppend]()
        {
            RenderPassBuilder::BuildFrameExecuteInputs(
                m_renderPipeline, m_rtStore, ppCtx, debugAppend);
        } });

    // ---- Framegraph Build ----
    m_systemScheduler.AddTask({ "Build Frame Graph",
        SR_RENDER_QUEUES | SR_BACKEND, SR_RENDER_QUEUES,
        [this]() { BuildFrameGraph(); } });

    // ---- Backend Execution (Layer 3) ----
    m_systemScheduler.AddTask({ "Execute Frame Graph",
        SR_RENDER_QUEUES | SR_MAIN_VIEW | SR_RTT_VIEWS | SR_TRANSFORM | SR_BACKEND,
        SR_BACKEND | SR_STATS,
        [this]()
        {
            RFG::ExecContext ectx{};
            ectx.backend              = m_backend.get();
            ectx.registry             = &m_registry;
            ectx.meshStore            = &m_meshStore;
            ectx.matStore             = &m_matStore;
            ectx.shaderStore          = &m_shaderStore;
            ectx.texStore             = &m_texStore;
            ectx.rtStore              = &m_rtStore;
            ectx.postProcessStore     = &m_postProcessStore;
            ectx.postProcessPassOrder = &m_postProcessPassOrder;
            m_frameGraph.Execute(m_renderPipeline, ectx);

            m_renderPipeline.mainView.stats.drawCalls = m_backend ? m_backend->GetDrawCallCount() : 0u;
            m_renderPipeline.mainView.stats.renderCommands =
                m_renderPipeline.mainView.execute.graphicsPass.enabled
                ? static_cast<uint32_t>(
                    m_renderPipeline.mainView.execute.opaqueQueue.Count() +
                    m_renderPipeline.mainView.execute.alphaQueue.Count()) : 0u;
            m_renderPipeline.mainView.stats.lightCount =
                m_renderPipeline.mainView.execute.frame.lightCount;

            UpdatePreparedMainViewFrameTransient(m_renderPipeline.mainView);
            AggregatePreparedFrameStats(m_renderPipeline.mainView, m_renderPipeline.rttViews);
            LogDebugCullingStats();

            m_framePhase = RenderFramePhase::ExecuteSubmit;
            if (m_backend && m_renderPipeline.mainView.execute.presentation.presentAfterExecute)
                m_backend->Present(true);
        } });

    // NOTE: nullptr intentional -- inner tasks already call m_jobSystem.ParallelFor.
    // Nested ParallelFor on the same JobSystem would deadlock.
    m_systemScheduler.Execute(nullptr);

    m_frameContexts[m_currentFrameIndex].MarkSubmitted(m_frameNumber);
    m_frameContexts[m_currentFrameIndex].MarkCompleted(m_frameNumber);
    m_framePhase = RenderFramePhase::Idle;
}

void GDXECSRenderer::Resize(int w, int h)
{
    if (m_backend) m_backend->Resize(w, h);
    if (h > 0)
    {
        const float aspect = static_cast<float>(w) / static_cast<float>(h);
        m_registry.View<CameraComponent>([aspect](EntityID, CameraComponent& cam)
        { cam.aspectRatio = aspect; });
    }
    m_persistentFrameState.viewportWidth  = static_cast<float>(w);
    m_persistentFrameState.viewportHeight = static_cast<float>(h);
    m_frameData.viewportWidth  = static_cast<float>(w);
    m_frameData.viewportHeight = static_cast<float>(h);
}

void GDXECSRenderer::Shutdown()
{
    if (!m_initialized && !m_backend) return;

    if (m_backend)
    {
        {
            std::vector<RenderTargetHandle> rtHandles;
            m_rtStore.ForEach([&rtHandles](RenderTargetHandle h, GDXRenderTargetResource&)
            { rtHandles.push_back(h); });
            for (const RenderTargetHandle h : rtHandles)
                m_backend->DestroyRenderTarget(h, m_rtStore, m_texStore);
            m_mainScenePostProcessTarget = RenderTargetHandle::Invalid();
        }
        {
            std::vector<MeshHandle> meshHandles;
            m_meshStore.ForEach([&meshHandles](MeshHandle h, MeshAssetResource&)
            { meshHandles.push_back(h); });
            for (const MeshHandle h : meshHandles)
                m_meshStore.Release(h);
        }

        m_shaderCache.Clear();
        m_defaultShader = ShaderHandle::Invalid();
        m_shadowShader  = ShaderHandle::Invalid();

        m_backend->DestroyPostProcessPasses(m_postProcessStore);
        m_backend->Shutdown(m_matStore, m_shaderStore, m_texStore);
        m_backend.reset();
    }

    m_postProcessPassOrder.clear();
    m_initialized = false;
}
