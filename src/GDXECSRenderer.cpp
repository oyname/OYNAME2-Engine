#include "GDXECSRenderer.h"
#include "GDXRenderTargetResource.h"
#include "Debug.h"
#include "GDXShaderLayout.h"
#include "RenderPassTargetDesc.h"

#include "GDXMath.h"
#include "BasicMeshGenerator.h"

#include <sstream>

namespace
{
    constexpr uint32_t kRelevantMainFeatures = SVF_SKINNED | SVF_VERTEX_COLOR;
    constexpr uint32_t kRelevantShadowFeatures = SVF_SKINNED | SVF_ALPHA_TEST;

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
        // Kameraposition: Zeile 3 der Weltmatrix (row-vector Konvention)
        const GIDX::Float3 position = { wt.matrix._41, wt.matrix._42, wt.matrix._43 };
        frame.cameraPos = position;
        frame.cullMask  = cam.cullMask;

        // Rotationsteil: Translation auf null setzen
        GIDX::Float4x4 rot = wt.matrix;
        rot._41 = 0.0f;  rot._42 = 0.0f;  rot._43 = 0.0f;  rot._44 = 1.0f;

        const GIDX::Float3 forward = GIDX::Normalize3(GIDX::TransformVector({ 0.0f, 0.0f, 1.0f }, rot));
        const GIDX::Float3 up      = GIDX::Normalize3(GIDX::TransformVector({ 0.0f, 1.0f, 0.0f }, rot));
        frame.cameraForward = forward;
        const GIDX::Float3 target = GIDX::Add(position, forward);

        const GIDX::Float4x4 view = GIDX::LookAtLH(position, target, up);
        frame.viewMatrix = view;

        GIDX::Float4x4 proj;
        if (cam.isOrtho)
        {
            proj = GIDX::OrthographicLH(cam.orthoWidth, cam.orthoHeight, cam.nearPlane, cam.farPlane);
        }
        else
        {
            const float fovRad = GIDX::ToRadians(cam.fovDeg);
            proj = GIDX::PerspectiveFovLH(fovRad, cam.aspectRatio, cam.nearPlane, cam.farPlane);
        }

        frame.projMatrix     = proj;
        frame.viewProjMatrix = GIDX::Multiply(view, proj);
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
    bool FrameGraphResourceIdentityEquals(const PreparedFrameGraphResourceRef& a, const PreparedFrameGraphResourceRef& b)
    {
        return a.kind == b.kind && a.texture == b.texture && a.renderTarget == b.renderTarget && a.scopeId == b.scopeId;
    }

    PreparedFrameGraphResourceRef MakeFrameGraphReadResource(
        PreparedFrameGraphResourceKind kind,
        TextureHandle texture = TextureHandle::Invalid(),
        RenderTargetHandle renderTarget = RenderTargetHandle::Invalid(),
        uint32_t scopeId = 0u)
    {
        PreparedFrameGraphResourceRef ref{};
        ref.kind = kind;
        ref.access = PreparedFrameGraphResourceAccess::Read;
        ref.texture = texture;
        ref.renderTarget = renderTarget;
        ref.scopeId = scopeId;
        return ref;
    }

    PreparedFrameGraphResourceRef MakeFrameGraphWriteResource(
        PreparedFrameGraphResourceKind kind,
        TextureHandle texture = TextureHandle::Invalid(),
        RenderTargetHandle renderTarget = RenderTargetHandle::Invalid(),
        uint32_t scopeId = 0u)
    {
        PreparedFrameGraphResourceRef ref{};
        ref.kind = kind;
        ref.access = PreparedFrameGraphResourceAccess::Write;
        ref.texture = texture;
        ref.renderTarget = renderTarget;
        ref.scopeId = scopeId;
        return ref;
    }

    bool FrameGraphHasDependency(const PreparedFrameGraphNode& node, uint32_t dependency)
    {
        for (uint32_t existing : node.dependencies)
        {
            if (existing == dependency)
                return true;
        }
        return false;
    }

    void FrameGraphAddDependency(PreparedFrameGraphNode& node, uint32_t dependency)
    {
        if (!FrameGraphHasDependency(node, dependency))
            node.dependencies.push_back(dependency);
    }
}




bool GDXECSRenderer::EnsureDebugCullingResources()
{
    if (!m_debugBoxMesh.IsValid())
    {
        MeshAssetResource mesh;
        mesh.debugName = "DebugBox";
        mesh.AddSubmesh(BuiltinMeshes::Cube());
        m_debugBoxMesh = UploadMesh(std::move(mesh));
    }

    auto makeMat = [this](float r, float g, float b, float a)
        {
            MaterialResource mat = MaterialResource::FlatColor(r, g, b, a);
            mat.data.flags = MF_UNLIT | MF_TRANSPARENT | MF_DOUBLE_SIDED;
            mat.data.receiveShadows = 0.0f;
            return CreateMaterial(std::move(mat));
        };

    if (!m_debugMainBoundsMat.IsValid())   m_debugMainBoundsMat = makeMat(0.10f, 0.95f, 0.20f, m_debugCulling.boundsAlpha);
    if (!m_debugShadowBoundsMat.IsValid()) m_debugShadowBoundsMat = makeMat(0.95f, 0.82f, 0.10f, m_debugCulling.boundsAlpha);
    if (!m_debugRttBoundsMat.IsValid())    m_debugRttBoundsMat = makeMat(0.12f, 0.45f, 1.00f, m_debugCulling.boundsAlpha);
    if (!m_debugMainFrustumMat.IsValid())  m_debugMainFrustumMat = makeMat(0.15f, 1.00f, 1.00f, m_debugCulling.frustumAlpha);
    if (!m_debugShadowFrustumMat.IsValid())m_debugShadowFrustumMat = makeMat(1.00f, 0.55f, 0.10f, m_debugCulling.frustumAlpha);

    return m_debugBoxMesh.IsValid()
        && m_debugMainBoundsMat.IsValid()
        && m_debugShadowBoundsMat.IsValid()
        && m_debugRttBoundsMat.IsValid()
        && m_debugMainFrustumMat.IsValid()
        && m_debugShadowFrustumMat.IsValid();
}

void GDXECSRenderer::AppendDebugBounds(RenderQueue& queue, const VisibleRenderCandidate& candidate, MaterialHandle material, float alpha, const FrameData& debugFrame, ViewExecutionStats* viewStats)
{
    if (!candidate.hasBounds || !m_debugBoxMesh.IsValid() || !material.IsValid())
        return;

    const float radius = (std::max)(candidate.worldBoundsRadius, 0.01f);
    const GIDX::Float3 scale = { radius * 2.0f, radius * 2.0f, radius * 2.0f };
    const MaterialResource* matRes = m_matStore.Get(material);
    const GDXShaderResource* shaderRes = m_shaderStore.Get(m_defaultShader);
    if (!matRes || !shaderRes)
        return;

    RenderCommand cmd{};
    cmd.mesh = m_debugBoxMesh;
    cmd.material = material;
    cmd.shader = m_defaultShader;
    cmd.submeshIndex = 0u;
    cmd.ownerEntity = candidate.entity;
    cmd.pass = RenderPass::Transparent;
    cmd.worldMatrix = BuildBoxWorldMatrix(candidate.worldBoundsCenter, scale);
    cmd.materialData = matRes->data;
    cmd.materialData.baseColor.w = alpha;
    const ResourceBindingSet bindings = BuildDebugBindings(*matRes, *shaderRes);
    cmd.SetBindings(bindings,
        BuildResourceBindingScopeKey(bindings, ResourceBindingScope::Pass, cmd.shader.value),
        BuildResourceBindingScopeKey(bindings, ResourceBindingScope::Material, cmd.material.value),
        BuildResourceBindingScopeKey(bindings, ResourceBindingScope::Draw, candidate.entity.value));
    GDXPipelineStateDesc pso{};
    pso.blendMode = GDXBlendMode::AlphaBlend;
    pso.cullMode = GDXCullMode::None;
    pso.depthMode = GDXDepthMode::ReadOnly;
    pso.depthTestEnabled = true;
    cmd.SetPipelineState(pso);
    const float depth = 1.0f - CameraSystem::ComputeNDCDepth(cmd.worldMatrix, debugFrame.viewProjMatrix);
    cmd.SetSortKey(RenderPass::Transparent, m_defaultShader.Index() & 0x0FFFu, GDXPipelineStateKey::FromDesc(pso).value & 0x00FFu, 0u, depth);
    queue.Submit(std::move(cmd));
    if (viewStats) ++viewStats->debugBoundsDraws;
}

