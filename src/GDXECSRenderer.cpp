#include "GDXECSRenderer.h"
#include "MaterialSemanticLayout.h"
#include "GDXDX11RenderBackend.h"
#include "GDXDX11ShaderCompiler.h"
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
#include <unordered_set>

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
        SR_PARTICLES     = 1ull << 7,
    };

    void CleanupStaleRttPostProcessTargets(
        Registry& registry,
        IGDXRenderBackend* backend,
        ResourceStore<GDXRenderTargetResource, RenderTargetTag>& rtStore,
        ResourceStore<GDXTextureResource, TextureTag>& texStore,
        std::unordered_map<RenderTargetHandle, RenderTargetHandle>& rttPostProcessTargets)
    {
        if (!backend)
        {
            rttPostProcessTargets.clear();
            return;
        }

        std::unordered_set<RenderTargetHandle> activeSourceTargets;
        registry.View<RenderTargetCameraComponent>(
            [&activeSourceTargets](EntityID, RenderTargetCameraComponent& rtCam)
            {
                if (rtCam.enabled && rtCam.target.IsValid())
                    activeSourceTargets.insert(rtCam.target);
            });

        for (auto it = rttPostProcessTargets.begin(); it != rttPostProcessTargets.end(); )
        {
            const RenderTargetHandle sourceTarget = it->first;
            RenderTargetHandle& postProcessTarget = it->second;

            GDXRenderTargetResource* sourceRt = sourceTarget.IsValid() ? rtStore.Get(sourceTarget) : nullptr;
            GDXRenderTargetResource* ppRt = postProcessTarget.IsValid() ? rtStore.Get(postProcessTarget) : nullptr;

            const bool sourceStillExists = (sourceRt != nullptr);
            const bool sourceStillActive = activeSourceTargets.find(sourceTarget) != activeSourceTargets.end();
            const bool ppStillExists = (ppRt != nullptr);

            if (!sourceStillExists || !sourceStillActive)
            {
                if (postProcessTarget.IsValid() && ppStillExists)
                    backend->DestroyRenderTarget(postProcessTarget, rtStore, texStore);
                it = rttPostProcessTargets.erase(it);
                continue;
            }

            if (postProcessTarget.IsValid() && !ppStillExists)
            {
                postProcessTarget = RenderTargetHandle::Invalid();
                ++it;
                continue;
            }

            if (sourceRt && ppRt && (sourceRt->width != ppRt->width || sourceRt->height != ppRt->height))
            {
                backend->DestroyRenderTarget(postProcessTarget, rtStore, texStore);
                postProcessTarget = RenderTargetHandle::Invalid();
            }

            ++it;
        }
    }
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
    ctx.mainViewClearColor[0]    = m_clearColor[0];
    ctx.mainViewClearColor[1]    = m_clearColor[1];
    ctx.mainViewClearColor[2]    = m_clearColor[2];
    ctx.mainViewClearColor[3]    = m_clearColor[3];
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
    ctx.rttSceneTargets  = &m_rttPostProcessTargets;
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
    m_frameDispatch.execCtx.frameGraph           = &m_renderPipeline.frameGraph;

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

bool GDXECSRenderer::InitParticleRenderer(TextureHandle atlasTexture)
{
    m_particlesRenderReady = m_backend ? m_backend->InitParticleRenderer(atlasTexture) : false;
    return m_particlesRenderReady;
}

void GDXECSRenderer::SetParticleSystem(GDXParticleSystem* ps)
{
    m_particleSystemPtr = ps;
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

    // ShaderCache initialisieren
    m_shaderCache.Init(m_backend.get(), &m_shaderStore);

    // Standard-Shader laden — identisch zu dem was der User machen würde
    const ShaderPathConfig& cfg = m_shaderCache.GetConfig();
    m_defaultShader = CreateShader(cfg.mainVS, cfg.mainPS, cfg.mainFlags);
    if (!m_defaultShader.IsValid()) return false;

    m_shadowShader = CreateShader(cfg.shadowVS, cfg.shadowPS, cfg.shadowFlags);

    // Neutral IBL fallback
    m_backend->LoadIBL(nullptr);

    m_shadowResourcesAvailable = m_backend->HasShadowResources();
    m_freeCamera.AttachRegistry(&m_registry);
    // PostProcess-Shader werden lazy geladen wenn der User
    // SetToneMapping / SetBloom / SetFXAA / SetGTAO aufruft.
    m_initialized = true;
    return true;
}


bool GDXECSRenderer::EnsureGTAOPassesCreated()
{
    if (!m_gtaoPass.IsValid())
    {
        PostProcessPassDesc desc{};
        desc.vertexShaderFile            = L"PostProcessFullscreenVS.hlsl";
        desc.pixelShaderFile             = L"PostProcessGTAOPS.hlsl";
        desc.debugName                   = L"GTAO";
        desc.constantBufferBytes         = sizeof(GTAOParams);
        desc.enabled                     = false;
        desc.captureSceneColorAsOriginal = true;
        desc.inputSlots = {
            { L"SceneDepth",   0u, PostProcessInputSemantic::SceneDepth,   true },
            { L"SceneNormals", 1u, PostProcessInputSemantic::SceneNormals, true }
        };
        m_gtaoPass = CreatePostProcessPass(desc);
    }
    if (!m_gtaoBlurPass.IsValid())
    {
        PostProcessPassDesc desc{};
        desc.vertexShaderFile    = L"PostProcessFullscreenVS.hlsl";
        desc.pixelShaderFile     = L"PostProcessGTAOBlurPS.hlsl";
        desc.debugName           = L"GTAOBlur";
        desc.constantBufferBytes = sizeof(GTAOBlurParams);
        desc.enabled             = false;
        desc.inputSlots = {
            { L"SceneColor",   0u, PostProcessInputSemantic::SceneColor,   true },
            { L"SceneDepth",   1u, PostProcessInputSemantic::SceneDepth,   true },
            { L"SceneNormals", 2u, PostProcessInputSemantic::SceneNormals, true }
        };
        m_gtaoBlurPass = CreatePostProcessPass(desc);
    }
    if (!m_gtaoCompositePass.IsValid())
    {
        PostProcessPassDesc desc{};
        desc.vertexShaderFile    = L"PostProcessFullscreenVS.hlsl";
        desc.pixelShaderFile     = L"PostProcessGTAOCompositePS.hlsl";
        desc.debugName           = L"GTAOComposite";
        desc.constantBufferBytes = sizeof(GTAOCompositeParams);
        desc.enabled             = false;
        desc.inputSlots = {
            { L"SceneColor",         0u, PostProcessInputSemantic::SceneColor,         true },
            { L"OriginalSceneColor", 1u, PostProcessInputSemantic::OriginalSceneColor, true }
        };
        m_gtaoCompositePass = CreatePostProcessPass(desc);
    }
    return m_gtaoPass.IsValid() && m_gtaoBlurPass.IsValid() && m_gtaoCompositePass.IsValid();
}

