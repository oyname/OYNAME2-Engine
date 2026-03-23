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
#include <algorithm>

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
// Context builders — used by FillFrameDispatch
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
    ctx.registry      = &m_registry;
    ctx.culling       = &m_viewCullingSystem;
    ctx.gather        = &m_gatherSystem;
    ctx.jobSystem     = &m_jobSystem;
    ctx.meshStore     = &m_meshStore;
    ctx.matStore      = &m_matStore;
    ctx.shaderStore   = &m_shaderStore;
    ctx.rtStore       = &m_rtStore;
    ctx.resolveShader = rs;
    return ctx;
}

// ---------------------------------------------------------------------------
// FillFrameDispatch — befüllt m_frameDispatch mit Heap-stabilen Zeigern.
// Muss einmal pro Frame vor EndFrame-Task-Scheduling aufgerufen werden.
// ---------------------------------------------------------------------------

void GDXECSRenderer::FillFrameDispatch(const RenderGatherSystem::ShaderResolver& rs)
{
    m_frameDispatch.Reset();

    m_frameDispatch.viewPrep   = MakeViewPrepContext();
    m_frameDispatch.cullGather = MakeCullGatherContext(rs);
    m_frameDispatch.postProc   = MakePostProcContext();

    m_frameDispatch.debugAppend = [this](
        RenderQueue& q, const VisibleSet& set,
        const RenderViewData& view, RFG::ViewStats* stats)
    {
        AppendDebugVisibleSet(q, set, view, stats);
    };

    m_frameDispatch.execCtx.backend              = m_backend.get();
    m_frameDispatch.execCtx.registry             = &m_registry;
    m_frameDispatch.execCtx.meshStore            = &m_meshStore;
    m_frameDispatch.execCtx.matStore             = &m_matStore;
    m_frameDispatch.execCtx.shaderStore          = &m_shaderStore;
    m_frameDispatch.execCtx.texStore             = &m_texStore;
    m_frameDispatch.execCtx.rtStore              = &m_rtStore;
    m_frameDispatch.execCtx.postProcessStore     = &m_postProcessStore;
    m_frameDispatch.execCtx.postProcessPassOrder = &m_postProcessPassOrder;

    m_frameDispatch.fgBuild.rtStore                    = &m_rtStore;
    m_frameDispatch.fgBuild.mainScenePostProcessTarget = m_mainScenePostProcessTarget;

    // Invariante prüfen — nur in Debug-Builds kostspielig
#if defined(_DEBUG) || defined(GIDX_ENABLE_FRAME_DISPATCH_VALIDATION)
    const char* reason = nullptr;
    if (!m_frameDispatch.IsValid(&reason))
        Debug::LogError(GDX_SRC_LOC, "FrameDispatch invalid after Fill: ", reason ? reason : "?");
#endif
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

    // Neutral IBL fallback — ensures t17/t18/t19 are always bound for PBR shading.
    // Users override this with LoadIBL(path) for a real environment map.
    m_backend->LoadIBL(nullptr);

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
    m_bloomBrightPass    = PostProcessHandle::Invalid();
    m_bloomBlurHPass     = PostProcessHandle::Invalid();
    m_bloomBlurVPass     = PostProcessHandle::Invalid();
    m_bloomCompositePass = PostProcessHandle::Invalid();
    m_toneMappingPass    = PostProcessHandle::Invalid();
    m_toneMappingMode    = ToneMappingMode::None;
    m_fxaaPass           = PostProcessHandle::Invalid();
}