void GDXECSRenderer::AppendDebugFrustum(RenderQueue& queue, const RenderViewData& view, MaterialHandle material, float alpha, const FrameData& debugFrame, ViewExecutionStats* viewStats)
{
    if (!m_debugBoxMesh.IsValid() || !material.IsValid())
        return;

    GIDX::Float3 corners[8]{};
    const GIDX::Float4x4& viewProj = (view.type == RenderViewType::Shadow) ? view.frame.shadowViewProjMatrix : view.frame.viewProjMatrix;
    if (!BuildFrustumCorners(viewProj, corners))
        return;

    static constexpr int edges[12][2] = {
        {0,1},{1,2},{2,3},{3,0},
        {4,5},{5,6},{6,7},{7,4},
        {0,4},{1,5},{2,6},{3,7}
    };

    for (const auto& e : edges)
    {
        const GIDX::Float3 a = corners[e[0]];
        const GIDX::Float3 b = corners[e[1]];
        const GIDX::Float4x4 world = BuildEdgeWorldMatrix(a, b, 0.03f);

        const MaterialResource* matRes = m_matStore.Get(material);
        const GDXShaderResource* shaderRes = m_shaderStore.Get(m_defaultShader);
        if (!matRes || !shaderRes)
            return;

        RenderCommand cmd{};
        cmd.mesh = m_debugBoxMesh;
        cmd.material = material;
        cmd.shader = m_defaultShader;
        cmd.submeshIndex = 0u;
        cmd.ownerEntity = NULL_ENTITY;
        cmd.pass = RenderPass::Transparent;
        cmd.worldMatrix = world;
        cmd.materialData = matRes->data;
        cmd.materialData.baseColor.w = alpha;
        const ResourceBindingSet bindings = BuildDebugBindings(*matRes, *shaderRes);
        cmd.SetBindings(bindings,
            BuildResourceBindingScopeKey(bindings, ResourceBindingScope::Pass, cmd.shader.value),
            BuildResourceBindingScopeKey(bindings, ResourceBindingScope::Material, cmd.material.value),
            BuildResourceBindingScopeKey(bindings, ResourceBindingScope::Draw, 0u));
        GDXPipelineStateDesc pso{};
        pso.blendMode = GDXBlendMode::AlphaBlend;
        pso.cullMode = GDXCullMode::None;
        pso.depthMode = GDXDepthMode::ReadOnly;
        pso.depthTestEnabled = true;
        cmd.SetPipelineState(pso);
        const float depth = 1.0f - CameraSystem::ComputeNDCDepth(cmd.worldMatrix, debugFrame.viewProjMatrix);
        cmd.SetSortKey(RenderPass::Transparent, m_defaultShader.Index() & 0x0FFFu, GDXPipelineStateKey::FromDesc(pso).value & 0x00FFu, 0u, depth);
        queue.Submit(std::move(cmd));
        if (viewStats) ++viewStats->debugFrustumDraws;
    }
}

void GDXECSRenderer::AppendDebugVisibleSet(RenderQueue& queue, const VisibleSet& set, const RenderViewData& view, ViewExecutionStats* viewStats)
{
    if (!m_debugCulling.enabled)
        return;
    if (!EnsureDebugCullingResources())
        return;

    MaterialHandle boundsMat = MaterialHandle::Invalid();
    bool drawBounds = false;
    bool drawFrustum = false;
    MaterialHandle frustumMat = MaterialHandle::Invalid();

    switch (view.type)
    {
    case RenderViewType::Main:
        drawBounds = m_debugCulling.drawMainVisibleBounds;
        boundsMat = m_debugMainBoundsMat;
        drawFrustum = m_debugCulling.drawMainFrustum;
        frustumMat = m_debugMainFrustumMat;
        break;
    case RenderViewType::Shadow:
        drawBounds = m_debugCulling.drawShadowVisibleBounds;
        boundsMat = m_debugShadowBoundsMat;
        drawFrustum = m_debugCulling.drawShadowFrustum;
        frustumMat = m_debugShadowFrustumMat;
        break;
    case RenderViewType::RenderTarget:
        drawBounds = m_debugCulling.drawRttVisibleBounds;
        boundsMat = m_debugRttBoundsMat;
        break;
    }

    if (drawBounds)
    {
        for (const auto& candidate : set.candidates)
            AppendDebugBounds(queue, candidate, boundsMat, m_debugCulling.boundsAlpha, view.frame, viewStats);
    }

    if (drawFrustum)
        AppendDebugFrustum(queue, view, frustumMat, m_debugCulling.frustumAlpha, view.frame, viewStats);
}

void GDXECSRenderer::LogDebugCullingStats() const
{
    if (!m_debugCulling.enabled || !m_debugCulling.logStats)
        return;
    if (m_debugCulling.logEveryNFrames == 0u)
        return;
    if ((m_frameNumber % m_debugCulling.logEveryNFrames) != 0u)
        return;

    auto fmt = [](const ViewCullingStats& s)
        {
            std::ostringstream oss;
            oss << "total=" << s.totalCandidates
                << " visible=" << s.visibleCandidates
                << " inactive=" << s.culledByInactive
                << " layer=" << s.culledByLayer
                << " frustum=" << s.culledByFrustum
                << " distance=" << s.culledByDistance
                << " noBounds=" << s.missingBounds;
            return oss.str();
        };
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
    ShaderVariantKey mainKey{};
    mainKey.pass = ShaderPassType::Main;
    mainKey.vertexFlags = GDX_VERTEX_DEFAULT;
    mainKey.features = SVF_NONE;
    m_defaultShader = CreateShaderVariant(mainKey);
    if (!m_defaultShader.IsValid()) return false;

    ShaderVariantKey shadowKey{};
    shadowKey.pass = ShaderPassType::Shadow;
    shadowKey.vertexFlags = GDX_VERTEX_POSITION;
    shadowKey.features = SVF_NONE;
    m_shadowShader = CreateShaderVariant(shadowKey);

    if (!m_shadowShader.IsValid())
        Debug::Log("GDXECSRenderer: Kein Shadow-Shader gefunden — Shadow Pass deaktiviert.");

    return true;
}

ShaderHandle GDXECSRenderer::CreateShader(
    const std::wstring& vsFile, const std::wstring& psFile, uint32_t vertexFlags)
{
    return LoadShaderInternal(vsFile, psFile, vertexFlags, vsFile + L" / " + psFile, nullptr);
}

ShaderHandle GDXECSRenderer::CreateShader(
    const std::wstring& vsFile,
    const std::wstring& psFile,
    uint32_t vertexFlags,
    const GDXShaderLayout& layout)
{
    return LoadShaderInternal(vsFile, psFile, vertexFlags, vsFile + L" / " + psFile, &layout);
}