void GDXECSRenderer::PrewarmPostProcessShaders()
{
    // Kompiliert alle Post-Process-Shader einmalig in Initialize,
    // damit kein Tastendruck spaeter einen Compile-Stall ausloest.
    // Alle Passes werden disabled erstellt.

    if (!m_toneMappingPass.IsValid())
    {
        PostProcessPassDesc desc{};
        desc.vertexShaderFile    = L"PostProcessFullscreenVS.hlsl";
        desc.pixelShaderFile     = L"PostProcessToneMappingPS.hlsl";
        desc.debugName           = L"ToneMapping_prewarm";
        desc.constantBufferBytes = sizeof(ToneMappingParams);
        desc.enabled             = false;
        m_toneMappingPass = CreatePostProcessPass(desc);
    }
    if (!m_fxaaPass.IsValid())
    {
        PostProcessPassDesc desc{};
        desc.vertexShaderFile    = L"PostProcessFullscreenVS.hlsl";
        desc.pixelShaderFile     = L"PostProcessFXAAPS.hlsl";
        desc.debugName           = L"FXAA_prewarm";
        desc.constantBufferBytes = sizeof(FXAAParams);
        desc.enabled             = false;
        m_fxaaPass = CreatePostProcessPass(desc);
    }
    if (!m_bloomBrightPass.IsValid())
    {
        PostProcessPassDesc desc{};
        desc.vertexShaderFile    = L"PostProcessFullscreenVS.hlsl";
        desc.pixelShaderFile     = L"PostProcessBloomBrightPS.hlsl";
        desc.debugName           = L"BloomBright_prewarm";
        desc.constantBufferBytes = sizeof(BloomBrightParams);
        desc.enabled             = false;
        desc.inputSlots = {{ L"SceneColor", 0u, PostProcessInputSemantic::SceneColor, true }};
        m_bloomBrightPass = CreatePostProcessPass(desc);
    }
    if (!m_bloomBlurHPass.IsValid())
    {
        PostProcessPassDesc desc{};
        desc.vertexShaderFile    = L"PostProcessFullscreenVS.hlsl";
        desc.pixelShaderFile     = L"PostProcessBloomBlurPS.hlsl";
        desc.debugName           = L"BloomBlurH_prewarm";
        desc.constantBufferBytes = sizeof(BloomBlurParams);
        desc.enabled             = false;
        desc.inputSlots = {{ L"SceneColor", 0u, PostProcessInputSemantic::SceneColor, true }};
        m_bloomBlurHPass = CreatePostProcessPass(desc);
    }
    if (!m_bloomBlurVPass.IsValid())
    {
        PostProcessPassDesc desc{};
        desc.vertexShaderFile    = L"PostProcessFullscreenVS.hlsl";
        desc.pixelShaderFile     = L"PostProcessBloomBlurPS.hlsl";
        desc.debugName           = L"BloomBlurV_prewarm";
        desc.constantBufferBytes = sizeof(BloomBlurParams);
        desc.enabled             = false;
        desc.inputSlots = {{ L"SceneColor", 0u, PostProcessInputSemantic::SceneColor, true }};
        m_bloomBlurVPass = CreatePostProcessPass(desc);
    }
    if (!m_bloomCompositePass.IsValid())
    {
        PostProcessPassDesc desc{};
        desc.vertexShaderFile    = L"PostProcessFullscreenVS.hlsl";
        desc.pixelShaderFile     = L"PostProcessBloomCompositePS.hlsl";
        desc.debugName           = L"BloomComposite_prewarm";
        desc.constantBufferBytes = sizeof(BloomCompositeParams);
        desc.enabled             = false;
        desc.inputSlots = {
            { L"SceneColor",         0u, PostProcessInputSemantic::SceneColor,         true },
            { L"OriginalSceneColor", 1u, PostProcessInputSemantic::OriginalSceneColor, true }
        };
        m_bloomCompositePass = CreatePostProcessPass(desc);
    }
    if (!m_volumetricFogPass.IsValid())
    {
        PostProcessPassDesc desc{};
        desc.vertexShaderFile    = L"PostProcessFullscreenVS.hlsl";
        desc.pixelShaderFile     = L"PostProcessVolumetricFogPS.hlsl";
        desc.debugName           = L"VolumetricFog_prewarm";
        desc.constantBufferBytes = sizeof(VolumetricFogParams);
        desc.enabled             = false;
        desc.inputSlots = {
            { L"SceneColor", 0u, PostProcessInputSemantic::SceneColor, true },
            { L"SceneDepth", 1u, PostProcessInputSemantic::SceneDepth, true },
            { L"ShadowMap",  2u, PostProcessInputSemantic::ShadowMap, false }
        };
        m_volumetricFogPass = CreatePostProcessPass(desc);
    }
    EnsureGTAOPassesCreated();
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
    return m_backend->UploadTexture(m_texStore, filePath, isSRGB, m_defaultWhiteTex);
}