void GDXECSRenderer::SetBloom(int viewportW, int viewportH,
                               float threshold, float intensity, float strength,
                               float tintR, float tintG, float tintB)
{
    const float w = (viewportW > 0) ? static_cast<float>(viewportW) : 1280.0f;
    const float h = (viewportH > 0) ? static_cast<float>(viewportH) : 720.0f;

    // Lazy-create all four passes on first call, in order.
    if (!m_bloomBrightPass.IsValid())
    {
        PostProcessPassDesc d{};
        d.vertexShaderFile    = L"PostProcessFullscreenVS.hlsl";
        d.pixelShaderFile     = L"PostProcessBloomBrightPS.hlsl";
        d.debugName           = L"BloomBright";
        d.constantBufferBytes = sizeof(BloomBrightParams);
        m_bloomBrightPass     = CreatePostProcessPass(d);
    }
    if (!m_bloomBlurHPass.IsValid())
    {
        PostProcessPassDesc d{};
        d.vertexShaderFile    = L"PostProcessFullscreenVS.hlsl";
        d.pixelShaderFile     = L"PostProcessBloomBlurPS.hlsl";
        d.debugName           = L"BloomBlurH";
        d.constantBufferBytes = sizeof(BloomBlurParams);
        m_bloomBlurHPass      = CreatePostProcessPass(d);
    }
    if (!m_bloomBlurVPass.IsValid())
    {
        PostProcessPassDesc d{};
        d.vertexShaderFile    = L"PostProcessFullscreenVS.hlsl";
        d.pixelShaderFile     = L"PostProcessBloomBlurPS.hlsl";
        d.debugName           = L"BloomBlurV";
        d.constantBufferBytes = sizeof(BloomBlurParams);
        m_bloomBlurVPass      = CreatePostProcessPass(d);
    }
    if (!m_bloomCompositePass.IsValid())
    {
        PostProcessPassDesc d{};
        d.vertexShaderFile    = L"PostProcessFullscreenVS.hlsl";
        d.pixelShaderFile     = L"PostProcessBloomCompositePS.hlsl";
        d.debugName           = L"BloomComposite";
        d.constantBufferBytes = sizeof(BloomCompositeParams);
        m_bloomCompositePass  = CreatePostProcessPass(d);
    }

    if (!m_bloomBrightPass.IsValid() || !m_bloomBlurHPass.IsValid() ||
        !m_bloomBlurVPass.IsValid()  || !m_bloomCompositePass.IsValid())
        return;

    // Update params
    BloomBrightParams bright{};
    bright.threshold = threshold;
    bright.intensity = intensity;
    SetPostProcessEnabled(m_bloomBrightPass, true);
    SetPostProcessConstants(m_bloomBrightPass, &bright, sizeof(bright));

    BloomBlurParams blurH{};
    blurH.texelSizeX = 1.0f / w;
    blurH.texelSizeY = 1.0f / h;
    blurH.directionX = 1.0f;
    blurH.directionY = 0.0f;
    SetPostProcessEnabled(m_bloomBlurHPass, true);
    SetPostProcessConstants(m_bloomBlurHPass, &blurH, sizeof(blurH));

    BloomBlurParams blurV{};
    blurV.texelSizeX = 1.0f / w;
    blurV.texelSizeY = 1.0f / h;
    blurV.directionX = 0.0f;
    blurV.directionY = 1.0f;
    SetPostProcessEnabled(m_bloomBlurVPass, true);
    SetPostProcessConstants(m_bloomBlurVPass, &blurV, sizeof(blurV));

    BloomCompositeParams comp{};
    comp.bloomTint[0]  = tintR;
    comp.bloomTint[1]  = tintG;
    comp.bloomTint[2]  = tintB;
    comp.bloomTint[3]  = 1.0f;
    comp.bloomStrength = strength;
    comp.sceneStrength = 1.0f;
    SetPostProcessEnabled(m_bloomCompositePass, true);
    SetPostProcessConstants(m_bloomCompositePass, &comp, sizeof(comp));
}

void GDXECSRenderer::DisableBloom()
{
    if (m_bloomBrightPass.IsValid())    SetPostProcessEnabled(m_bloomBrightPass,    false);
    if (m_bloomBlurHPass.IsValid())     SetPostProcessEnabled(m_bloomBlurHPass,     false);
    if (m_bloomBlurVPass.IsValid())     SetPostProcessEnabled(m_bloomBlurVPass,     false);
    if (m_bloomCompositePass.IsValid()) SetPostProcessEnabled(m_bloomCompositePass, false);
}