ShaderHandle GDXECSRenderer::LoadShaderInternal(
    const std::wstring& vsFile,
    const std::wstring& psFile,
    uint32_t vertexFlags,
    const std::wstring& debugName,
    const GDXShaderLayout* customLayout)
{
    if (!m_backend) return ShaderHandle::Invalid();
    const GDXShaderLayout layout = customLayout
        ? *customLayout
        : GDXShaderLayouts::BuildMain(vertexFlags, (vertexFlags & GDX_VERTEX_BONE_WEIGHTS) != 0u);
    return m_backend->CreateShader(m_shaderStore, vsFile, psFile, vertexFlags, layout, debugName);
}

ShaderVariantKey GDXECSRenderer::BuildVariantKey(RenderPass pass, const SubmeshData& submesh, const MaterialResource& mat) const
{
    ShaderVariantKey key{};
    key.pass = (pass == RenderPass::Shadow) ? ShaderPassType::Shadow : ShaderPassType::Main;
    key.vertexFlags = submesh.ComputeVertexFlags();

    if (submesh.HasSkinning()) key.features |= SVF_SKINNED;
    if (submesh.colors.size() == submesh.positions.size() && !submesh.colors.empty()) key.features |= SVF_VERTEX_COLOR;
    if (mat.IsAlphaTest()) key.features |= SVF_ALPHA_TEST;
    if (mat.IsTransparent()) key.features |= SVF_TRANSPARENT;
    if ((mat.data.flags & MF_USE_NORMAL_MAP) != 0u) key.features |= SVF_NORMAL_MAP;
    if ((mat.data.flags & MF_UNLIT) != 0u) key.features |= SVF_UNLIT;

    return key;
}

ShaderVariantKey GDXECSRenderer::NormalizeVariantKey(const ShaderVariantKey& in) const
{
    ShaderVariantKey key = in;

    if (key.pass == ShaderPassType::Main)
        key.features &= kRelevantMainFeatures;
    else
        key.features &= kRelevantShadowFeatures;

    // Tangent-Stream nur behalten, wenn Shader später wirklich darauf aufbaut.
    // Aktueller Standard-Shader erzeugt TBN per Derivaten.
    key.vertexFlags &= ~GDX_VERTEX_TANGENT;

    return key;
}

ShaderHandle GDXECSRenderer::CreateShaderVariant(const ShaderVariantKey& rawKey)
{
    const ShaderVariantKey key = NormalizeVariantKey(rawKey);

    std::wstring vsFile;
    std::wstring psFile;
    uint32_t vertexFlags = key.vertexFlags;

    const bool skinned = (key.features & SVF_SKINNED) != 0u;
    const bool vertexColor = (key.features & SVF_VERTEX_COLOR) != 0u;
    const bool alphaTest = (key.features & SVF_ALPHA_TEST) != 0u;

    if (key.pass == ShaderPassType::Main)
    {
        psFile = L"ECSPixelShader.hlsl";

        if (skinned && vertexColor)
            vsFile = L"ECSVertexShader_SkinnedVertexColor.hlsl";
        else if (skinned)
            vsFile = L"ECSVertexShader_Skinned.hlsl";
        else if (vertexColor)
            vsFile = L"ECSVertexShader_VertexColor.hlsl";
        else
            vsFile = L"ECSVertexShader.hlsl";
    }
    else
    {
        if (skinned && alphaTest)
        {
            vsFile = L"ECSShadowVertexShader_SkinnedAlphaTest.hlsl";
            psFile = L"ECSShadowPixelShader_AlphaTest.hlsl";
            vertexFlags = GDX_VERTEX_POSITION | GDX_VERTEX_TEX1 | GDX_VERTEX_BONE_INDICES | GDX_VERTEX_BONE_WEIGHTS;
        }
        else if (skinned)
        {
            vsFile = L"ECSShadowVertexShader_Skinned.hlsl";
            psFile = L"ECSShadowPixelShader.hlsl";
            vertexFlags = GDX_VERTEX_POSITION | GDX_VERTEX_BONE_INDICES | GDX_VERTEX_BONE_WEIGHTS;
        }
        else if (alphaTest)
        {
            vsFile = L"ECSShadowVertexShader_AlphaTest.hlsl";
            psFile = L"ECSShadowPixelShader_AlphaTest.hlsl";
            vertexFlags = GDX_VERTEX_POSITION | GDX_VERTEX_TEX1;
        }
        else
        {
            vsFile = L"ECSShadowVertexShader.hlsl";
            psFile = L"ECSShadowPixelShader.hlsl";
            vertexFlags = GDX_VERTEX_POSITION;
        }
    }

    const std::wstring debugName = L"Variant: " + vsFile + L" / " + psFile;
    const GDXShaderLayout layout = (key.pass == ShaderPassType::Shadow)
        ? GDXShaderLayouts::BuildShadow(vertexFlags, skinned, alphaTest)
        : GDXShaderLayouts::BuildMain(vertexFlags, skinned);

    ShaderHandle handle = m_backend
        ? m_backend->CreateShader(m_shaderStore, vsFile, psFile, vertexFlags, layout, debugName)
        : ShaderHandle::Invalid();
    if (!handle.IsValid())
        return ShaderHandle::Invalid();

    if (auto* res = m_shaderStore.Get(handle))
    {
        res->passType = key.pass;
        res->variantFeatures = key.features;
        res->supportsSkinning = skinned;
        res->usesVertexColor = vertexColor;
    }

    m_shaderVariantCache.emplace(key, handle);
    return handle;
}

ShaderHandle GDXECSRenderer::ResolveShaderVariant(RenderPass pass, const SubmeshData& submesh, const MaterialResource& mat)
{
    if (pass != RenderPass::Shadow && mat.shader.IsValid())
        return mat.shader;

    const ShaderVariantKey key = NormalizeVariantKey(BuildVariantKey(pass, submesh, mat));
    auto it = m_shaderVariantCache.find(key);
    if (it != m_shaderVariantCache.end())
        return it->second;

    return CreateShaderVariant(key);
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

void GDXECSRenderer::PrepareMainViewData(const FrameData& frameSnapshot, ViewPassExecutionData& outView)
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
    std::vector<ViewPassExecutionData>& outViews)
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

            ViewPassExecutionData preparedView{};
            preparedView.prepared.frame = frameSnapshot;
            preparedView.prepared.frame.viewportWidth = static_cast<float>(rt->width);
            preparedView.prepared.frame.viewportHeight = static_cast<float>(rt->height);

            const bool built = BuildFrameDataFromCameraEntityWithOverride(m_registry, entity, cameraForView, preparedView.prepared.frame);

            if (!built)
                return;

            preparedView.prepared.graphicsView = {};
            preparedView.prepared.graphicsView.type = RenderViewType::RenderTarget;
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
            }

            outViews.push_back(std::move(preparedView));
        });
}

void GDXECSRenderer::CullPreparedRenderTargetGraphicsViews(std::vector<ViewPassExecutionData>& preparedViews)
{
    // Each view writes to its own graphicsVisibleSet — no shared mutable state.
    // ViewCullingSystem is stateless (const method, no members).
    // Safe to process views in parallel via ParallelFor with minBatchSize=1.
    const size_t viewCount = preparedViews.size();
    auto process = [&](size_t begin, size_t end)
        {
            for (size_t vi = begin; vi < end; ++vi)
            {
                ViewPassExecutionData& preparedView = preparedViews[vi];
                GDXRenderTargetResource* rt = m_rtStore.Get(preparedView.prepared.graphicsView.renderTarget);
                if (!rt || !rt->ready)
                    continue;

                preparedView.graphicsVisibleSet = {};
                m_viewCullingSystem.BuildVisibleSet(m_registry, preparedView.prepared.graphicsView, preparedView.graphicsVisibleSet, nullptr);
                preparedView.stats.graphicsCulling = preparedView.graphicsVisibleSet.stats;
            }
        };

    m_jobSystem.ParallelFor(viewCount, process, 1u);
}

