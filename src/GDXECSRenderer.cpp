#include "GDXECSRenderer.h"
#include "CameraSystem.h"
#include "GDXRenderTargetResource.h"
#include "Debug.h"
#include "GDXShaderLayout.h"
#include "RenderPassTargetDesc.h"

#include "GDXMath.h"
#include "BasicMeshGenerator.h"

#include <sstream>

namespace
{
    enum SystemResourceMask : uint64_t
    {
        SR_TRANSFORM = 1ull << 0,
        SR_FRAME = 1ull << 1,
        SR_RENDER_QUEUES = 1ull << 2,
        SR_BACKEND = 1ull << 3,
        SR_STATS = 1ull << 4,
        // Separate bits for main-view vs rtt-view data so that the scheduler
        // can batch RTT tasks and Main tasks in the same parallel group.
        SR_MAIN_VIEW = 1ull << 5,
        SR_RTT_VIEWS = 1ull << 6,
    };

    bool BuildFrameDataFromWorldAndCamera(const WorldTransformComponent& wt, const CameraComponent& cam, FrameData& frame)
    {
        CameraSystem::BuildFrameData(wt, cam, frame);
        return true;
    }

    bool BuildFrameDataFromCameraEntity(Registry& registry, EntityID cameraEntity, FrameData& frame)
    {
        const auto* wt = registry.Get<WorldTransformComponent>(cameraEntity);
        const auto* cam = registry.Get<CameraComponent>(cameraEntity);
        if (!wt || !cam) return false;
        return BuildFrameDataFromWorldAndCamera(*wt, *cam, frame);
    }

    bool BuildFrameDataFromCameraEntityWithOverride(Registry& registry, EntityID cameraEntity, const CameraComponent& cameraOverride, FrameData& frame)
    {
        const auto* wt = registry.Get<WorldTransformComponent>(cameraEntity);
        if (!wt) return false;
        return BuildFrameDataFromWorldAndCamera(*wt, cameraOverride, frame);
    }
    GIDX::Float4x4 BuildBoxWorldMatrix(const GIDX::Float3& center, const GIDX::Float3& scale)
    {
        return GIDX::Multiply(
            GIDX::Scaling(scale.x, scale.y, scale.z),
            GIDX::Translation(center.x, center.y, center.z));
    }

    GIDX::Float3 LerpPoint(const GIDX::Float3& a, const GIDX::Float3& b, float t)
    {
        return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
    }

    bool BuildFrustumCorners(const GIDX::Float4x4& viewProj, GIDX::Float3 outCorners[8])
    {
        const float det = GIDX::Determinant(viewProj);
        if (det == 0.0f)
            return false;

        const GIDX::Float4x4 inv = GIDX::Inverse(viewProj);

        const GIDX::Float3 ndc[8] =
        {
            { -1.0f, -1.0f, 0.0f },
            { -1.0f,  1.0f, 0.0f },
            {  1.0f,  1.0f, 0.0f },
            {  1.0f, -1.0f, 0.0f },
            { -1.0f, -1.0f, 1.0f },
            { -1.0f,  1.0f, 1.0f },
            {  1.0f,  1.0f, 1.0f },
            {  1.0f, -1.0f, 1.0f },
        };

        for (int i = 0; i < 8; ++i)
        {
            const GIDX::Float4 p = GIDX::TransformFloat4(
                { ndc[i].x, ndc[i].y, ndc[i].z, 1.0f }, inv);

            if (std::fabs(p.w) <= 1e-6f)
                return false;

            const float invW = 1.0f / p.w;
            outCorners[i] = { p.x * invW, p.y * invW, p.z * invW };
        }

        return true;
    }


    GIDX::Float4x4 BuildEdgeWorldMatrix(const GIDX::Float3& a, const GIDX::Float3& b, float thickness)
    {
        const GIDX::Float3 center = LerpPoint(a, b, 0.5f);
        const GIDX::Float3 forward = GIDX::Normalize3(GIDX::Subtract(b, a), { 0.0f, 0.0f, 1.0f });
        GIDX::Float3 upRef = { 0.0f, 1.0f, 0.0f };
        if (std::fabs(GIDX::Dot3(forward, upRef)) > 0.98f)
            upRef = { 1.0f, 0.0f, 0.0f };
        const GIDX::Float3 right = GIDX::Normalize3(GIDX::Cross(upRef, forward), { 1.0f, 0.0f, 0.0f });
        const GIDX::Float3 up = GIDX::Normalize3(GIDX::Cross(forward, right), { 0.0f, 1.0f, 0.0f });
        const float len = (std::max)(GIDX::Length3(GIDX::Subtract(b, a)), 0.01f);

        GIDX::Float4x4 m{};
        m._11 = right.x * thickness; m._12 = right.y * thickness; m._13 = right.z * thickness; m._14 = 0.0f;
        m._21 = up.x * thickness;    m._22 = up.y * thickness;    m._23 = up.z * thickness;    m._24 = 0.0f;
        m._31 = forward.x * (len * 0.5f); m._32 = forward.y * (len * 0.5f); m._33 = forward.z * (len * 0.5f); m._34 = 0.0f;
        m._41 = center.x; m._42 = center.y; m._43 = center.z; m._44 = 1.0f;
        return m;
    }