void GDXECSRenderer::SetToneMapping(ToneMappingMode mode, float exposure, float gamma)
{
    if (mode == ToneMappingMode::None)
    {
        if (m_toneMappingPass.IsValid())
            SetPostProcessEnabled(m_toneMappingPass, false);
        m_toneMappingMode = ToneMappingMode::None;
        return;
    }

    // Lazy-create the pass on first call.
    if (!m_toneMappingPass.IsValid())
    {
        PostProcessPassDesc desc{};
        desc.vertexShaderFile    = L"PostProcessFullscreenVS.hlsl";
        desc.pixelShaderFile     = L"PostProcessToneMappingPS.hlsl";
        desc.debugName           = L"ToneMapping";
        desc.constantBufferBytes = sizeof(ToneMappingParams);
        desc.enabled             = true;
        m_toneMappingPass = CreatePostProcessPass(desc);
    }

    if (!m_toneMappingPass.IsValid())
        return;  // shader compile failed — silent no-op

    ToneMappingParams params{};
    params.exposure = exposure;
    params.gamma    = gamma;
    params.mode     = static_cast<int>(mode);

    SetPostProcessEnabled(m_toneMappingPass, true);
    SetPostProcessConstants(m_toneMappingPass, &params, sizeof(params));
    m_toneMappingMode = mode;

    // ToneMapping must precede FXAA (FXAA runs on LDR output).
    // If FXAA already sits at the end, move TM just before it.
    if (m_fxaaPass.IsValid())
    {
        auto& order = m_postProcessPassOrder;
        auto fxaaIt = std::find(order.begin(), order.end(), m_fxaaPass);
        auto tmIt   = std::find(order.begin(), order.end(), m_toneMappingPass);
        if (fxaaIt != order.end() && tmIt != order.end() && tmIt > fxaaIt)
        {
            order.erase(tmIt);
            fxaaIt = std::find(order.begin(), order.end(), m_fxaaPass);
            order.insert(fxaaIt, m_toneMappingPass);
        }
    }
}

void GDXECSRenderer::DisableToneMapping()
{
    SetToneMapping(ToneMappingMode::None);
}

void GDXECSRenderer::SetFXAA(int viewportW, int viewportH,
                               float contrastThreshold, float relativeThreshold)
{
    if (!m_fxaaPass.IsValid())
    {
        PostProcessPassDesc desc{};
        desc.vertexShaderFile    = L"PostProcessFullscreenVS.hlsl";
        desc.pixelShaderFile     = L"PostProcessFXAAPS.hlsl";
        desc.debugName           = L"FXAA";
        desc.constantBufferBytes = sizeof(FXAAParams);
        desc.enabled             = true;
        m_fxaaPass = CreatePostProcessPass(desc);
    }

    if (!m_fxaaPass.IsValid())
        return;

    const float w = (viewportW > 0) ? static_cast<float>(viewportW) : 1280.0f;
    const float h = (viewportH > 0) ? static_cast<float>(viewportH) : 720.0f;

    FXAAParams params{};
    params.texelW            = 1.0f / w;
    params.texelH            = 1.0f / h;
    params.contrastThreshold = contrastThreshold;
    params.relativeThreshold = relativeThreshold;

    SetPostProcessEnabled(m_fxaaPass, true);
    SetPostProcessConstants(m_fxaaPass, &params, sizeof(params));
}