void GDXECSRenderer::CullPreparedRenderTargetShadowViews(std::vector<ViewPassExecutionData>& preparedViews)
{
    // Same safety argument as CullPreparedRenderTargetGraphicsViews.
    const size_t viewCount = preparedViews.size();
    auto process = [&](size_t begin, size_t end)
        {
            for (size_t vi = begin; vi < end; ++vi)
            {
                ViewPassExecutionData& preparedView = preparedViews[vi];
                GDXRenderTargetResource* rt = m_rtStore.Get(preparedView.prepared.graphicsView.renderTarget);
                if (!rt || !rt->ready || !preparedView.prepared.shadowEnabled)
                    continue;

                preparedView.shadowVisibleSet = {};
                m_viewCullingSystem.BuildVisibleSet(m_registry, preparedView.prepared.shadowView, preparedView.shadowVisibleSet, nullptr);
                preparedView.stats.shadowCulling = preparedView.shadowVisibleSet.stats;
            }
        };

    m_jobSystem.ParallelFor(viewCount, process, 1u);
}

void GDXECSRenderer::CullPreparedMainViewGraphics(ViewPassExecutionData& preparedView)
{
    preparedView.graphicsVisibleSet = {};
    m_framePhase = RenderFramePhase::VisibilityBuild;
    m_viewCullingSystem.BuildVisibleSet(m_registry, preparedView.prepared.graphicsView, preparedView.graphicsVisibleSet, &m_jobSystem);
    preparedView.stats.graphicsCulling = preparedView.graphicsVisibleSet.stats;
}

void GDXECSRenderer::CullPreparedMainViewShadow(ViewPassExecutionData& preparedView)
{
    preparedView.shadowVisibleSet = {};
    preparedView.stats.shadowCulling = {};

    if (!preparedView.prepared.shadowEnabled)
        return;

    m_framePhase = RenderFramePhase::VisibilityBuild;
    m_viewCullingSystem.BuildVisibleSet(m_registry, preparedView.prepared.shadowView, preparedView.shadowVisibleSet, &m_jobSystem);
    preparedView.stats.shadowCulling = preparedView.shadowVisibleSet.stats;
}

void GDXECSRenderer::GatherPreparedRenderTargetGraphicsViews(const RenderGatherSystem::ShaderResolver& resolveShader,
    std::vector<ViewPassExecutionData>& preparedViews)
{
    // Each view writes to its own graphicsGatherChunks. RenderGatherSystem has no
    // mutable member state — safe to process views in parallel.
    // The inner GatherVisibleSetChunks already uses the job system for per-chunk work;
    // we pass nullptr here to avoid nested parallelism that would saturate the thread pool.
    const size_t viewCount = preparedViews.size();
    auto process = [&](size_t begin, size_t end)
        {
            for (size_t vi = begin; vi < end; ++vi)
            {
                ViewPassExecutionData& preparedView = preparedViews[vi];
                GDXRenderTargetResource* rt = m_rtStore.Get(preparedView.prepared.graphicsView.renderTarget);
                if (!rt || !rt->ready)
                    continue;

                preparedView.graphicsGatherChunks.clear();
                m_gatherSystem.GatherVisibleSetChunks(preparedView.graphicsVisibleSet, preparedView.prepared.frame,
                    m_meshStore, m_matStore, m_shaderStore,
                    resolveShader,
                    preparedView.graphicsGatherChunks,
                    &preparedView.prepared.gatherOptions,
                    nullptr);
            }
        };

    m_jobSystem.ParallelFor(viewCount, process, 1u);
}

void GDXECSRenderer::GatherPreparedRenderTargetShadowViews(const RenderGatherSystem::ShaderResolver& resolveShader,
    std::vector<ViewPassExecutionData>& preparedViews)
{
    // Same safety argument as GatherPreparedRenderTargetGraphicsViews.
    const size_t viewCount = preparedViews.size();
    auto process = [&](size_t begin, size_t end)
        {
            for (size_t vi = begin; vi < end; ++vi)
            {
                ViewPassExecutionData& preparedView = preparedViews[vi];
                GDXRenderTargetResource* rt = m_rtStore.Get(preparedView.prepared.graphicsView.renderTarget);
                if (!rt || !rt->ready || !preparedView.prepared.shadowEnabled)
                    continue;

                RenderGatherOptions shadowGatherOptions = preparedView.prepared.gatherOptions;
                shadowGatherOptions.gatherOpaque = false;
                shadowGatherOptions.gatherTransparent = false;
                shadowGatherOptions.gatherShadows = true;

                preparedView.shadowGatherChunks.clear();
                m_gatherSystem.GatherShadowVisibleSetChunks(preparedView.shadowVisibleSet, preparedView.prepared.frame,
                    m_meshStore, m_matStore, m_shaderStore,
                    resolveShader,
                    preparedView.shadowGatherChunks,
                    &shadowGatherOptions,
                    nullptr);
            }
        };

    m_jobSystem.ParallelFor(viewCount, process, 1u);
}

void GDXECSRenderer::FinalizePreparedViewQueues(ViewPassExecutionData& preparedView)
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

void GDXECSRenderer::FinalizePreparedRenderTargetQueues(std::vector<ViewPassExecutionData>& preparedViews)
{
    // Each view writes only to its own opaqueQueue / transparentQueue / shadowQueue.
    // FinalizePreparedViewQueues has no shared mutable state — safe to parallelize.
    const size_t viewCount = preparedViews.size();
    auto process = [&](size_t begin, size_t end)
        {
            for (size_t vi = begin; vi < end; ++vi)
            {
                ViewPassExecutionData& preparedView = preparedViews[vi];
                GDXRenderTargetResource* rt = m_rtStore.Get(preparedView.prepared.graphicsView.renderTarget);
                if (!rt || !rt->ready)
                    continue;

                FinalizePreparedViewQueues(preparedView);
            }
        };

    m_jobSystem.ParallelFor(viewCount, process, 1u);
}

void GDXECSRenderer::ConfigurePreparedCommonExecuteInputs(ViewPassExecutionData& preparedView, bool presentAfterExecute)
{
    preparedView.execute.Reset();
    // NOTE: lights and frame constants are updated in the pre-pass inside
    // ExecutePreparedFrame, not here. No mutable flag needed.
    preparedView.execute.presentation.presentAfterExecute = presentAfterExecute;
}

bool GDXECSRenderer::PrepareMainViewPostProcessPresentation(ViewPassExecutionData& preparedView)
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

void GDXECSRenderer::BuildPreparedRenderTargetExecuteInputs(std::vector<ViewPassExecutionData>& preparedViews)
{
    for (auto& preparedView : preparedViews)
    {
        ConfigurePreparedCommonExecuteInputs(preparedView, false);

        GDXRenderTargetResource* rt = m_rtStore.Get(preparedView.prepared.graphicsView.renderTarget);
        if (!rt || !rt->ready)
            continue;

        BuildPreparedShadowPassExecuteInput(preparedView);
        BuildPreparedGraphicsPassExecuteInput(preparedView, preparedView.prepared.graphicsTargetDesc, true, false);
        BuildPreparedExecutionQueues(preparedView);
    }
}

void GDXECSRenderer::BuildPreparedShadowPassExecuteInput(ViewPassExecutionData& preparedView)
{
    preparedView.execute.shadowPass.Reset();
    preparedView.execute.shadowPass.enabled = preparedView.prepared.shadowEnabled && !preparedView.shadowQueue.Empty();
    if (!preparedView.execute.shadowPass.enabled)
        return;

    preparedView.execute.shadowPass.desc = BackendRenderPassDesc::Shadow(preparedView.prepared.frame);
}