    ResourceBindingSet BuildDebugBindings(const MaterialResource& mat, const GDXShaderResource& shader)
    {
        ResourceBindingSet set;

        for (uint32_t i = 0; i < shader.layout.constantBufferCount; ++i)
        {
            const auto& src = shader.layout.constantBuffers[i];
            ConstantBufferBindingDesc cb{};
            cb.semantic = src.slot;
            cb.vsRegister = src.vsRegister;
            cb.psRegister = src.psRegister;
            cb.buffer = (src.slot == GDXShaderConstantBufferSlot::Material) ? mat.gpuConstantBuffer : nullptr;
            cb.enabled = (src.slot != GDXShaderConstantBufferSlot::Material) || (mat.gpuConstantBuffer != nullptr);
            cb.scope = (src.slot == GDXShaderConstantBufferSlot::Frame) ? ResourceBindingScope::Pass :
                ((src.slot == GDXShaderConstantBufferSlot::Material) ? ResourceBindingScope::Material : ResourceBindingScope::Draw);
            set.AddConstantBufferBinding(cb);
        }

        for (uint32_t i = 0; i < shader.layout.textureBindingCount; ++i)
        {
            const auto& src = shader.layout.textureBindings[i];
            ShaderResourceBindingDesc desc{};
            switch (src.semantic)
            {
            case GDXShaderTextureSemantic::Albedo:   desc.semantic = ShaderResourceSemantic::Albedo; break;
            case GDXShaderTextureSemantic::Normal:   desc.semantic = ShaderResourceSemantic::Normal; break;
            case GDXShaderTextureSemantic::ORM:      desc.semantic = ShaderResourceSemantic::ORM; break;
            case GDXShaderTextureSemantic::Emissive: desc.semantic = ShaderResourceSemantic::Emissive; break;
            case GDXShaderTextureSemantic::Detail:   desc.semantic = ShaderResourceSemantic::Detail; break;
            case GDXShaderTextureSemantic::ShadowMap:desc.semantic = ShaderResourceSemantic::ShadowMap; break;
            }
            desc.bindingIndex = src.shaderRegister;
            desc.enabled = false;
            desc.requiredState = ResourceState::ShaderRead;
            desc.scope = (desc.semantic == ShaderResourceSemantic::ShadowMap) ? ResourceBindingScope::Pass : ResourceBindingScope::Material;
            set.AddTextureBinding(desc);
        }

        return set;
    }

}
namespace
{
    bool FrameGraphHasDependency(const RFG::Node& node, uint32_t dependency)
    {
        for (uint32_t existing : node.dependencies)
            if (existing == dependency) return true;
        return false;
    }

    void FrameGraphAddDependency(RFG::Node& node, uint32_t dependency)
    {
        if (!FrameGraphHasDependency(node, dependency))
            node.dependencies.push_back(dependency);
    }
}

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
    ctx.matStore     = &m_matStore;
    ctx.shaderStore  = &m_shaderStore;
    ctx.defaultShader = m_defaultShader;
    ctx.frameNumber  = m_frameNumber;
    m_debugCulling.AppendVisibleSet(queue, set, view, ctx, viewStats);
}

void GDXECSRenderer::LogDebugCullingStats() const
{
    m_debugCulling.LogStats(m_renderPipeline.mainView.stats,
        [&]() -> std::vector<RFG::ViewStats> {
            std::vector<RFG::ViewStats> v;
            v.reserve(m_renderPipeline.rttViews.size());
            for (const auto& rv : m_renderPipeline.rttViews) v.push_back(rv.stats);
            return v;
        }(),
        m_frameNumber);
}

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
    m_defaultWhiteTex = defaults.white;
    m_defaultNormalTex = defaults.normal;
    m_defaultORMTex = defaults.orm;
    m_defaultBlackTex = defaults.black;

    if (!LoadDefaultShaders()) return false;

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
    const std::wstring& vsFile,
    const std::wstring& psFile,
    uint32_t vertexFlags,
    const GDXShaderLayout& layout)
{
    return m_shaderCache.LoadShader(vsFile, psFile, vertexFlags, layout);
}