TextureHandle GDXECSRenderer::CreateTexture(
    const ImageBuffer& image, const std::wstring& debugName, bool isSRGB)
{
    if (!m_backend || !image.IsValid()) return m_defaultWhiteTex;
    return m_backend->UploadTextureFromImage(m_texStore, image, isSRGB, debugName, m_defaultWhiteTex);
}

MeshHandle GDXECSRenderer::UploadMesh(MeshAssetResource mesh)
{
    MeshHandle h = m_meshStore.Add(std::move(mesh));
    if (auto* r = m_meshStore.Get(h); r && m_backend)
        m_backend->UploadMesh(h, *r);
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

    const MaterialSemanticLayout materialLayout = MaterialSemanticLayout::BuildDefault();
    MaterialSemanticValidationResult materialValidation{};
    if (!materialLayout.ValidateMaterial(mat, &materialValidation))
        Debug::LogWarning(GDX_SRC_LOC, "CreateMaterial: MaterialSemanticLayout-Validierung fehlgeschlagen.");

    if (mat.GetShader().IsValid())
    {
        if (const GDXShaderResource* shader = m_shaderStore.Get(mat.GetShader()))
        {
            MaterialSemanticValidationResult shaderValidation{};
            if (!materialLayout.ValidateShaderCompatibility(shader->layout, mat, &shaderValidation))
                Debug::LogWarning(GDX_SRC_LOC, "CreateMaterial: autoritativer Material-/Shader-Vertrag fehlgeschlagen.");
        }
    }

    MaterialHandle h = m_matStore.Add(std::move(mat));
    if (auto* r = m_matStore.Get(h))
    {
        r->SetSortID(h.Index());
        if (m_backend) m_backend->UploadMaterial(h, *r);
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

PostProcessHandle GDXECSRenderer::CreatePostProcessPass(
    const PostProcessPassDesc& desc, PostProcessInsert position)
{
    if (!m_backend) return PostProcessHandle::Invalid();
    PostProcessHandle h = m_backend->CreatePostProcessPass(m_postProcessStore, desc);
    if (!h.IsValid()) return h;

    auto& order = m_postProcessPassOrder;

    auto insertBefore = [&](PostProcessHandle anchor) -> bool
    {
        auto it = std::find(order.begin(), order.end(), anchor);
        if (it != order.end()) { order.insert(it, h); return true; }
        return false;
    };

    switch (position)
    {
    case PostProcessInsert::Front:
        order.insert(order.begin(), h);
        break;

    case PostProcessInsert::BeforeToneMap:
        if (!insertBefore(m_toneMappingPass))
            order.push_back(h);
        break;

    case PostProcessInsert::AfterToneMap:
        if (m_toneMappingPass.IsValid())
        {
            auto it = std::find(order.begin(), order.end(), m_toneMappingPass);
            if (it != order.end()) { order.insert(std::next(it), h); break; }
        }
        order.push_back(h);
        break;

    case PostProcessInsert::BeforeFXAA:
        if (!insertBefore(m_fxaaPass))
            order.push_back(h);
        break;

    case PostProcessInsert::AfterFXAA:
        if (m_fxaaPass.IsValid())
        {
            auto it = std::find(order.begin(), order.end(), m_fxaaPass);
            if (it != order.end()) { order.insert(std::next(it), h); break; }
        }
        order.push_back(h);
        break;

    case PostProcessInsert::End:
    default:
        order.push_back(h);
        break;
    }

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

bool GDXECSRenderer::SetPostProcessCustomInput(PostProcessHandle h,
                                                const std::wstring& slotName,
                                                TextureHandle texture)
{
    auto* pass = m_postProcessStore.Get(h);
    if (!pass) return false;
    for (PostProcessInputSlot& slot : pass->inputs)
    {
        if (slot.semantic == PostProcessInputSemantic::Custom && slot.name == slotName)
        {
            slot.customTexture = texture;
            return true;
        }
    }
    return false; // Slot mit diesem Namen nicht gefunden
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
    m_depthDebugPass     = PostProcessHandle::Invalid();
    m_normalDebugPass    = PostProcessHandle::Invalid();
    m_gtaoPass           = PostProcessHandle::Invalid();
    m_gtaoBlurPass       = PostProcessHandle::Invalid();
    m_gtaoCompositePass  = PostProcessHandle::Invalid();
    m_fogPass            = PostProcessHandle::Invalid();
    m_volumetricFogPass  = PostProcessHandle::Invalid();
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
        d.captureSceneColorAsOriginal = true;
        d.inputSlots = {
            { L"SceneColor", 0u, PostProcessInputSemantic::SceneColor, true }
        };
        m_bloomBrightPass     = CreatePostProcessPass(d);
    }
    if (!m_bloomBlurHPass.IsValid())
    {
        PostProcessPassDesc d{};
        d.vertexShaderFile    = L"PostProcessFullscreenVS.hlsl";
        d.pixelShaderFile     = L"PostProcessBloomBlurPS.hlsl";
        d.debugName           = L"BloomBlurH";
        d.constantBufferBytes = sizeof(BloomBlurParams);
        d.inputSlots = {
            { L"SceneColor", 0u, PostProcessInputSemantic::SceneColor, true }
        };
        m_bloomBlurHPass      = CreatePostProcessPass(d);
    }
    if (!m_bloomBlurVPass.IsValid())
    {
        PostProcessPassDesc d{};
        d.vertexShaderFile    = L"PostProcessFullscreenVS.hlsl";
        d.pixelShaderFile     = L"PostProcessBloomBlurPS.hlsl";
        d.debugName           = L"BloomBlurV";
        d.constantBufferBytes = sizeof(BloomBlurParams);
        d.inputSlots = {
            { L"SceneColor", 0u, PostProcessInputSemantic::SceneColor, true }
        };
        m_bloomBlurVPass      = CreatePostProcessPass(d);
    }
    if (!m_bloomCompositePass.IsValid())
    {
        PostProcessPassDesc d{};
        d.vertexShaderFile    = L"PostProcessFullscreenVS.hlsl";
        d.pixelShaderFile     = L"PostProcessBloomCompositePS.hlsl";
        d.debugName           = L"BloomComposite";
        d.constantBufferBytes = sizeof(BloomCompositeParams);
        d.inputSlots = {
            { L"SceneColor",         0u, PostProcessInputSemantic::SceneColor,         true },
            { L"OriginalSceneColor", 1u, PostProcessInputSemantic::OriginalSceneColor, true }
        };
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

    auto& order = m_postProcessPassOrder;
    auto removeHandle = [&order](PostProcessHandle h)
    {
        auto it = std::find(order.begin(), order.end(), h);
        if (it != order.end()) order.erase(it);
    };
    removeHandle(m_bloomBrightPass);
    removeHandle(m_bloomBlurHPass);
    removeHandle(m_bloomBlurVPass);
    removeHandle(m_bloomCompositePass);

    auto insertPos = order.begin();
    auto tmIt   = std::find(order.begin(), order.end(), m_toneMappingPass);
    auto fxaaIt = std::find(order.begin(), order.end(), m_fxaaPass);
    if (tmIt != order.end()) insertPos = tmIt;
    else if (fxaaIt != order.end()) insertPos = fxaaIt;
    else insertPos = order.end();

    insertPos = order.insert(insertPos, m_bloomBrightPass);
    ++insertPos;
    insertPos = order.insert(insertPos, m_bloomBlurHPass);
    ++insertPos;
    insertPos = order.insert(insertPos, m_bloomBlurVPass);
    ++insertPos;
    order.insert(insertPos, m_bloomCompositePass);
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

void GDXECSRenderer::SetDepthDebugView(bool enabled)
{
    if (!enabled)
    {
        if (m_depthDebugPass.IsValid())
            SetPostProcessEnabled(m_depthDebugPass, false);
        return;
    }

    if (!m_depthDebugPass.IsValid())
    {
        PostProcessPassDesc desc{};
        desc.vertexShaderFile    = L"PostProcessFullscreenVS.hlsl";
        desc.pixelShaderFile     = L"PostProcessDepthDebugPS.hlsl";
        desc.debugName           = L"DepthDebug";
        desc.constantBufferBytes = 16u;
        desc.enabled             = true;
        desc.inputSlots = {
            { L"SceneDepth", 0u, PostProcessInputSemantic::SceneDepth, true }
        };
        m_depthDebugPass = CreatePostProcessPass(desc);
        Debug::Log(GDX_SRC_LOC,
                   L"SetDepthDebugView create result valid=",
                   m_depthDebugPass.IsValid() ? 1ull : 0ull,
                   L" handle=",
                   static_cast<unsigned long long>(m_depthDebugPass.value));
    }

    if (!m_depthDebugPass.IsValid())
    {
        Debug::LogError(GDX_SRC_LOC, L"SetDepthDebugView failed: invalid pass handle");
        return;
    }

    SetPostProcessEnabled(m_depthDebugPass, true);
    Debug::Log(GDX_SRC_LOC,
               L"SetDepthDebugView enabled handle=",
               static_cast<unsigned long long>(m_depthDebugPass.value));

    auto& order = m_postProcessPassOrder;
    auto it = std::find(order.begin(), order.end(), m_depthDebugPass);
    if (it != order.end())
        order.erase(it);
    order.push_back(m_depthDebugPass);

    Debug::Log(GDX_SRC_LOC,
               L"SetDepthDebugView order size=",
               static_cast<unsigned long long>(order.size()),
               L" pushed handle=",
               static_cast<unsigned long long>(m_depthDebugPass.value));
}


void GDXECSRenderer::SetNormalDebugView(bool enabled)
{
    if (!enabled)
    {
        if (m_normalDebugPass.IsValid())
            SetPostProcessEnabled(m_normalDebugPass, false);
        return;
    }

    if (!m_normalDebugPass.IsValid())
    {
        PostProcessPassDesc desc{};
        desc.vertexShaderFile    = L"PostProcessFullscreenVS.hlsl";
        desc.pixelShaderFile     = L"PostProcessNormalDebugPS.hlsl";
        desc.debugName           = L"NormalDebug";
        desc.constantBufferBytes = sizeof(GTAOCompositeParams);
        desc.enabled             = true;
        desc.inputSlots = {
            { L"SceneNormals", 0u, PostProcessInputSemantic::SceneNormals, true }
        };
        m_normalDebugPass = CreatePostProcessPass(desc);
        Debug::Log(GDX_SRC_LOC,
                   L"SetNormalDebugView create result valid=",
                   m_normalDebugPass.IsValid() ? 1ull : 0ull,
                   L" handle=",
                   static_cast<unsigned long long>(m_normalDebugPass.value));
    }

    if (!m_normalDebugPass.IsValid())
    {
        Debug::LogError(GDX_SRC_LOC, L"SetNormalDebugView failed: invalid pass handle");
        return;
    }

    SetPostProcessEnabled(m_normalDebugPass, true);
    Debug::Log(GDX_SRC_LOC,
               L"SetNormalDebugView enabled handle=",
               static_cast<unsigned long long>(m_normalDebugPass.value));

    auto& order = m_postProcessPassOrder;
    auto it = std::find(order.begin(), order.end(), m_normalDebugPass);
    if (it != order.end())
        order.erase(it);
    order.push_back(m_normalDebugPass);

    Debug::Log(GDX_SRC_LOC,
               L"SetNormalDebugView order size=",
               static_cast<unsigned long long>(order.size()),
               L" pushed handle=",
               static_cast<unsigned long long>(m_normalDebugPass.value));
}


void GDXECSRenderer::SetEdgeDebugView(bool enabled,
                                     int viewportW, int viewportH,
                                     float depthScale,
                                     float normalScale,
                                     bool depthOnly,
                                     bool normalOnly)
{
    if (!enabled)
    {
        if (m_edgeDebugPass.IsValid())
            SetPostProcessEnabled(m_edgeDebugPass, false);
        return;
    }

    if (!m_edgeDebugPass.IsValid())
    {
        PostProcessPassDesc desc{};
        desc.vertexShaderFile    = L"PostProcessFullscreenVS.hlsl";
        desc.pixelShaderFile     = L"PostProcessEdgeDebugPS.hlsl";
        desc.debugName           = L"EdgeDebug";
        desc.constantBufferBytes = sizeof(EdgeDebugParams);
        desc.enabled             = true;
        desc.inputSlots = {
            { L"SceneDepth",   0u, PostProcessInputSemantic::SceneDepth,   true },
            { L"SceneNormals", 1u, PostProcessInputSemantic::SceneNormals, true }
        };
        m_edgeDebugPass = CreatePostProcessPass(desc);
        Debug::Log(GDX_SRC_LOC,
                   L"SetEdgeDebugView create result valid=",
                   m_edgeDebugPass.IsValid() ? 1ull : 0ull,
                   L" handle=",
                   static_cast<unsigned long long>(m_edgeDebugPass.value));
    }

    if (!m_edgeDebugPass.IsValid())
    {
        Debug::LogError(GDX_SRC_LOC, L"SetEdgeDebugView failed: invalid pass handle");
        return;
    }

    const float w = (viewportW > 0) ? static_cast<float>(viewportW) : 1280.0f;
    const float h = (viewportH > 0) ? static_cast<float>(viewportH) : 720.0f;

    EdgeDebugParams params{};
    params.texelW      = 1.0f / w;
    params.texelH      = 1.0f / h;
    params.depthScale  = depthScale;
    params.normalScale = normalScale;
    params.depthOnly   = depthOnly ? 1.0f : 0.0f;
    params.normalOnly  = normalOnly ? 1.0f : 0.0f;

    SetPostProcessEnabled(m_edgeDebugPass, true);
    SetPostProcessConstants(m_edgeDebugPass, &params, sizeof(params));

    auto& order = m_postProcessPassOrder;
    auto it = std::find(order.begin(), order.end(), m_edgeDebugPass);
    if (it != order.end())
        order.erase(it);
    order.push_back(m_edgeDebugPass);

    Debug::Log(GDX_SRC_LOC,
               L"SetEdgeDebugView enabled handle=",
               static_cast<unsigned long long>(m_edgeDebugPass.value),
               L" depthScale=", static_cast<unsigned long long>(depthScale),
               L" normalScale=", static_cast<unsigned long long>(normalScale));
}


void GDXECSRenderer::SetGTAO(int viewportW, int viewportH,
                             float nearPlane, float farPlane,
                             float radiusPixels, float thickness,
                             float intensity, float power)
{
    const float w = (viewportW > 0) ? static_cast<float>(viewportW) : 1280.0f;
    const float h = (viewportH > 0) ? static_cast<float>(viewportH) : 720.0f;

    if (!EnsureGTAOPassesCreated())
        return;

    GTAOParams ao{};
    ao.texelW         = 1.0f / w;
    ao.texelH         = 1.0f / h;
    ao.radiusPixels   = (radiusPixels > 0.0f) ? radiusPixels : 8.0f;
    ao.thickness      = (thickness > 0.0f) ? thickness : 0.45f;
    ao.intensity      = (intensity > 0.0f) ? intensity : 2.0f;
    ao.power          = (power > 0.0f) ? power : 2.0f;
    ao.normalBias     = 0.12f;
    ao.depthClamp     = 0.10f;
    ao.nearPlane      = nearPlane;
    ao.farPlane       = farPlane;
    ao.depthFadeStart = farPlane * 0.08f;
    ao.depthFadeEnd   = farPlane * 0.8f;
    ao.projScaleX     = 1.0f;
    ao.projScaleY     = 1.0f;
    ao.directionCount = 8u;   // MAX_DIRECTIONS im Shader
    ao.stepCount      = 4u;   // MAX_STEPS im Shader
    ao.cameraIsOrtho  = 0u;
    ao.debugView      = 0u;
    SetPostProcessEnabled(m_gtaoPass, true);
    SetPostProcessConstants(m_gtaoPass, &ao, sizeof(ao));

    GTAOBlurParams blur{};
    blur.texelW          = 1.0f / w;
    blur.texelH          = 1.0f / h;
    blur.depthSharpness  = 16.0f;
    blur.normalSharpness = 2.0f;
    blur.nearPlane       = nearPlane;
    blur.farPlane        = farPlane;
    blur.cameraIsOrtho   = 0u;
    SetPostProcessEnabled(m_gtaoBlurPass, true);
    SetPostProcessConstants(m_gtaoBlurPass, &blur, sizeof(blur));

    GTAOCompositeParams comp{};
    comp.minVisibility          = 0.45f;
    comp.strength               = 1.35f;
    comp.highlightProtectStart  = 2.0f;
    comp.highlightProtectEnd    = 6.0f;
    SetPostProcessEnabled(m_gtaoCompositePass, true);
    SetPostProcessConstants(m_gtaoCompositePass, &comp, sizeof(comp));

    auto& order = m_postProcessPassOrder;
    auto removeHandle = [&order](PostProcessHandle h)
    {
        auto it = std::find(order.begin(), order.end(), h);
        if (it != order.end()) order.erase(it);
    };
    removeHandle(m_gtaoPass);
    removeHandle(m_gtaoBlurPass);
    removeHandle(m_gtaoCompositePass);

    auto insertPos = order.begin();
    auto bloomCompositeIt = std::find(order.begin(), order.end(), m_bloomCompositePass);
    auto tmIt    = std::find(order.begin(), order.end(), m_toneMappingPass);
    auto fxaaIt  = std::find(order.begin(), order.end(), m_fxaaPass);
    if (bloomCompositeIt != order.end()) insertPos = std::next(bloomCompositeIt);
    else if (tmIt != order.end()) insertPos = tmIt;
    else if (fxaaIt != order.end()) insertPos = fxaaIt;
    else insertPos = order.end();

    insertPos = order.insert(insertPos, m_gtaoPass);
    ++insertPos;
    insertPos = order.insert(insertPos, m_gtaoBlurPass);
    ++insertPos;
    order.insert(insertPos, m_gtaoCompositePass);
}

void GDXECSRenderer::DisableGTAO()
{
    if (m_gtaoPass.IsValid())          SetPostProcessEnabled(m_gtaoPass, false);
    if (m_gtaoBlurPass.IsValid())      SetPostProcessEnabled(m_gtaoBlurPass, false);
    if (m_gtaoCompositePass.IsValid()) SetPostProcessEnabled(m_gtaoCompositePass, false);
}


void GDXECSRenderer::SetFog(const FogSettings& settings)
{
    m_fogSettings = settings;

    if (!settings.enabled)
    {
        DisableFog();
        return;
    }

    if (!m_fogPass.IsValid())
    {
        PostProcessPassDesc desc{};
        desc.vertexShaderFile    = L"PostProcessFullscreenVS.hlsl";
        desc.pixelShaderFile     = L"PostProcessDepthFogPS.hlsl";
        desc.debugName           = L"Fog";
        desc.constantBufferBytes = sizeof(FogParams);
        desc.enabled             = true;
        desc.inputSlots =
        {
            { L"SceneColor", 0u, PostProcessInputSemantic::SceneColor, true },
            { L"SceneDepth", 1u, PostProcessInputSemantic::SceneDepth, true }
        };
        m_fogPass = CreatePostProcessPass(desc);
    }

    if (!m_fogPass.IsValid())
        return;

    FogParams params{};
    params.colorR           = settings.colorR;
    params.colorG           = settings.colorG;
    params.colorB           = settings.colorB;
    params.mode             = static_cast<uint32_t>(settings.mode);
    params.start            = settings.start;
    params.end              = settings.end;
    params.density          = settings.density;
    params.maxOpacity       = settings.maxOpacity;
    params.power            = settings.power;
    params.heightStart      = settings.heightStart;
    params.heightEnd        = settings.heightEnd;
    params.heightStrength   = settings.heightStrength;
    params.cameraNearPlane  = 0.1f;
    params.cameraFarPlane   = 1000.0f;
    params.projScaleX       = 1.0f;
    params.projScaleY       = 1.0f;
    params.enabled          = settings.enabled ? 1u : 0u;
    params.heightFogEnabled = settings.heightFogEnabled ? 1u : 0u;
    params.cameraIsOrtho    = 0u;

    SetPostProcessEnabled(m_fogPass, true);
    SetPostProcessConstants(m_fogPass, &params, sizeof(params));

    auto& order = m_postProcessPassOrder;
    auto fogIt = std::find(order.begin(), order.end(), m_fogPass);
    if (fogIt != order.end())
        order.erase(fogIt);

    auto tmIt = std::find(order.begin(), order.end(), m_toneMappingPass);
    if (tmIt != order.end())
    {
        order.insert(tmIt, m_fogPass);
    }
    else
    {
        auto fxaaIt = std::find(order.begin(), order.end(), m_fxaaPass);
        if (fxaaIt != order.end())
            order.insert(fxaaIt, m_fogPass);
        else
            order.push_back(m_fogPass);
    }
}

void GDXECSRenderer::DisableFog()
{
    m_fogSettings.enabled = false;
    if (m_fogPass.IsValid())
        SetPostProcessEnabled(m_fogPass, false);
}

void GDXECSRenderer::SetDepthFogTest(bool enabled)
{
    if (!enabled)
    {
        DisableFog();
        return;
    }

    FogSettings settings{};
    settings.enabled = true;
    settings.mode = FogMode::LinearDepth;
    settings.colorR = 0.62f;
    settings.colorG = 0.68f;
    settings.colorB = 0.78f;
    settings.start = 0.55f;
    settings.end = 0.98f;
    settings.density = 2.0f;
    settings.maxOpacity = 0.75f;
    settings.power = 1.0f;
    settings.heightFogEnabled = false;
    settings.heightStart = 0.0f;
    settings.heightEnd = 1.0f;
    settings.heightStrength = 1.0f;
    SetFog(settings);
}


void GDXECSRenderer::SetVolumetricFog(const VolumetricFogSettings& settings)
{
    m_volumetricFogSettings = settings;

    if (!settings.enabled)
    {
        DisableVolumetricFog();
        return;
    }

    if (!m_volumetricFogPass.IsValid())
    {
        PostProcessPassDesc desc{};
        desc.vertexShaderFile    = L"PostProcessFullscreenVS.hlsl";
        desc.pixelShaderFile     = L"PostProcessVolumetricFogPS.hlsl";
        desc.debugName           = L"VolumetricFog";
        desc.constantBufferBytes = sizeof(VolumetricFogParams);
        desc.enabled             = true;
        desc.inputSlots =
        {
            { L"SceneColor", 0u, PostProcessInputSemantic::SceneColor, true },
            { L"SceneDepth", 1u, PostProcessInputSemantic::SceneDepth, true },
            { L"ShadowMap",  2u, PostProcessInputSemantic::ShadowMap, false }
        };
        m_volumetricFogPass = CreatePostProcessPass(desc);
    }

    if (!m_volumetricFogPass.IsValid())
        return;

    VolumetricFogParams params{};
    params.colorR         = settings.colorR;
    params.colorG         = settings.colorG;
    params.colorB         = settings.colorB;
    params.density        = settings.density;
    params.anisotropy     = settings.anisotropy;
    params.startDistance  = settings.startDistance;
    params.maxDistance    = settings.maxDistance;
    params.maxOpacity     = settings.maxOpacity;
    params.baseHeight     = settings.baseHeight;
    params.heightFalloff  = settings.heightFalloff;
    params.stepCount      = settings.stepCount;
    params.shadowStrength = settings.shadowStrength;
    params.lightIntensity = settings.lightIntensity;
    params.jitterStrength = settings.jitterStrength;

    SetPostProcessEnabled(m_volumetricFogPass, true);
    SetPostProcessConstants(m_volumetricFogPass, &params, sizeof(params));

    auto& order = m_postProcessPassOrder;
    auto it = std::find(order.begin(), order.end(), m_volumetricFogPass);
    if (it != order.end())
        order.erase(it);

    auto tmIt = std::find(order.begin(), order.end(), m_toneMappingPass);
    if (tmIt != order.end())
        order.insert(tmIt, m_volumetricFogPass);
    else
        order.push_back(m_volumetricFogPass);
}

void GDXECSRenderer::DisableVolumetricFog()
{
    m_volumetricFogSettings.enabled = false;
    if (m_volumetricFogPass.IsValid())
        SetPostProcessEnabled(m_volumetricFogPass, false);
}

void GDXECSRenderer::SetShadowMapSize(uint32_t size)
{
    if (m_backend) m_backend->SetShadowMapSize(size);
}

bool GDXECSRenderer::SupportsTextureFormat(GDXTextureFormat format) const
{
    return m_backend ? m_backend->SupportsTextureFormat(format) : false;
}

bool GDXECSRenderer::SupportsOcclusionCulling() const
{
    return m_backend ? m_backend->SupportsOcclusionCulling() : false;
}

bool GDXECSRenderer::SetFullscreen(bool fullscreen)
{
    return m_backend ? m_backend->SetFullscreen(fullscreen) : false;
}

bool GDXECSRenderer::IsFullscreen() const
{
    return m_backend ? m_backend->IsFullscreen() : false;
}

void GDXECSRenderer::SetOcclusionCulling(bool enabled)
{
    if (enabled && !SupportsOcclusionCulling())
    {
        Debug::LogWarning(GDX_SRC_LOC, L"SetOcclusionCulling: Backend unterstuetzt keine Occlusion Queries.");
        return;
    }
    m_occlusionCullingEnabled = enabled;
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
    if (!m_cameraSystem.Update(m_registry, snapshot))
    {
        snapshot.lightCount       = 0u;
        snapshot.hasShadowPass    = false;
        snapshot.shadowCasterMask = 0xFFFFFFFFu;
        snapshot.lightAffectMask  = 0xFFFFFFFFu;
        outFrame    = snapshot;
        m_frameData = snapshot;
        return;
    }

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
    CleanupStaleRttPostProcessTargets(
        m_registry,
        m_backend.get(),
        m_rtStore,
        m_texStore,
        m_rttPostProcessTargets);

    // Occlusion Query Ergebnisse vom letzten Frame einsammeln
    if (m_occlusionCullingEnabled && m_backend)
    {
        m_occlusionVisible.clear();
        m_backend->CollectOcclusionResults(m_occlusionVisible);
    }

    m_framePhase = RenderFramePhase::UpdateWrite;
    m_currentFrameIndex = (m_currentFrameIndex + 1u) % GDXMaxFramesInFlight;
    m_persistentFrameState.ApplyTo(m_frameData);
    m_frameContexts[m_currentFrameIndex].Begin(m_currentFrameIndex, ++m_frameNumber, &m_frameData);
    m_frameTransients[m_currentFrameIndex].BeginFrame();
    m_stats = {};
    if (m_backend) m_backend->BeginFrame(m_clearColor);

    //if ((m_frameNumber % 120u) == 0u)
    //{
    //    const GDXDX11RenderBackend* dx11 = dynamic_cast<const GDXDX11RenderBackend*>(m_backend.get());
    //    Debug::Log(GDX_SRC_LOC, "FrameSummary frame=", m_frameNumber,
    //               " meshAlive=", m_meshStore.AliveCount(),
    //               " matAlive=", m_matStore.AliveCount(),
    //               " shaderAlive=", m_shaderStore.AliveCount(),
    //               " texAlive=", m_texStore.AliveCount(),
    //               " rtAlive=", m_rtStore.AliveCount(),
    //               " postAlive=", m_postProcessStore.AliveCount(),
    //               " shaderVariants=", m_shaderCache.DebugVariantCount(),
    //               " trackedRttTargets=", m_rttPostProcessTargets.size(),
    //               " backendRttPairs=", dx11 ? dx11->DebugRttSurfacePairCount() : 0u,
    //               " trackedTexStates=", dx11 ? dx11->DebugTrackedTextureStateCount() : 0u,
    //               " pipelineCache=", dx11 ? dx11->DebugPipelineCacheSize() : 0u,
    //               " layoutCache=", dx11 ? dx11->DebugLayoutCacheSize() : 0u);
    //}
}

void GDXECSRenderer::Tick(float dt)
{
    m_lastDeltaTime = dt;
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

    // ---- Particles ----
    m_systemScheduler.AddTask({ "Particles",
        SR_TRANSFORM, SR_PARTICLES,
        [this]()
        {
            if (!m_particleSystemPtr)
                return;

            m_particleEmitterSystem.Update(
                m_registry,
                *m_particleSystemPtr,
                m_lastDeltaTime);
        } });

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

    m_systemScheduler.AddTask({ "Prepare Main Particles",
        SR_MAIN_VIEW | SR_PARTICLES, SR_MAIN_VIEW,
        [this]()
        {
            m_renderPipeline.mainView.execute.particleSubmission.Clear();
            if (!m_particlesRenderReady || !m_particleSystemPtr)
                return;

            ParticleRenderContext ctx{};
            ctx.viewMatrix = m_renderPipeline.mainView.prepared.frame.viewMatrix;
            ctx.viewProj = m_renderPipeline.mainView.prepared.frame.viewProjMatrix;
            ctx.cameraPosition = m_renderPipeline.mainView.prepared.frame.cameraPos;
            ctx.cameraForward = m_renderPipeline.mainView.prepared.frame.cameraForward;
            ctx.camRight = GDX::Normalize3({ ctx.viewMatrix._11, ctx.viewMatrix._21, ctx.viewMatrix._31 });
            ctx.camUp = GDX::Normalize3({ ctx.viewMatrix._12, ctx.viewMatrix._22, ctx.viewMatrix._32 });
            m_particleSystemPtr->BuildRenderSubmission(ctx, m_renderPipeline.mainView.execute.particleSubmission);
        } });

    m_systemScheduler.AddTask({ "Prepare RTT Particles",
        SR_RTT_VIEWS | SR_PARTICLES, SR_RTT_VIEWS,
        [this]()
        {
            if (!m_particlesRenderReady || !m_particleSystemPtr)
            {
                for (auto& view : m_renderPipeline.rttViews)
                    view.execute.particleSubmission.Clear();
                return;
            }

            for (auto& view : m_renderPipeline.rttViews)
            {
                ParticleRenderContext ctx{};
                ctx.viewMatrix = view.prepared.frame.viewMatrix;
                ctx.viewProj = view.prepared.frame.viewProjMatrix;
                ctx.cameraPosition = view.prepared.frame.cameraPos;
                ctx.cameraForward = view.prepared.frame.cameraForward;
                ctx.camRight = GDX::Normalize3({ ctx.viewMatrix._11, ctx.viewMatrix._21, ctx.viewMatrix._31 });
                ctx.camUp = GDX::Normalize3({ ctx.viewMatrix._12, ctx.viewMatrix._22, ctx.viewMatrix._32 });
                m_particleSystemPtr->BuildRenderSubmission(ctx, view.execute.particleSubmission);
            }
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

            // Occlusion-Ergebnisse vom letzten Frame anwenden:
            // Entities die letzten Frame verdeckt waren aus dem VisibleSet entfernen.
            // Erster Frame: m_occlusionVisible ist leer → alle bleiben drin (konservativ).
            if (m_occlusionCullingEnabled && !m_occlusionVisible.empty())
            {
                auto& candidates = m_renderPipeline.mainView.graphicsVisibleSet.candidates;
                const size_t before = candidates.size();
                candidates.erase(
                    std::remove_if(candidates.begin(), candidates.end(),
                        [this](const VisibleRenderCandidate& c)
                        {
                            return c.hasBounds &&
                                   m_occlusionVisible.find(c.entity) == m_occlusionVisible.end();
                        }),
                    candidates.end());
                const size_t culled = before - candidates.size();
                if (culled > 0u)
                    m_renderPipeline.mainView.stats.graphicsCulling.culledByFrustum += static_cast<uint32_t>(culled);
            }
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
        SR_RENDER_QUEUES | SR_BACKEND | SR_MAIN_VIEW | SR_RTT_VIEWS, SR_RENDER_QUEUES,
        [this]()
        {
            RenderPassBuilder::BuildFrameExecuteInputs(
                m_renderPipeline,
                m_rtStore,
                m_frameDispatch.postProc,
                m_frameDispatch.debugAppend,
                m_particlesRenderReady && m_particleSystemPtr && !m_renderPipeline.mainView.execute.particleSubmission.Empty());
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
            m_frameDispatch.execCtx.frameGraph = &m_renderPipeline.frameGraph;
            m_frameGraph.Execute(m_renderPipeline, m_frameDispatch.execCtx);

            m_renderPipeline.mainView.stats.drawCalls =
                m_backend ? m_backend->GetDrawCallCount() : 0u;
            m_renderPipeline.mainView.stats.renderCommands =
                m_renderPipeline.mainView.execute.opaquePass.enabled
                ? static_cast<uint32_t>(
                    m_renderPipeline.mainView.execute.opaqueQueue.Count() +
                    m_renderPipeline.mainView.execute.alphaQueue.Count()) : 0u;
            m_renderPipeline.mainView.stats.lightCount =
                m_renderPipeline.mainView.execute.frame.lightCount;

            // Occlusion Queries für nächsten Frame abschicken
            if (m_occlusionCullingEnabled && m_backend)
                m_backend->SubmitOcclusionQueries(
                    m_renderPipeline.mainView.graphicsVisibleSet.candidates,
                    m_meshStore,
                    m_renderPipeline.mainView.execute.frame);

            UpdatePreparedMainViewFrameTransient(m_renderPipeline.mainView);
            AggregatePreparedFrameStats(m_renderPipeline.mainView, m_renderPipeline.rttViews);
            LogDebugCullingStats();

            m_framePhase = RenderFramePhase::ExecuteSubmit;
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

    GDXDX11LogShaderCacheStats();

    if (m_backend)
    {
        {
            std::vector<RenderTargetHandle> rtHandles;
            m_rtStore.ForEach([&rtHandles](RenderTargetHandle h, GDXRenderTargetResource&)
            { rtHandles.push_back(h); });
            for (const RenderTargetHandle h : rtHandles)
                m_backend->DestroyRenderTarget(h, m_rtStore, m_texStore);
            m_mainScenePostProcessTarget = RenderTargetHandle::Invalid();
            m_rttPostProcessTargets.clear();
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