void GDXECSRenderer::BuildPreparedGraphicsPassExecuteInput(
    ViewPassExecutionData& preparedView,
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

void GDXECSRenderer::BuildPreparedExecutionQueues(ViewPassExecutionData& preparedView)
{
    preparedView.execute.shadowQueue = preparedView.shadowQueue;
    preparedView.execute.graphicsQueue = preparedView.BuildGraphicsQueue();

    const PreparedPassExecution& passExecution = preparedView.execute.graphicsPass;
    if (passExecution.appendGraphicsVisibleSet)
        AppendDebugVisibleSet(preparedView.execute.graphicsQueue, preparedView.graphicsVisibleSet, preparedView.prepared.graphicsView, &preparedView.stats);

    if (passExecution.appendShadowVisibleSet && preparedView.prepared.shadowEnabled)
        AppendDebugVisibleSet(preparedView.execute.graphicsQueue, preparedView.shadowVisibleSet, preparedView.prepared.shadowView, &preparedView.stats);

    if (passExecution.sortQueueBeforeExecute)
        preparedView.execute.graphicsQueue.Sort();
}

void GDXECSRenderer::ExecutePreparedViewPasses(ViewPassExecutionData& preparedView)
{
    // NOTE: lights and frame constants are now managed by the pre-pass in
    // ExecutePreparedFrame. This legacy path (used by non-frame-graph routes)
    // updates them unconditionally to preserve the existing behaviour.
    if (m_backend)
    {
        m_backend->UpdateLights(m_registry, preparedView.prepared.frame);
        m_backend->UpdateFrameConstants(preparedView.prepared.frame);
    }

    if (m_backend && preparedView.execute.shadowPass.enabled)
    {
        m_backend->ExecuteRenderPass(preparedView.execute.shadowPass.desc, m_registry,
            preparedView.execute.shadowQueue, m_meshStore, m_matStore, m_shaderStore, m_texStore, &m_rtStore);
        preparedView.stats.shadowPassExecuted = true;
    }

    if (m_backend && preparedView.execute.graphicsPass.enabled)
    {
        m_backend->ExecuteRenderPass(preparedView.execute.graphicsPass.desc, m_registry,
            preparedView.execute.graphicsQueue, m_meshStore, m_matStore, m_shaderStore, m_texStore, &m_rtStore);
        preparedView.stats.graphicsPassExecuted = true;
    }
}

void GDXECSRenderer::ExecutePreparedMainViewPresentation(ViewPassExecutionData& preparedView)
{
    if (!m_backend)
        return;

    if (preparedView.execute.presentation.postProcess.enabled &&
        preparedView.execute.presentation.postProcess.sceneTexture.IsValid())
    {
        m_backend->ExecutePostProcessChain(m_postProcessPassOrder, m_postProcessStore, m_texStore,
            preparedView.execute.presentation.postProcess.sceneTexture,
            preparedView.prepared.frame.viewportWidth,
            preparedView.prepared.frame.viewportHeight);
        preparedView.stats.presentationExecuted = true;
    }
}

void GDXECSRenderer::ExecutePreparedRenderTargetViews(std::vector<ViewPassExecutionData>& preparedViews)
{
    for (auto& preparedView : preparedViews)
    {
        GDXRenderTargetResource* rt = m_rtStore.Get(preparedView.prepared.graphicsView.renderTarget);
        if (!rt || !rt->ready)
            continue;

        preparedView.stats.countedAsRenderTargetView = true;
        ExecutePreparedViewPasses(preparedView);
    }
}

void GDXECSRenderer::GatherPreparedMainViewGraphics(const RenderGatherSystem::ShaderResolver& resolveShader,
    ViewPassExecutionData& preparedView)
{
    preparedView.graphicsGatherChunks.clear();
    m_gatherSystem.GatherVisibleSetChunks(preparedView.graphicsVisibleSet, preparedView.prepared.frame,
        m_meshStore, m_matStore, m_shaderStore,
        resolveShader, preparedView.graphicsGatherChunks,
        &preparedView.prepared.gatherOptions,
        &m_jobSystem);
}

void GDXECSRenderer::GatherPreparedMainViewShadow(const RenderGatherSystem::ShaderResolver& resolveShader,
    ViewPassExecutionData& preparedView)
{
    preparedView.shadowGatherChunks.clear();
    if (!preparedView.prepared.shadowEnabled)
        return;

    RenderGatherOptions shadowGatherOptions = preparedView.prepared.gatherOptions;
    shadowGatherOptions.gatherOpaque = false;
    shadowGatherOptions.gatherTransparent = false;
    shadowGatherOptions.gatherShadows = true;

    m_gatherSystem.GatherShadowVisibleSetChunks(preparedView.shadowVisibleSet, preparedView.prepared.frame,
        m_meshStore, m_matStore, m_shaderStore,
        resolveShader,
        preparedView.shadowGatherChunks,
        &shadowGatherOptions,
        &m_jobSystem);
}

void GDXECSRenderer::FinalizePreparedMainViewQueues(ViewPassExecutionData& preparedView)
{
    FinalizePreparedViewQueues(preparedView);
}

void GDXECSRenderer::BuildPreparedMainViewExecuteInputs(ViewPassExecutionData& preparedView)
{
    ConfigurePreparedCommonExecuteInputs(preparedView, true);

    BuildPreparedShadowPassExecuteInput(preparedView);

    if (!PrepareMainViewPostProcessPresentation(preparedView))
        BuildPreparedGraphicsPassExecuteInput(preparedView, preparedView.prepared.graphicsTargetDesc, true, true);

    BuildPreparedExecutionQueues(preparedView);
}

void GDXECSRenderer::FinalizePreparedFrameQueues(RendererFramePipelineData& pipeline)
{
    FinalizePreparedRenderTargetQueues(pipeline.rttViews);
    FinalizePreparedMainViewQueues(pipeline.mainView);
}

void GDXECSRenderer::BuildPreparedFrameExecuteInputs(RendererFramePipelineData& pipeline)
{
    BuildPreparedRenderTargetExecuteInputs(pipeline.rttViews);
    BuildPreparedMainViewExecuteInputs(pipeline.mainView);
}

void GDXECSRenderer::ExecutePreparedMainView(ViewPassExecutionData& preparedView)
{
    ExecutePreparedViewPasses(preparedView);
    ExecutePreparedMainViewPresentation(preparedView);

    preparedView.stats.drawCalls = m_backend ? m_backend->GetDrawCallCount() : 0u;
    preparedView.stats.renderCommands = preparedView.execute.graphicsPass.enabled ? static_cast<uint32_t>(preparedView.execute.graphicsQueue.Count()) : 0u;
    preparedView.stats.lightCount = preparedView.prepared.frame.lightCount;
}

void GDXECSRenderer::BuildPreparedFrameGraph(RendererFramePipelineData& pipeline)
{
    pipeline.frameGraph.Reset();

    for (uint32_t i = 0u; i < static_cast<uint32_t>(pipeline.rttViews.size()); ++i)
    {
        ViewPassExecutionData& view = pipeline.rttViews[i];
        GDXRenderTargetResource* rt = m_rtStore.Get(view.prepared.graphicsView.renderTarget);
        if (!rt || !rt->ready)
            continue;

        if (view.execute.shadowPass.enabled)
        {
            PreparedFrameGraphNode shadowNode{};
            shadowNode.kind = PreparedFrameGraphNodeKind::RenderTargetShadow;
            shadowNode.view = &view;
            shadowNode.executeInput = &view.execute;
            shadowNode.statsOutput = &view.stats;
            shadowNode.viewIndex = i;
            shadowNode.enabled = true;
            shadowNode.countedAsRenderTargetView = true;
            shadowNode.writeResource = MakeFrameGraphWriteResource(
                PreparedFrameGraphResourceKind::ShadowMap,
                TextureHandle::Invalid(),
                RenderTargetHandle::Invalid(),
                i + 1u);
            pipeline.frameGraph.nodes.push_back(std::move(shadowNode));
        }

        if (view.execute.graphicsPass.enabled)
        {
            PreparedFrameGraphNode graphicsNode{};
            graphicsNode.kind = PreparedFrameGraphNodeKind::RenderTargetGraphics;
            graphicsNode.view = &view;
            graphicsNode.executeInput = &view.execute;
            graphicsNode.statsOutput = &view.stats;
            graphicsNode.viewIndex = i;
            graphicsNode.enabled = true;
            graphicsNode.countedAsRenderTargetView = true;
            if (view.execute.shadowPass.enabled)
            {
                graphicsNode.readResource = MakeFrameGraphReadResource(
                    PreparedFrameGraphResourceKind::ShadowMap,
                    TextureHandle::Invalid(),
                    RenderTargetHandle::Invalid(),
                    i + 1u);
            }
            graphicsNode.writeResource = MakeFrameGraphWriteResource(
                PreparedFrameGraphResourceKind::RenderTargetColor,
                TextureHandle::Invalid(),
                view.prepared.graphicsView.renderTarget,
                i + 1u);
            pipeline.frameGraph.nodes.push_back(std::move(graphicsNode));
        }
    }

    if (pipeline.mainView.execute.shadowPass.enabled)
    {
        PreparedFrameGraphNode shadowNode{};
        shadowNode.kind = PreparedFrameGraphNodeKind::MainShadow;
        shadowNode.view = &pipeline.mainView;
        shadowNode.executeInput = &pipeline.mainView.execute;
        shadowNode.statsOutput = &pipeline.mainView.stats;
        shadowNode.enabled = true;
        shadowNode.writeResource = MakeFrameGraphWriteResource(
            PreparedFrameGraphResourceKind::ShadowMap,
            TextureHandle::Invalid(),
            RenderTargetHandle::Invalid(),
            0u);
        pipeline.frameGraph.nodes.push_back(std::move(shadowNode));
    }

    if (pipeline.mainView.execute.graphicsPass.enabled)
    {
        PreparedFrameGraphNode graphicsNode{};
        graphicsNode.kind = PreparedFrameGraphNodeKind::MainGraphics;
        graphicsNode.view = &pipeline.mainView;
        graphicsNode.executeInput = &pipeline.mainView.execute;
        graphicsNode.statsOutput = &pipeline.mainView.stats;
        graphicsNode.enabled = true;
        if (pipeline.mainView.execute.shadowPass.enabled)
        {
            graphicsNode.readResource = MakeFrameGraphReadResource(
                PreparedFrameGraphResourceKind::ShadowMap,
                TextureHandle::Invalid(),
                RenderTargetHandle::Invalid(),
                0u);
        }

        if (pipeline.mainView.execute.presentation.postProcess.enabled &&
            pipeline.mainView.execute.presentation.postProcess.sceneTexture.IsValid())
        {
            graphicsNode.writeResource = MakeFrameGraphWriteResource(
                PreparedFrameGraphResourceKind::MainSceneColor,
                pipeline.mainView.execute.presentation.postProcess.sceneTexture,
                m_mainScenePostProcessTarget,
                0u);
        }
        else
        {
            graphicsNode.writeResource = MakeFrameGraphWriteResource(
                PreparedFrameGraphResourceKind::BackbufferColor);
        }

        pipeline.frameGraph.nodes.push_back(std::move(graphicsNode));
    }

    if (pipeline.mainView.execute.presentation.postProcess.enabled)
    {
        PreparedFrameGraphNode presentationNode{};
        presentationNode.kind = PreparedFrameGraphNodeKind::MainPresentation;
        presentationNode.view = &pipeline.mainView;
        presentationNode.executeInput = &pipeline.mainView.execute;
        presentationNode.statsOutput = &pipeline.mainView.stats;
        presentationNode.enabled = true;
        if (pipeline.mainView.execute.presentation.postProcess.sceneTexture.IsValid())
        {
            presentationNode.readResource = MakeFrameGraphReadResource(
                PreparedFrameGraphResourceKind::MainSceneColor,
                pipeline.mainView.execute.presentation.postProcess.sceneTexture,
                m_mainScenePostProcessTarget,
                0u);
        }
        presentationNode.writeResource = MakeFrameGraphWriteResource(
            PreparedFrameGraphResourceKind::BackbufferColor);
        pipeline.frameGraph.nodes.push_back(std::move(presentationNode));
    }

    FinalizePreparedFrameGraph(pipeline.frameGraph);
}

bool GDXECSRenderer::FinalizePreparedFrameGraph(PreparedFrameGraph& frameGraph)
{
    BuildPreparedFrameGraphDependencies(frameGraph);
    BuildPreparedFrameGraphExecutionOrder(frameGraph);
    return ValidatePreparedFrameGraph(frameGraph);
}

void GDXECSRenderer::BuildPreparedFrameGraphDependencies(PreparedFrameGraph& frameGraph)
{
    for (auto& node : frameGraph.nodes)
        node.dependencies.clear();

    for (uint32_t i = 0u; i < static_cast<uint32_t>(frameGraph.nodes.size()); ++i)
    {
        PreparedFrameGraphNode& node = frameGraph.nodes[i];

        if (node.readResource.kind != PreparedFrameGraphResourceKind::None)
        {
            for (int j = static_cast<int>(i) - 1; j >= 0; --j)
            {
                const PreparedFrameGraphNode& prev = frameGraph.nodes[static_cast<size_t>(j)];
                if (prev.writeResource.kind != PreparedFrameGraphResourceKind::None &&
                    FrameGraphResourceIdentityEquals(node.readResource, prev.writeResource))
                {
                    FrameGraphAddDependency(node, static_cast<uint32_t>(j));
                    break;
                }
            }
        }

        if ((node.kind == PreparedFrameGraphNodeKind::RenderTargetGraphics || node.kind == PreparedFrameGraphNodeKind::MainGraphics) &&
            node.view && node.view->prepared.shadowEnabled)
        {
            for (int j = static_cast<int>(i) - 1; j >= 0; --j)
            {
                const PreparedFrameGraphNode& prev = frameGraph.nodes[static_cast<size_t>(j)];
                if (prev.view == node.view &&
                    (prev.kind == PreparedFrameGraphNodeKind::RenderTargetShadow || prev.kind == PreparedFrameGraphNodeKind::MainShadow))
                {
                    if (prev.enabled)
                        FrameGraphAddDependency(node, static_cast<uint32_t>(j));
                    break;
                }
            }
        }

        if (node.kind == PreparedFrameGraphNodeKind::MainPresentation)
        {
            for (int j = static_cast<int>(i) - 1; j >= 0; --j)
            {
                const PreparedFrameGraphNode& prev = frameGraph.nodes[static_cast<size_t>(j)];
                if (prev.kind == PreparedFrameGraphNodeKind::MainGraphics)
                {
                    FrameGraphAddDependency(node, static_cast<uint32_t>(j));
                    break;
                }
            }
        }

        if (node.writeResource.kind != PreparedFrameGraphResourceKind::None)
        {
            for (int j = static_cast<int>(i) - 1; j >= 0; --j)
            {
                const PreparedFrameGraphNode& prev = frameGraph.nodes[static_cast<size_t>(j)];
                if (prev.writeResource.kind != PreparedFrameGraphResourceKind::None &&
                    FrameGraphResourceIdentityEquals(node.writeResource, prev.writeResource))
                {
                    FrameGraphAddDependency(node, static_cast<uint32_t>(j));
                    break;
                }
            }
        }
    }
}

bool GDXECSRenderer::BuildPreparedFrameGraphExecutionOrder(PreparedFrameGraph& frameGraph) const
{
    frameGraph.executionOrder.clear();
    const uint32_t nodeCount = static_cast<uint32_t>(frameGraph.nodes.size());
    std::vector<uint32_t> indegree(nodeCount, 0u);
    std::vector<std::vector<uint32_t>> dependents(nodeCount);

    for (uint32_t i = 0u; i < nodeCount; ++i)
    {
        for (uint32_t dep : frameGraph.nodes[i].dependencies)
        {
            if (dep >= nodeCount || dep == i)
                continue;
            ++indegree[i];
            dependents[dep].push_back(i);
        }
    }

    std::vector<uint32_t> ready;
    ready.reserve(nodeCount);
    for (uint32_t i = 0u; i < nodeCount; ++i)
    {
        if (indegree[i] == 0u)
            ready.push_back(i);
    }

    while (!ready.empty())
    {
        uint32_t bestPos = 0u;
        for (uint32_t k = 1u; k < static_cast<uint32_t>(ready.size()); ++k)
        {
            if (ready[k] < ready[bestPos])
                bestPos = k;
        }

        const uint32_t nodeIndex = ready[bestPos];
        ready.erase(ready.begin() + static_cast<std::ptrdiff_t>(bestPos));
        frameGraph.executionOrder.push_back(nodeIndex);

        for (uint32_t dependent : dependents[nodeIndex])
        {
            if (indegree[dependent] > 0u)
            {
                --indegree[dependent];
                if (indegree[dependent] == 0u)
                    ready.push_back(dependent);
            }
        }
    }

    return frameGraph.executionOrder.size() == nodeCount;
}

bool GDXECSRenderer::ValidatePreparedFrameGraph(PreparedFrameGraph& frameGraph) const
{
    frameGraph.validation.Reset();
    const uint32_t nodeCount = static_cast<uint32_t>(frameGraph.nodes.size());

    for (uint32_t i = 0u; i < nodeCount; ++i)
    {
        const PreparedFrameGraphNode& node = frameGraph.nodes[i];

        for (uint32_t dep : node.dependencies)
        {
            if (dep >= nodeCount)
            {
                frameGraph.validation.valid = false;
                frameGraph.validation.errors.push_back("FrameGraph dependency index out of range at node " + std::to_string(i));
                continue;
            }

            if (dep == i)
            {
                frameGraph.validation.valid = false;
                frameGraph.validation.errors.push_back("FrameGraph self dependency at node " + std::to_string(i));
            }
        }

        if (node.readResource.kind != PreparedFrameGraphResourceKind::None)
        {
            bool hasWriterDependency = false;
            for (uint32_t dep : node.dependencies)
            {
                if (dep < nodeCount && FrameGraphResourceIdentityEquals(node.readResource, frameGraph.nodes[dep].writeResource))
                {
                    hasWriterDependency = true;
                    break;
                }
            }

            if (!hasWriterDependency)
            {
                frameGraph.validation.valid = false;
                frameGraph.validation.errors.push_back("FrameGraph read without writer dependency at node " + std::to_string(i));
            }
        }

        if (node.writeResource.kind != PreparedFrameGraphResourceKind::None)
        {
            for (uint32_t j = 0u; j < i; ++j)
            {
                if (FrameGraphResourceIdentityEquals(node.writeResource, frameGraph.nodes[j].writeResource))
                {
                    const bool ordered = FrameGraphHasDependency(node, j);
                    if (!ordered)
                    {
                        frameGraph.validation.valid = false;
                        frameGraph.validation.errors.push_back(
                            "FrameGraph write-after-write without dependency between nodes " +
                            std::to_string(j) + " and " + std::to_string(i));
                    }
                }
            }
        }
    }

    if (frameGraph.executionOrder.size() != static_cast<size_t>(nodeCount))
    {
        frameGraph.validation.valid = false;
        frameGraph.validation.errors.push_back("FrameGraph contains a dependency cycle");
        return frameGraph.validation.valid;
    }

    std::vector<int32_t> orderPosition(nodeCount, -1);
    for (uint32_t orderPos = 0u; orderPos < static_cast<uint32_t>(frameGraph.executionOrder.size()); ++orderPos)
    {
        const uint32_t nodeIndex = frameGraph.executionOrder[orderPos];
        if (nodeIndex >= nodeCount)
        {
            frameGraph.validation.valid = false;
            frameGraph.validation.errors.push_back(
                "FrameGraph execution index out of range at order position " + std::to_string(orderPos));
            continue;
        }

        if (orderPosition[nodeIndex] != -1)
        {
            frameGraph.validation.valid = false;
            frameGraph.validation.errors.push_back(
                "FrameGraph execution order contains duplicate node " + std::to_string(nodeIndex));
            continue;
        }

        orderPosition[nodeIndex] = static_cast<int32_t>(orderPos);
    }

    for (uint32_t i = 0u; i < nodeCount; ++i)
    {
        if (orderPosition[i] == -1)
        {
            frameGraph.validation.valid = false;
            frameGraph.validation.errors.push_back(
                "FrameGraph execution order missing node " + std::to_string(i));
        }
    }

    for (uint32_t i = 0u; i < nodeCount; ++i)
    {
        if (orderPosition[i] == -1)
            continue;

        const PreparedFrameGraphNode& node = frameGraph.nodes[i];
        for (uint32_t dep : node.dependencies)
        {
            if (dep >= nodeCount || orderPosition[dep] == -1)
                continue;

            if (orderPosition[dep] >= orderPosition[i])
            {
                frameGraph.validation.valid = false;
                frameGraph.validation.errors.push_back(
                    "FrameGraph execution order violates dependency from node " +
                    std::to_string(dep) + " to node " + std::to_string(i));
            }
        }
    }

    return frameGraph.validation.valid;
}

void GDXECSRenderer::ExecutePreparedFrameGraphNode(PreparedFrameGraphNode& node)
{
    if (!node.enabled)
        return;

    // Execute reads exclusively from executeInput (const — frozen after Prepare).
    // Execute writes exclusively to statsOutput.
    // node.view is NOT touched here; it belongs to the Prepare phase.
    const PreparedExecuteData* exec = node.executeInput;
    ViewExecutionStats* stats = node.statsOutput;

    if (!exec || !stats)
        return;

    if (node.countedAsRenderTargetView)
        stats->countedAsRenderTargetView = true;

    switch (node.kind)
    {
    case PreparedFrameGraphNodeKind::RenderTargetShadow:
    case PreparedFrameGraphNodeKind::MainShadow:
        if (m_backend && exec->shadowPass.enabled)
        {
            m_backend->ExecuteRenderPass(exec->shadowPass.desc, m_registry,
                exec->shadowQueue, m_meshStore, m_matStore, m_shaderStore, m_texStore, &m_rtStore);
            stats->shadowPassExecuted = true;
        }
        break;

    case PreparedFrameGraphNodeKind::RenderTargetGraphics:
    case PreparedFrameGraphNodeKind::MainGraphics:
        if (m_backend && exec->graphicsPass.enabled)
        {
            m_backend->ExecuteRenderPass(exec->graphicsPass.desc, m_registry,
                exec->graphicsQueue, m_meshStore, m_matStore, m_shaderStore, m_texStore, &m_rtStore);
            stats->graphicsPassExecuted = true;
        }
        break;

    case PreparedFrameGraphNodeKind::MainPresentation:
        // Presentation reads from exec->presentation; stats->presentationExecuted
        // is written inside ExecutePreparedMainViewPresentation via the same stats pointer.
        // The view pointer is still needed here because ExecutePreparedMainViewPresentation
        // accesses prepared.frame for viewport dimensions — that is readonly Prepare data.
        if (node.view)
            ExecutePreparedMainViewPresentation(*node.view);
        break;
    }
}

void GDXECSRenderer::UpdatePreparedMainViewFrameTransient(ViewPassExecutionData& preparedView)
{
    auto& frameTransient = m_frameTransients[m_currentFrameIndex];
    constexpr size_t kApproxFrameConstantsBytes = 272u;
    constexpr size_t kApproxEntityConstantsBytes = 128u;
    (void)frameTransient.uploadArena.Allocate(kApproxFrameConstantsBytes, 16u);
    (void)frameTransient.uploadArena.Allocate(kApproxEntityConstantsBytes *
        (preparedView.opaqueQueue.Count() + preparedView.transparentQueue.Count() + preparedView.shadowQueue.Count()), 16u);
}

void GDXECSRenderer::AggregatePreparedFrameStats(const ViewPassExecutionData& mainView, const std::vector<ViewPassExecutionData>& rttViews)
{
    m_stats = {};
    m_stats.drawCalls = mainView.stats.drawCalls;
    m_stats.renderCommands = mainView.stats.renderCommands;
    m_stats.lightCount = mainView.stats.lightCount;
    m_stats.mainCulling = mainView.stats.graphicsCulling;
    m_stats.shadowCulling = mainView.stats.shadowCulling;
    m_stats.debugBoundsDraws = mainView.stats.debugBoundsDraws;
    m_stats.debugFrustumDraws = mainView.stats.debugFrustumDraws;

    for (const auto& preparedView : rttViews)
    {
        if (preparedView.stats.countedAsRenderTargetView)
            ++m_stats.rttViewCount;

        m_stats.debugBoundsDraws += preparedView.stats.debugBoundsDraws;
        m_stats.debugFrustumDraws += preparedView.stats.debugFrustumDraws;
    }
}

void GDXECSRenderer::ExecutePreparedFrame(RendererFramePipelineData& pipeline)
{
    if (!pipeline.frameGraph.validation.valid)
    {
        for (const auto& error : pipeline.frameGraph.validation.errors)
            DBERROR(GDX_SRC_LOC, error);
        return;
    }

    // Pre-pass: update lights and frame constants once per unique view, in execution
    // order. Reads only from the frozen executeInput snapshot — never touches node.view.
    if (m_backend)
    {
        const PreparedExecuteData* lastUpdatedExec = nullptr;
        for (uint32_t nodeIndex : pipeline.frameGraph.executionOrder)
        {
            if (nodeIndex >= pipeline.frameGraph.nodes.size())
                continue;
            const PreparedFrameGraphNode& node = pipeline.frameGraph.nodes[nodeIndex];
            if (!node.enabled || !node.executeInput)
                continue;
            if (node.kind == PreparedFrameGraphNodeKind::MainPresentation)
                continue; // presentation node shares its executeInput; constants already uploaded
            // Use node.view->prepared.frame for light/constant data — prepared.frame is
            // readonly Prepare-phase data that is safe to read here.
            if (node.executeInput != lastUpdatedExec && node.view)
            {
                m_backend->UpdateLights(m_registry, node.view->prepared.frame);
                m_backend->UpdateFrameConstants(node.view->prepared.frame);
                lastUpdatedExec = node.executeInput;
            }
        }
    }

    std::vector<bool> executed(pipeline.frameGraph.nodes.size(), false);
    for (uint32_t nodeIndex : pipeline.frameGraph.executionOrder)
    {
        if (nodeIndex >= pipeline.frameGraph.nodes.size())
        {
            DBERROR(GDX_SRC_LOC, "FrameGraph execution index out of range");
            return;
        }

        PreparedFrameGraphNode& node = pipeline.frameGraph.nodes[nodeIndex];
        bool dependenciesReady = true;
        for (uint32_t dep : node.dependencies)
        {
            if (dep >= executed.size() || !executed[dep])
            {
                dependenciesReady = false;
                break;
            }
        }

        if (!dependenciesReady)
        {
            DBERROR(GDX_SRC_LOC, "FrameGraph dependency not satisfied before node execution");
            return;
        }

        ExecutePreparedFrameGraphNode(node);
        executed[nodeIndex] = true;
    }

    pipeline.mainView.stats.drawCalls = m_backend ? m_backend->GetDrawCallCount() : 0u;
    pipeline.mainView.stats.renderCommands = pipeline.mainView.execute.graphicsPass.enabled ? static_cast<uint32_t>(pipeline.mainView.execute.graphicsQueue.Count()) : 0u;
    pipeline.mainView.stats.lightCount = pipeline.mainView.prepared.frame.lightCount;

    UpdatePreparedMainViewFrameTransient(pipeline.mainView);

    AggregatePreparedFrameStats(pipeline.mainView, pipeline.rttViews);
    LogDebugCullingStats();

    m_framePhase = RenderFramePhase::ExecuteSubmit;
    if (m_backend && pipeline.mainView.execute.presentation.presentAfterExecute)
        m_backend->Present(true);
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
    // RTT Cull/Gather writes only SR_RTT_VIEWS; Main Cull/Gather writes only SR_MAIN_VIEW.
    // The scheduler can therefore batch one RTT task and one Main task together.
    m_systemScheduler.AddTask({ "Cull RTT Graphics",
        SR_RTT_VIEWS | SR_TRANSFORM,
        SR_RTT_VIEWS,
        [this]() { CullPreparedRenderTargetGraphicsViews(m_renderPipeline.rttViews); } });
    m_systemScheduler.AddTask({ "Cull RTT Shadows",
        SR_RTT_VIEWS | SR_TRANSFORM,
        SR_RTT_VIEWS,
        [this]() { CullPreparedRenderTargetShadowViews(m_renderPipeline.rttViews); } });
    m_systemScheduler.AddTask({ "Cull Main Graphics",
        SR_MAIN_VIEW | SR_TRANSFORM,
        SR_MAIN_VIEW,
        [this]() { CullPreparedMainViewGraphics(m_renderPipeline.mainView); } });
    m_systemScheduler.AddTask({ "Cull Main Shadows",
        SR_MAIN_VIEW | SR_TRANSFORM,
        SR_MAIN_VIEW,
        [this]() { CullPreparedMainViewShadow(m_renderPipeline.mainView); } });
    m_systemScheduler.AddTask({ "Gather RTT Graphics",
        SR_RTT_VIEWS | SR_TRANSFORM,
        SR_RTT_VIEWS,
        [this, &resolveShader]() { GatherPreparedRenderTargetGraphicsViews(resolveShader, m_renderPipeline.rttViews); } });
    m_systemScheduler.AddTask({ "Gather RTT Shadows",
        SR_RTT_VIEWS | SR_TRANSFORM,
        SR_RTT_VIEWS,
        [this, &resolveShader]() { GatherPreparedRenderTargetShadowViews(resolveShader, m_renderPipeline.rttViews); } });
    m_systemScheduler.AddTask({ "Gather Main Graphics",
        SR_MAIN_VIEW | SR_TRANSFORM,
        SR_MAIN_VIEW,
        [this, &resolveShader]() { GatherPreparedMainViewGraphics(resolveShader, m_renderPipeline.mainView); } });
    m_systemScheduler.AddTask({ "Gather Main Shadows",
        SR_MAIN_VIEW | SR_TRANSFORM,
        SR_MAIN_VIEW,
        [this, &resolveShader]() { GatherPreparedMainViewShadow(resolveShader, m_renderPipeline.mainView); } });
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
        [this]() { BuildPreparedFrameGraph(m_renderPipeline); } });
    m_systemScheduler.AddTask({ "Execute Prepared Frame",
        SR_RENDER_QUEUES | SR_MAIN_VIEW | SR_RTT_VIEWS | SR_TRANSFORM | SR_BACKEND,
        SR_BACKEND | SR_STATS,
        [this]() { ExecutePreparedFrame(m_renderPipeline); } });
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

    m_shaderVariantCache.clear();
    m_postProcessPassOrder.clear();
    m_initialized = false;
}