ShaderHandle GDXECSRenderer::LoadShaderInternal(
    const std::wstring& vsFile,
    const std::wstring& psFile,
    uint32_t vertexFlags,
    const std::wstring& debugName,
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


TextureHandle GDXECSRenderer::CreateTexture(const ImageBuffer& image, const std::wstring& debugName, bool isSRGB)
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
    // Bounds VOR dem Move berechnen — danach ist mesh leer.
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

RenderTargetHandle GDXECSRenderer::CreateRenderTarget(uint32_t w, uint32_t h, const std::wstring& name,
    GDXTextureFormat colorFormat)
{
    if (!m_backend) return RenderTargetHandle::Invalid();
    return m_backend->CreateRenderTarget(m_rtStore, m_texStore, w, h, name, colorFormat);
}

TextureHandle GDXECSRenderer::GetRenderTargetTexture(RenderTargetHandle h)
{
    if (auto* rt = m_rtStore.Get(h))
        return rt->exposedTexture;
    return m_defaultWhiteTex;
}

PostProcessHandle GDXECSRenderer::CreatePostProcessPass(const PostProcessPassDesc& desc)
{
    if (!m_backend) return PostProcessHandle::Invalid();
    PostProcessHandle h = m_backend->CreatePostProcessPass(m_postProcessStore, desc);
    if (h.IsValid())
        m_postProcessPassOrder.push_back(h);
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
    m_postProcessPassOrder.clear();
}

void GDXECSRenderer::SetShadowMapSize(uint32_t size)
{
    if (m_backend)
        m_backend->SetShadowMapSize(size);
}

bool GDXECSRenderer::SupportsTextureFormat(GDXTextureFormat format) const
{
    return m_backend ? m_backend->SupportsTextureFormat(format) : false;
}

void GDXECSRenderer::SetClearColor(float r, float g, float b, float a)
{
    m_clearColor[0] = r;
    m_clearColor[1] = g;
    m_clearColor[2] = b;
    m_clearColor[3] = a;
}

void GDXECSRenderer::CaptureFrameSnapshot(FrameData& outFrame)
{
    m_framePhase = RenderFramePhase::FreezeSnapshot;

    FrameData snapshot{};
    m_persistentFrameState.ApplyTo(snapshot);

    m_cameraSystem.Update(m_registry, snapshot);

    // Lichtdaten und Shadow-Setup werden bewusst in den eingefrorenen Snapshot geschrieben.
    if (m_backend)
        m_backend->UpdateLights(m_registry, snapshot);
    else
    {
        snapshot.lightCount = 0u;
        snapshot.hasShadowPass = false;
        snapshot.shadowCasterMask = 0xFFFFFFFFu;
        snapshot.lightAffectMask = 0xFFFFFFFFu;
    }

    outFrame = snapshot;
    m_frameData = snapshot;
}

void GDXECSRenderer::PrepareMainViewData(const FrameData& frameSnapshot, RFG::ViewPassData& outView)
{
    outView.Reset();

    outView.prepared.frame = frameSnapshot;

    outView.prepared.graphicsView = {};
    outView.prepared.graphicsView.type = RenderViewType::Main;
    outView.prepared.graphicsView.frame = outView.prepared.frame;
    outView.prepared.graphicsView.renderTarget = RenderTargetHandle{};
    outView.prepared.graphicsView.forbiddenShaderReadTexture = TextureHandle{};
    outView.prepared.graphicsView.gatherOpaque = true;
    outView.prepared.graphicsView.gatherTransparent = true;
    outView.prepared.graphicsView.gatherShadows = false;
    outView.prepared.graphicsView.skipSelfReferentialDraws = false;
    outView.prepared.graphicsView.visibilityLayerMask = outView.prepared.frame.cullMask;
    outView.prepared.graphicsView.shadowCasterLayerMask = outView.prepared.frame.shadowCasterMask;

    outView.prepared.gatherOptions = {};
    outView.prepared.gatherOptions.gatherOpaque = true;
    outView.prepared.gatherOptions.gatherTransparent = true;
    outView.prepared.gatherOptions.gatherShadows = false;
    outView.prepared.gatherOptions.skipSelfReferentialDraws = false;
    outView.prepared.gatherOptions.forbiddenShaderReadTexture = TextureHandle{};
    outView.prepared.gatherOptions.visibilityLayerMask = outView.prepared.frame.cullMask;
    outView.prepared.gatherOptions.shadowCasterLayerMask = outView.prepared.frame.shadowCasterMask;

    outView.prepared.clearDesc = {};
    outView.prepared.graphicsTargetDesc = RenderPassTargetDesc::Backbuffer(
        outView.prepared.frame.viewportWidth,
        outView.prepared.frame.viewportHeight);

    outView.prepared.shadowEnabled =
        outView.prepared.frame.hasShadowPass &&
        m_backend &&
        m_backend->HasShadowResources();

    outView.prepared.shadowView = {};
    if (outView.prepared.shadowEnabled)
    {
        outView.prepared.shadowView = outView.prepared.graphicsView;
        outView.prepared.shadowView.type = RenderViewType::Shadow;
        outView.prepared.shadowView.gatherOpaque = false;
        outView.prepared.shadowView.gatherTransparent = false;
        outView.prepared.shadowView.gatherShadows = true;
    }
}

void GDXECSRenderer::PrepareRenderTargetViewData(
    const FrameData& frameSnapshot,
    std::vector<RFG::ViewPassData>& outViews)
{
    outViews.clear();

    m_registry.View<CameraComponent, RenderTargetCameraComponent>(
        [this, &outViews, &frameSnapshot](EntityID entity, CameraComponent& cam, RenderTargetCameraComponent& rtCam)
        {
            if (!m_backend || !rtCam.enabled || !rtCam.target.IsValid())
                return;

            GDXRenderTargetResource* rt = m_rtStore.Get(rtCam.target);
            if (!rt || !rt->ready)
                return;

            CameraComponent cameraForView = cam;
            if (rtCam.autoAspectFromTarget && rt->height > 0u)
                cameraForView.aspectRatio = static_cast<float>(rt->width) / static_cast<float>(rt->height);

            RFG::ViewPassData preparedView{};
            preparedView.prepared.frame = frameSnapshot;
            preparedView.prepared.frame.viewportWidth = static_cast<float>(rt->width);
            preparedView.prepared.frame.viewportHeight = static_cast<float>(rt->height);

            const bool built = BuildFrameDataFromCameraEntityWithOverride(m_registry, entity, cameraForView, preparedView.prepared.frame);

            if (!built)
                return;

            // Kaskaden-Splits für das RTT-Frustum neu berechnen.
            // Der Snapshot enthält die Kaskaden der Main-Kamera — für die RTT-Kamera
            // müssen sie auf Basis des RTT-Frustums (viewProjMatrix) neu berechnet werden.
            // Ohne diesen Aufruf werden Shadow-Caster außerhalb des Main-Frustums
            // im RTT-Bild fälschlicherweise gecutlt.
            if (preparedView.prepared.frame.hasShadowPass && m_backend)
                m_backend->UpdateLights(m_registry, preparedView.prepared.frame);

            preparedView.prepared.graphicsView = {};
            preparedView.prepared.graphicsView.type = RenderViewType::RenderTarget;
            // frame NACH UpdateLights zuweisen — enthält jetzt RTT-Kaskaden
            preparedView.prepared.graphicsView.frame = preparedView.prepared.frame;
            preparedView.prepared.graphicsView.renderTarget = rtCam.target;
            preparedView.prepared.graphicsView.forbiddenShaderReadTexture = rt->exposedTexture;
            preparedView.prepared.graphicsView.gatherOpaque = rtCam.renderOpaque;
            preparedView.prepared.graphicsView.gatherTransparent = rtCam.renderTransparent;
            preparedView.prepared.graphicsView.gatherShadows = false;
            preparedView.prepared.graphicsView.skipSelfReferentialDraws = rtCam.skipSelfReferentialDraws;
            preparedView.prepared.graphicsView.visibilityLayerMask = preparedView.prepared.frame.cullMask;
            preparedView.prepared.graphicsView.shadowCasterLayerMask = preparedView.prepared.frame.shadowCasterMask;

            preparedView.prepared.gatherOptions = {};
            preparedView.prepared.gatherOptions.gatherOpaque = rtCam.renderOpaque;
            preparedView.prepared.gatherOptions.gatherTransparent = rtCam.renderTransparent;
            preparedView.prepared.gatherOptions.gatherShadows = false;
            preparedView.prepared.gatherOptions.skipSelfReferentialDraws = rtCam.skipSelfReferentialDraws;
            preparedView.prepared.gatherOptions.forbiddenShaderReadTexture = rt->exposedTexture;
            preparedView.prepared.gatherOptions.visibilityLayerMask = preparedView.prepared.frame.cullMask;
            preparedView.prepared.gatherOptions.shadowCasterLayerMask = preparedView.prepared.frame.shadowCasterMask;

            preparedView.prepared.clearDesc = rtCam.clear;
            preparedView.prepared.graphicsTargetDesc = RenderPassTargetDesc::Offscreen(
                rtCam.target,
                rtCam.clear,
                static_cast<float>(rt->width),
                static_cast<float>(rt->height),
                rt->debugName);

            preparedView.prepared.shadowEnabled =
                preparedView.prepared.frame.hasShadowPass &&
                rtCam.renderShadows &&
                m_backend &&
                m_backend->HasShadowResources();

            preparedView.prepared.shadowView = {};
            if (preparedView.prepared.shadowEnabled)
            {
                preparedView.prepared.shadowView = preparedView.prepared.graphicsView;
                preparedView.prepared.shadowView.type = RenderViewType::Shadow;
                preparedView.prepared.shadowView.gatherOpaque = false;
                preparedView.prepared.shadowView.gatherTransparent = false;
                preparedView.prepared.shadowView.gatherShadows = true;
                // Shadow-Caster für RTT nicht per Frustum cullen —
                // der Licht-Frustum deckt die RTT-Szene möglicherweise nicht vollständig ab.
                // Alle Objekte mit castShadows=true werden gerendert.
                preparedView.prepared.shadowView.enableFrustumCulling = false;
            }

            outViews.push_back(std::move(preparedView));
        });
}

// ---------------------------------------------------------------------------
// Fix 3 — Single-view core methods.
// Enthalten die eigentliche Logik; Main- und RTT-Pfad rufen sie auf.
// js = nullptr → seriell (RTT inner loop), js = &m_jobSystem → parallel (Main).
// ---------------------------------------------------------------------------

void GDXECSRenderer::CullViewGraphics(RFG::ViewPassData& view, JobSystem* js)
{
    view.graphicsVisibleSet = {};
    m_viewCullingSystem.BuildVisibleSet(m_registry, view.prepared.graphicsView, view.graphicsVisibleSet, js);
    view.stats.graphicsCulling = view.graphicsVisibleSet.stats;
}

void GDXECSRenderer::CullViewShadow(RFG::ViewPassData& view, JobSystem* js)
{
    view.shadowVisibleSet = {};
    view.stats.shadowCulling = {};
    if (!view.prepared.shadowEnabled)
        return;
    m_viewCullingSystem.BuildVisibleSet(m_registry, view.prepared.shadowView, view.shadowVisibleSet, js);
    view.stats.shadowCulling = view.shadowVisibleSet.stats;
}

void GDXECSRenderer::GatherViewGraphics(const RenderGatherSystem::ShaderResolver& rs,
                                         RFG::ViewPassData& view, JobSystem* js)
{
    view.graphicsGatherChunks.clear();
    m_gatherSystem.GatherVisibleSetChunks(view.graphicsVisibleSet, view.prepared.frame,
        m_meshStore, m_matStore, m_shaderStore,
        rs, view.graphicsGatherChunks,
        &view.prepared.gatherOptions, js);
}

void GDXECSRenderer::GatherViewShadow(const RenderGatherSystem::ShaderResolver& rs,
                                       RFG::ViewPassData& view, JobSystem* js)
{
    view.shadowGatherChunks.clear();
    if (!view.prepared.shadowEnabled)
        return;

    RenderGatherOptions shadowOpts = view.prepared.gatherOptions;
    shadowOpts.gatherOpaque      = false;
    shadowOpts.gatherTransparent = false;
    shadowOpts.gatherShadows     = true;

    m_gatherSystem.GatherShadowVisibleSetChunks(view.shadowVisibleSet, view.prepared.frame,
        m_meshStore, m_matStore, m_shaderStore,
        rs, view.shadowGatherChunks,
        &shadowOpts, js);
}

// ---------------------------------------------------------------------------
// RTT loops — parallele Iteration über alle RTT-Views.
// Inner-Job-System ist nullptr (kein nested ParallelFor).
// ---------------------------------------------------------------------------

void GDXECSRenderer::CullPreparedRenderTargetViews(std::vector<RFG::ViewPassData>& views)
{
    m_jobSystem.ParallelFor(views.size(), [&](size_t begin, size_t end)
    {
        for (size_t vi = begin; vi < end; ++vi)
        {
            RFG::ViewPassData& v = views[vi];
            const GDXRenderTargetResource* rt = m_rtStore.Get(v.prepared.graphicsView.renderTarget);
            if (!rt || !rt->ready) continue;
            CullViewGraphics(v, nullptr);
            CullViewShadow(v, nullptr);
        }
    }, 1u);
}

void GDXECSRenderer::GatherPreparedRenderTargetViews(const RenderGatherSystem::ShaderResolver& rs,
                                                      std::vector<RFG::ViewPassData>& views)
{
    m_jobSystem.ParallelFor(views.size(), [&](size_t begin, size_t end)
    {
        for (size_t vi = begin; vi < end; ++vi)
        {
            RFG::ViewPassData& v = views[vi];
            const GDXRenderTargetResource* rt = m_rtStore.Get(v.prepared.graphicsView.renderTarget);
            if (!rt || !rt->ready) continue;
            GatherViewGraphics(rs, v, nullptr);
            GatherViewShadow(rs, v, nullptr);
        }
    }, 1u);
}

// ---------------------------------------------------------------------------
// Main view — ein View, nutzt &m_jobSystem für innere Parallelität.
// ---------------------------------------------------------------------------

void GDXECSRenderer::CullPreparedMainView(RFG::ViewPassData& view)
{
    m_framePhase = RenderFramePhase::VisibilityBuild;
    CullViewGraphics(view, &m_jobSystem);
    CullViewShadow(view, &m_jobSystem);
}

void GDXECSRenderer::GatherPreparedMainView(const RenderGatherSystem::ShaderResolver& rs,
                                             RFG::ViewPassData& view)
{
    GatherViewGraphics(rs, view, &m_jobSystem);
    GatherViewShadow(rs, view, &m_jobSystem);
}

void GDXECSRenderer::BuildPreparedMainViewExecuteInputs(RFG::ViewPassData& preparedView)
{
    ConfigurePreparedCommonExecuteInputs(preparedView, true);
    BuildPreparedShadowPassExecuteInput(preparedView);
    if (!PrepareMainViewPostProcessPresentation(preparedView))
        BuildPreparedGraphicsPassExecuteInput(preparedView, preparedView.prepared.graphicsTargetDesc, true, true);
    BuildPreparedExecutionQueues(preparedView);
}

void GDXECSRenderer::FinalizePreparedViewQueues(RFG::ViewPassData& preparedView)
{
    m_gatherSystem.MergeVisibleSetChunks(preparedView.graphicsGatherChunks, preparedView.opaqueQueue, preparedView.transparentQueue);
    RenderGatherSystem::SortRenderQueue(preparedView.opaqueQueue);
    RenderGatherSystem::SortRenderQueue(preparedView.transparentQueue);

    if (preparedView.prepared.shadowEnabled)
    {
        m_gatherSystem.MergeShadowVisibleSetChunks(preparedView.shadowGatherChunks, preparedView.shadowQueue);
        RenderGatherSystem::SortRenderQueue(preparedView.shadowQueue);
    }
    else
    {
        preparedView.shadowQueue.Clear();
    }
}

void GDXECSRenderer::FinalizePreparedRenderTargetQueues(std::vector<RFG::ViewPassData>& preparedViews)
{
    // Each view writes only to its own opaqueQueue / transparentQueue / shadowQueue.
    // FinalizePreparedViewQueues has no shared mutable state — safe to parallelize.
    const size_t viewCount = preparedViews.size();
    auto process = [&](size_t begin, size_t end)
        {
            for (size_t vi = begin; vi < end; ++vi)
            {
                RFG::ViewPassData& preparedView = preparedViews[vi];
                GDXRenderTargetResource* rt = m_rtStore.Get(preparedView.prepared.graphicsView.renderTarget);
                if (!rt || !rt->ready)
                    continue;

                FinalizePreparedViewQueues(preparedView);
            }
        };

    m_jobSystem.ParallelFor(viewCount, process, 1u);
}

void GDXECSRenderer::ConfigurePreparedCommonExecuteInputs(RFG::ViewPassData& preparedView, bool presentAfterExecute)
{
    preparedView.execute.Reset();
    // FrameData einfrieren — Execute liest ausschließlich aus execute.frame,
    // nie aus preparedView.prepared.frame oder node.view->prepared.frame.
    preparedView.execute.frame = preparedView.prepared.frame;
    preparedView.execute.presentation.presentAfterExecute = presentAfterExecute;
}

bool GDXECSRenderer::PrepareMainViewPostProcessPresentation(RFG::ViewPassData& preparedView)
{
    const bool hasPostProcess = !m_postProcessPassOrder.empty();
    if (!hasPostProcess)
        return false;

    const uint32_t targetWidth = static_cast<uint32_t>(preparedView.prepared.frame.viewportWidth > 1.0f ? preparedView.prepared.frame.viewportWidth : 1.0f);
    const uint32_t targetHeight = static_cast<uint32_t>(preparedView.prepared.frame.viewportHeight > 1.0f ? preparedView.prepared.frame.viewportHeight : 1.0f);
    const bool needsNewTarget = !m_mainScenePostProcessTarget.IsValid();
    GDXRenderTargetResource* existing = needsNewTarget ? nullptr : m_rtStore.Get(m_mainScenePostProcessTarget);
    if (needsNewTarget || !existing || existing->width != targetWidth || existing->height != targetHeight)
        m_mainScenePostProcessTarget = CreateRenderTarget(targetWidth, targetHeight, L"MainScenePostProcess", GDXTextureFormat::RGBA16_FLOAT);

    if (!m_mainScenePostProcessTarget.IsValid())
        return false;

    preparedView.execute.presentation.postProcess.enabled = true;
    preparedView.execute.presentation.postProcess.sceneTexture = GetRenderTargetTexture(m_mainScenePostProcessTarget);
    BuildPreparedGraphicsPassExecuteInput(
        preparedView,
        RenderPassTargetDesc::Offscreen(
            m_mainScenePostProcessTarget,
            preparedView.prepared.clearDesc,
            preparedView.prepared.frame.viewportWidth,
            preparedView.prepared.frame.viewportHeight,
            L"MainScenePostProcess"),
        true,
        true);
    return true;
}

void GDXECSRenderer::BuildPreparedRenderTargetExecuteInputs(std::vector<RFG::ViewPassData>& preparedViews)
{
    for (auto& preparedView : preparedViews)
    {
        ConfigurePreparedCommonExecuteInputs(preparedView, false);

        GDXRenderTargetResource* rt = m_rtStore.Get(preparedView.prepared.graphicsView.renderTarget);
        if (!rt || !rt->ready)
            continue;

        BuildPreparedShadowPassExecuteInput(preparedView);
        BuildPreparedGraphicsPassExecuteInput(preparedView, preparedView.prepared.graphicsTargetDesc, true, preparedView.prepared.shadowEnabled);
        BuildPreparedExecutionQueues(preparedView);
    }
}

void GDXECSRenderer::BuildPreparedShadowPassExecuteInput(RFG::ViewPassData& preparedView)
{
    preparedView.execute.shadowPass.Reset();
    preparedView.execute.shadowPass.enabled = preparedView.prepared.shadowEnabled && !preparedView.shadowQueue.Empty();
    if (!preparedView.execute.shadowPass.enabled)
        return;

    preparedView.execute.shadowPass.desc = BackendRenderPassDesc::Shadow(preparedView.prepared.frame);
}

void GDXECSRenderer::BuildPreparedGraphicsPassExecuteInput(
    RFG::ViewPassData& preparedView,
    const RenderPassTargetDesc& targetDesc,
    bool appendGraphicsVisibleSet,
    bool appendShadowVisibleSet)
{
    preparedView.execute.graphicsPass.Reset();
    preparedView.execute.graphicsPass.enabled = true;
    preparedView.execute.graphicsPass.desc = BackendRenderPassDesc::Graphics(
        targetDesc,
        &preparedView.prepared.frame,
        RenderPass::Opaque);
    preparedView.execute.graphicsPass.appendGraphicsVisibleSet = appendGraphicsVisibleSet;
    preparedView.execute.graphicsPass.appendShadowVisibleSet = appendShadowVisibleSet;
    preparedView.execute.graphicsPass.sortQueueBeforeExecute = true;
}

void GDXECSRenderer::BuildPreparedExecutionQueues(RFG::ViewPassData& preparedView)
{
    preparedView.execute.shadowQueue = preparedView.shadowQueue;
    preparedView.execute.graphicsQueue = preparedView.BuildGraphicsQueue();

    const RFG::PassExec& passExecution = preparedView.execute.graphicsPass;
    if (passExecution.appendGraphicsVisibleSet)
        AppendDebugVisibleSet(preparedView.execute.graphicsQueue, preparedView.graphicsVisibleSet, preparedView.prepared.graphicsView, &preparedView.stats);

    if (passExecution.appendShadowVisibleSet && preparedView.prepared.shadowEnabled)
        AppendDebugVisibleSet(preparedView.execute.graphicsQueue, preparedView.shadowVisibleSet, preparedView.prepared.shadowView, &preparedView.stats);

    if (passExecution.sortQueueBeforeExecute)
        preparedView.execute.graphicsQueue.Sort();
}

void GDXECSRenderer::FinalizePreparedFrameQueues(RFG::PipelineData& pipeline)
{
    FinalizePreparedRenderTargetQueues(pipeline.rttViews);
    FinalizePreparedViewQueues(pipeline.mainView);  // direkt — kein Wrapper mehr nötig
}

void GDXECSRenderer::BuildPreparedFrameExecuteInputs(RFG::PipelineData& pipeline)
{
    BuildPreparedRenderTargetExecuteInputs(pipeline.rttViews);
    BuildPreparedMainViewExecuteInputs(pipeline.mainView);
}

// ---------------------------------------------------------------------------
// Frame Graph — Build + Execute delegiert an GDXRenderFrameGraph.
// ---------------------------------------------------------------------------

void GDXECSRenderer::AggregatePreparedFrameStats(
    const RFG::ViewPassData& mainView,
    const std::vector<RFG::ViewPassData>& rttViews)
{
    m_stats = {};
    m_stats.drawCalls       = mainView.stats.drawCalls;
    m_stats.renderCommands  = mainView.stats.renderCommands;
    m_stats.lightCount      = mainView.stats.lightCount;
    m_stats.mainCulling     = mainView.stats.graphicsCulling;
    m_stats.shadowCulling   = mainView.stats.shadowCulling;
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
    constexpr size_t kApproxFrameConstantsBytes = 272u + (4u * 64u + 32u);
    constexpr size_t kApproxEntityConstantsBytes = 128u;
    (void)frameTransient.uploadArena.Allocate(kApproxFrameConstantsBytes, 16u);
    (void)frameTransient.uploadArena.Allocate(kApproxEntityConstantsBytes *
        (preparedView.opaqueQueue.Count() + preparedView.transparentQueue.Count() +
         preparedView.shadowQueue.Count()), 16u);
}

void GDXECSRenderer::BeginFrame()
{
    m_framePhase = RenderFramePhase::UpdateWrite;
    m_currentFrameIndex = (m_currentFrameIndex + 1u) % GDXMaxFramesInFlight;
    m_persistentFrameState.ApplyTo(m_frameData);
    m_frameContexts[m_currentFrameIndex].Begin(m_currentFrameIndex, ++m_frameNumber, &m_frameData);
    m_frameTransients[m_currentFrameIndex].BeginFrame();

    m_stats = {};

    if (m_backend)
        m_backend->BeginFrame(m_clearColor);
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

    m_renderPipeline.Reset();

    m_systemScheduler.Clear();
    m_systemScheduler.AddTask({ "Transform",
        0ull,
        SR_TRANSFORM,
        [this]() { m_transformSystem.Update(m_registry, &m_jobSystem); } });
    m_systemScheduler.AddTask({ "Capture Frame Snapshot",
        SR_TRANSFORM | SR_FRAME,
        SR_FRAME | SR_STATS,
        [this]() { CaptureFrameSnapshot(m_renderPipeline.frameSnapshot); } });
    // Prepare Main View and Prepare RTT Views write to separate pipeline members
    // (mainView vs rttViews) and can run in parallel once the frame snapshot is ready.
    m_systemScheduler.AddTask({ "Prepare Main View",
        SR_FRAME | SR_STATS,
        SR_MAIN_VIEW,
        [this]() { PrepareMainViewData(m_renderPipeline.frameSnapshot, m_renderPipeline.mainView); } });
    m_systemScheduler.AddTask({ "Prepare RTT Views",
        SR_FRAME | SR_STATS,
        SR_RTT_VIEWS,
        [this]() { PrepareRenderTargetViewData(m_renderPipeline.frameSnapshot, m_renderPipeline.rttViews); } });
    // RTT und Main können parallel laufen sobald der Snapshot bereit ist.
    // Cull+Gather jeweils zusammengefasst — ein Task pro View-Gruppe.
    m_systemScheduler.AddTask({ "Cull+Gather RTT",
        SR_RTT_VIEWS | SR_TRANSFORM,
        SR_RTT_VIEWS,
        [this, &resolveShader]()
        {
            CullPreparedRenderTargetViews(m_renderPipeline.rttViews);
            GatherPreparedRenderTargetViews(resolveShader, m_renderPipeline.rttViews);
        } });
    m_systemScheduler.AddTask({ "Cull+Gather Main",
        SR_MAIN_VIEW | SR_TRANSFORM,
        SR_MAIN_VIEW,
        [this, &resolveShader]()
        {
            CullPreparedMainView(m_renderPipeline.mainView);
            GatherPreparedMainView(resolveShader, m_renderPipeline.mainView);
        } });
    // Finalize and subsequent stages consume both SR_MAIN_VIEW and SR_RTT_VIEWS.
    m_systemScheduler.AddTask({ "Finalize Frame Queues",
        SR_MAIN_VIEW | SR_RTT_VIEWS | SR_TRANSFORM,
        SR_RENDER_QUEUES,
        [this]() { FinalizePreparedFrameQueues(m_renderPipeline); } });
    m_systemScheduler.AddTask({ "Build Frame Execute Inputs",
        SR_RENDER_QUEUES | SR_BACKEND,
        SR_RENDER_QUEUES,
        [this]() { BuildPreparedFrameExecuteInputs(m_renderPipeline); } });
    m_systemScheduler.AddTask({ "Build Frame Graph",
        SR_RENDER_QUEUES | SR_BACKEND,
        SR_RENDER_QUEUES,
        [this]()
        {
            GDXRenderFrameGraph::BuildContext bctx{};
            bctx.rtStore = &m_rtStore;
            bctx.mainScenePostProcessTarget = m_mainScenePostProcessTarget;
            m_frameGraph.Build(m_renderPipeline, bctx);
        } });
    m_systemScheduler.AddTask({ "Execute Frame Graph",
        SR_RENDER_QUEUES | SR_MAIN_VIEW | SR_RTT_VIEWS | SR_TRANSFORM | SR_BACKEND,
        SR_BACKEND | SR_STATS,
        [this]()
        {
            RFG::ExecContext ectx{};
            ectx.backend             = m_backend.get();
            ectx.registry            = &m_registry;
            ectx.meshStore           = &m_meshStore;
            ectx.matStore            = &m_matStore;
            ectx.shaderStore         = &m_shaderStore;
            ectx.texStore            = &m_texStore;
            ectx.rtStore             = &m_rtStore;
            ectx.postProcessStore    = &m_postProcessStore;
            ectx.postProcessPassOrder = &m_postProcessPassOrder;
            m_frameGraph.Execute(m_renderPipeline, ectx);

            m_renderPipeline.mainView.stats.drawCalls = m_backend ? m_backend->GetDrawCallCount() : 0u;
            m_renderPipeline.mainView.stats.renderCommands =
                m_renderPipeline.mainView.execute.graphicsPass.enabled
                ? static_cast<uint32_t>(m_renderPipeline.mainView.execute.graphicsQueue.Count()) : 0u;
            m_renderPipeline.mainView.stats.lightCount = m_renderPipeline.mainView.execute.frame.lightCount;

            UpdatePreparedMainViewFrameTransient(m_renderPipeline.mainView);
            AggregatePreparedFrameStats(m_renderPipeline.mainView, m_renderPipeline.rttViews);
            LogDebugCullingStats();

            m_framePhase = RenderFramePhase::ExecuteSubmit;
            if (m_backend && m_renderPipeline.mainView.execute.presentation.presentAfterExecute)
                m_backend->Present(true);
        } });
    // NOTE: nullptr is intentional. The tasks dispatched by this scheduler
    // (Cull/Gather RTT/Main) already call m_jobSystem.ParallelFor internally.
    // Passing &m_jobSystem here would cause nested ParallelFor calls into the same
    // JobSystem which has a single shared m_currentFn slot — guaranteed deadlock.
    // Parallelism happens at the intra-task level (each task fans out via ParallelFor),
    // not at the inter-task level (scheduler batch).
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
            {
                cam.aspectRatio = aspect;
            });
    }

    m_persistentFrameState.viewportWidth = static_cast<float>(w);
    m_persistentFrameState.viewportHeight = static_cast<float>(h);
    m_frameData.viewportWidth = static_cast<float>(w);
    m_frameData.viewportHeight = static_cast<float>(h);
}

void GDXECSRenderer::Shutdown()
{
    if (!m_initialized && !m_backend) return;

    if (m_backend)
    {
        m_backend->DestroyPostProcessPasses(m_postProcessStore);
        m_backend->Shutdown(m_matStore, m_shaderStore, m_texStore);
        m_backend.reset();
    }

    m_shaderCache.Clear();
    m_postProcessPassOrder.clear();
    m_initialized = false;
}