void GDXECSRenderer::DisableFXAA()
{
    if (m_fxaaPass.IsValid())
        SetPostProcessEnabled(m_fxaaPass, false);
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
    // Shader-Resolver bleibt lokal — er captured nur this, kein Stack-Speicher.
    // FrameDispatch speichert eine Kopie davon (std::function), deshalb ist
    // die Lifetime von resolveShader nicht kritisch für die Task-Lambdas.
    auto resolveShader = [this](RenderPass pass, const SubmeshData& submesh, const MaterialResource& mat)
    {
        return ResolveShaderVariant(pass, submesh, mat);
    };

    // m_frameDispatch befüllen — alle Task-Lambdas capturen nur [this],
    // lesen Contexts aus m_frameDispatch (Heap-Member, frame-stabil).
    FillFrameDispatch(resolveShader);

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
        [this]()
        {
            RenderViewPrep::PrepareMainView(
                m_frameDispatch.viewPrep,
                m_renderPipeline.frameSnapshot,
                m_debugCamera,
                m_renderPipeline.mainView);
        } });

    m_systemScheduler.AddTask({ "Prepare RTT Views",
        SR_FRAME | SR_STATS, SR_RTT_VIEWS,
        [this]()
        {
            RenderViewPrep::PrepareRTTViews(
                m_frameDispatch.viewPrep,
                m_renderPipeline.frameSnapshot,
                m_renderPipeline.rttViews);
        } });

    // ---- Cull + Gather ----
    m_systemScheduler.AddTask({ "Cull+Gather RTT",
        SR_RTT_VIEWS | SR_TRANSFORM, SR_RTT_VIEWS,
        [this]()
        {
            CullGather::CullGatherRTTViews(
                m_frameDispatch.cullGather,
                m_renderPipeline.rttViews);
        } });

    m_systemScheduler.AddTask({ "Cull+Gather Main",
        SR_MAIN_VIEW | SR_TRANSFORM, SR_MAIN_VIEW,
        [this]()
        {
            m_framePhase = RenderFramePhase::VisibilityBuild;
            CullGather::CullGatherMainView(
                m_frameDispatch.cullGather,
                m_renderPipeline.mainView);
        } });

    // ---- Queue Finalization ----
    m_systemScheduler.AddTask({ "Finalize Frame Queues",
        SR_MAIN_VIEW | SR_RTT_VIEWS | SR_TRANSFORM, SR_RENDER_QUEUES,
        [this]()
        {
            CullGather::FinalizeFrameQueues(
                m_frameDispatch.cullGather,
                m_renderPipeline);
        } });

    // ---- Execute-Input Build (Layer 2) ----
    m_systemScheduler.AddTask({ "Build Frame Execute Inputs",
        SR_RENDER_QUEUES | SR_BACKEND, SR_RENDER_QUEUES,
        [this]()
        {
            RenderPassBuilder::BuildFrameExecuteInputs(
                m_renderPipeline,
                m_rtStore,
                m_frameDispatch.postProc,
                m_frameDispatch.debugAppend);
        } });

    // ---- Framegraph Build ----
    m_systemScheduler.AddTask({ "Build Frame Graph",
        SR_RENDER_QUEUES | SR_BACKEND, 0ull,                      // ← liest nur, schreibt nichts
        [this]()
        {
            // fgBuild.mainScenePostProcessTarget wird in PrepareMainViewPostProcess
            // aktualisiert — nach Build Frame Execute Inputs neu lesen.
            m_frameDispatch.fgBuild.mainScenePostProcessTarget =
                m_mainScenePostProcessTarget;
            m_frameGraph.Build(m_renderPipeline, m_frameDispatch.fgBuild);
        } });

    // ---- Backend Execution (Layer 3) ----
    m_systemScheduler.AddTask({ "Execute Frame Graph",
        SR_RENDER_QUEUES | SR_MAIN_VIEW | SR_RTT_VIEWS | SR_TRANSFORM | SR_BACKEND,
        SR_BACKEND | SR_STATS,
        [this]()
        {
            m_frameGraph.Execute(m_renderPipeline, m_frameDispatch.execCtx);

            m_renderPipeline.mainView.stats.drawCalls =
                m_backend ? m_backend->GetDrawCallCount() : 0u;
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

    // Alle Task-Lambdas capturen nur [this] und lesen aus m_frameDispatch —
    // Heap-Member, keine Stack-Zeiger mehr. Execute(&m_jobSystem) ist jetzt sicher.
    // Inner-Parallelismus (CullGatherRTTViews/MainView) läuft weiterhin über
    // den selben JobSystem — kein nested ParallelFor weil CullGatherRTTViews
    // intern js=nullptr übergibt.
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
