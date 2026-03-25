#include "GDXDebugCullingRenderer.h"
#include "Core/GDXMath.h"
#include "Core/GDXMathOps.h"
#include "GDXPipelineState.h"
#include "GDXShaderLayout.h"
#include "GDXTextureSlots.h"
#include "GDXResourceBinding.h"
#include "BasicMeshGenerator.h"
#include "CameraSystem.h"
#include "Core/Debug.h"

#include <sstream>
#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------------------
// Helpers (file-local, same as in original GDXECSRenderer.cpp)
// ---------------------------------------------------------------------------
namespace
{
    Float3 LerpPoint(const Float3& a, const Float3& b, float t)
    {
        return { a.x+(b.x-a.x)*t, a.y+(b.y-a.y)*t, a.z+(b.z-a.z)*t };
    }

    Matrix4 BuildBoxWorldMatrix(const Float3& center, const Float3& scale)
    {
        Matrix4 m = Matrix4::Identity();
        m._11 = scale.x; m._22 = scale.y; m._33 = scale.z;
        m._41 = center.x; m._42 = center.y; m._43 = center.z;
        return m;
    }

    bool BuildFrustumCorners(const Matrix4& viewProj, Float3 outCorners[8])
    {
        if (GDX::Determinant(viewProj) == 0.f) return false;
        const Matrix4 inv = GDX::Inverse(viewProj);
        const Float3 ndc[8] = {
            {-1,-1,0},{-1,1,0},{1,1,0},{1,-1,0},
            {-1,-1,1},{-1,1,1},{1,1,1},{1,-1,1},
        };
        for (int i = 0; i < 8; ++i)
        {
            const Float4 p = GDX::TransformFloat4({ndc[i].x,ndc[i].y,ndc[i].z,1.f}, inv);
            if (std::abs(p.w) <= 1e-6f) return false;
            const float iw = 1.f/p.w;
            outCorners[i] = { p.x*iw, p.y*iw, p.z*iw };
        }
        return true;
    }

    Matrix4 BuildEdgeWorldMatrix(const Float3& a, const Float3& b, float thickness)
    {
        const Float3 center  = LerpPoint(a, b, 0.5f);
        const Float3 forward = GDX::Normalize3(GDX::Subtract(b,a), {0,0,1});
        Float3 upRef = {0,1,0};
        if (std::abs(GDX::Dot3(forward, upRef)) > 0.98f) upRef = {1,0,0};
        const Float3 right = GDX::Normalize3(GDX::Cross(upRef, forward), {1,0,0});
        const Float3 up    = GDX::Normalize3(GDX::Cross(forward, right),  {0,1,0});
        const float len = (std::max)(GDX::Length3(GDX::Subtract(b,a)), 0.01f);
        Matrix4 m{};
        m._11=right.x*thickness;    m._12=right.y*thickness;    m._13=right.z*thickness;    m._14=0;
        m._21=up.x*thickness;       m._22=up.y*thickness;       m._23=up.z*thickness;       m._24=0;
        m._31=forward.x*(len*.5f);  m._32=forward.y*(len*.5f);  m._33=forward.z*(len*.5f);  m._34=0;
        m._41=center.x; m._42=center.y; m._43=center.z; m._44=1;
        return m;
    }

    ResourceBindingSet BuildDebugBindings(const MaterialResource& mat, const GDXShaderResource& shader)
    {
        ResourceBindingSet set;
        for (uint32_t i = 0; i < shader.layout.constantBufferCount; ++i)
        {
            const auto& src = shader.layout.constantBuffers[i];
            ConstantBufferBindingDesc cb{};
            cb.semantic   = src.slot;
            cb.vsRegister = src.vsRegister;
            cb.psRegister = src.psRegister;
            cb.buffer     = (src.slot == GDXShaderConstantBufferSlot::Material) ? mat.gpuConstantBuffer : nullptr;
            cb.enabled    = (src.slot != GDXShaderConstantBufferSlot::Material) || (mat.gpuConstantBuffer != nullptr);
            cb.scope      = (src.slot == GDXShaderConstantBufferSlot::Frame)
                          ? ResourceBindingScope::Pass
                          : ((src.slot == GDXShaderConstantBufferSlot::Material)
                             ? ResourceBindingScope::Material : ResourceBindingScope::Draw);
            set.AddConstantBufferBinding(cb);
        }
        for (uint32_t i = 0; i < shader.layout.textureBindingCount; ++i)
        {
            const auto& src = shader.layout.textureBindings[i];
            ShaderResourceBindingDesc desc{};
            switch (src.semantic)
            {
            case GDXShaderTextureSemantic::Albedo:    desc.semantic = ShaderResourceSemantic::Albedo;    break;
            case GDXShaderTextureSemantic::Normal:    desc.semantic = ShaderResourceSemantic::Normal;    break;
            case GDXShaderTextureSemantic::ORM:       desc.semantic = ShaderResourceSemantic::ORM;       break;
            case GDXShaderTextureSemantic::Emissive:  desc.semantic = ShaderResourceSemantic::Emissive;  break;
            case GDXShaderTextureSemantic::Detail:    desc.semantic = ShaderResourceSemantic::Detail;    break;
            case GDXShaderTextureSemantic::ShadowMap: desc.semantic = ShaderResourceSemantic::ShadowMap; break;
            }
            desc.bindingIndex  = src.shaderRegister;
            desc.enabled       = false;
            desc.requiredState = ResourceState::ShaderRead;
            desc.scope         = (desc.semantic == ShaderResourceSemantic::ShadowMap)
                               ? ResourceBindingScope::Pass : ResourceBindingScope::Material;
            set.AddTextureBinding(desc);
        }
        return set;
    }

    // -----------------------------------------------------------------------
    // LineList-Hilfsfunktionen — 1-Pixel-Linien via D3D11_PRIMITIVE_TOPOLOGY_LINELIST.
    // Je 2 aufeinanderfolgende Vertices = ein Segment, kein Index-Buffer nötig.
    // -----------------------------------------------------------------------
    void PushLine(SubmeshData& mesh, const Float3& a, const Float3& b)
    {
        mesh.positions.push_back(a); mesh.normals.push_back({0,1,0}); mesh.uv0.push_back({0,0});
        mesh.positions.push_back(b); mesh.normals.push_back({0,1,0}); mesh.uv0.push_back({0,0});
    }

    // 12 Kanten des Frustum-Pyramidenstumpfs.
    SubmeshData BuildFrustumLineList(const Float3 c[8])
    {
        SubmeshData mesh;
        // Nahebene
        PushLine(mesh,c[0],c[1]); PushLine(mesh,c[1],c[2]);
        PushLine(mesh,c[2],c[3]); PushLine(mesh,c[3],c[0]);
        // Fernebene
        PushLine(mesh,c[4],c[5]); PushLine(mesh,c[5],c[6]);
        PushLine(mesh,c[6],c[7]); PushLine(mesh,c[7],c[4]);
        // Verbindungskanten
        PushLine(mesh,c[0],c[4]); PushLine(mesh,c[1],c[5]);
        PushLine(mesh,c[2],c[6]); PushLine(mesh,c[3],c[7]);
        return mesh;
    }

    // 12 Kanten einer AABB (aus Sphere-Mittelpunkt + Radius approximiert).
    SubmeshData BuildAABBLineList(const Float3& center, float radius)
    {
        const float r = radius;
        const Float3& o = center;
        const Float3 v[8] = {
            {o.x-r,o.y-r,o.z-r},{o.x+r,o.y-r,o.z-r},
            {o.x+r,o.y+r,o.z-r},{o.x-r,o.y+r,o.z-r},
            {o.x-r,o.y-r,o.z+r},{o.x+r,o.y-r,o.z+r},
            {o.x+r,o.y+r,o.z+r},{o.x-r,o.y+r,o.z+r},
        };
        SubmeshData mesh;
        PushLine(mesh,v[0],v[1]); PushLine(mesh,v[1],v[2]);
        PushLine(mesh,v[2],v[3]); PushLine(mesh,v[3],v[0]);
        PushLine(mesh,v[4],v[5]); PushLine(mesh,v[5],v[6]);
        PushLine(mesh,v[6],v[7]); PushLine(mesh,v[7],v[4]);
        PushLine(mesh,v[0],v[4]); PushLine(mesh,v[1],v[5]);
        PushLine(mesh,v[2],v[6]); PushLine(mesh,v[3],v[7]);
        return mesh;
    }

} // namespace


// ---------------------------------------------------------------------------

bool GDXDebugCullingRenderer::EnsureResources(
    std::function<MeshHandle(MeshAssetResource)>    uploadMesh,
    std::function<MaterialHandle(MaterialResource)> createMat,
    ShaderHandle defaultShader)
{
    if (!defaultShader.IsValid()) return false;

    if (!m_debugBoxMesh.IsValid())
    {
        MeshAssetResource mesh;
        mesh.debugName = "DebugBox";
        mesh.AddSubmesh(BuiltinMeshes::Cube());
        m_debugBoxMesh = uploadMesh(std::move(mesh));
    }

    // Bounds + Frustum: solid, unlit, kein Alpha — LineList braucht keine Transparenz.
    auto makeMat = [&](float r, float g, float b) -> MaterialHandle
    {
        MaterialResource mat = MaterialResource::FlatColor(r, g, b, 1.0f);
        mat.data.flags = MF_UNLIT | MF_DOUBLE_SIDED;
        mat.data.receiveShadows = 0.f;
        return createMat(std::move(mat));
    };

    if (!m_mainBoundsMat.IsValid())   m_mainBoundsMat   = makeMat(0.10f, 0.95f, 0.20f);  // grün
    if (!m_shadowBoundsMat.IsValid()) m_shadowBoundsMat = makeMat(0.95f, 0.82f, 0.10f);  // gelb
    if (!m_rttBoundsMat.IsValid())    m_rttBoundsMat    = makeMat(0.12f, 0.45f, 1.00f);  // blau

    // Frustum-Wireframe: weiß/orange, solid (kein Alpha), unlit.
    auto makeFrustumMat = [&](float r, float g, float b) -> MaterialHandle
    {
        MaterialResource mat = MaterialResource::FlatColor(r, g, b, 1.0f);
        mat.data.flags = MF_UNLIT | MF_DOUBLE_SIDED;
        mat.data.receiveShadows = 0.f;
        return createMat(std::move(mat));
    };
    if (!m_mainFrustumMat.IsValid())   m_mainFrustumMat   = makeFrustumMat(1.0f, 1.0f, 1.0f);  // weiß
    if (!m_shadowFrustumMat.IsValid()) m_shadowFrustumMat = makeFrustumMat(1.0f, 0.55f, 0.1f); // orange

    return m_debugBoxMesh.IsValid()
        && m_mainBoundsMat.IsValid() && m_shadowBoundsMat.IsValid()
        && m_rttBoundsMat.IsValid()
        && m_mainFrustumMat.IsValid() && m_shadowFrustumMat.IsValid();
}

void GDXDebugCullingRenderer::AppendVisibleSet(
    RenderQueue& queue, const VisibleSet& set,
    const RenderViewData& view,
    const RenderContext& ctx,
    RFG::ViewStats* stats)
{
    if (!options.enabled) return;

    MaterialHandle boundsMat  = MaterialHandle::Invalid();
    MaterialHandle frustumMat = MaterialHandle::Invalid();
    bool drawBounds  = false;
    bool drawFrustum = false;

    switch (view.type)
    {
    case RenderViewType::Main:
        drawBounds  = options.drawMainVisibleBounds;  boundsMat  = m_mainBoundsMat;
        drawFrustum = options.drawMainFrustum;        frustumMat = m_mainFrustumMat;
        break;
    case RenderViewType::Shadow:
        drawBounds  = options.drawShadowVisibleBounds; boundsMat  = m_shadowBoundsMat;
        drawFrustum = options.drawShadowFrustum;       frustumMat = m_shadowFrustumMat;
        break;
    case RenderViewType::RenderTarget:
        drawBounds  = options.drawRttVisibleBounds;    boundsMat  = m_rttBoundsMat;
        break;
    }

    if (drawBounds)
        for (const auto& c : set.candidates)
            AppendBounds(queue, c, boundsMat, options.boundsAlpha, view.frame, ctx, stats);

    if (drawFrustum)
        AppendFrustum(queue, view, frustumMat, options.frustumAlpha, view.frame, ctx, stats);
}

void GDXDebugCullingRenderer::AppendBounds(
    RenderQueue& queue, const VisibleRenderCandidate& candidate,
    MaterialHandle mat, float /*alpha*/,
    const FrameData& frame, const RenderContext& ctx,
    RFG::ViewStats* stats)
{
    if (!candidate.hasBounds || !mat.IsValid()) return;
    if (!ctx.matStore || !ctx.shaderStore || !ctx.uploadFrustumMesh) return;

    const MaterialResource*  matRes    = ctx.matStore->Get(mat);
    const GDXShaderResource* shaderRes = ctx.shaderStore->Get(ctx.defaultShader);
    if (!matRes || !shaderRes) return;

    // AABB aus Bounding Sphere — LineList, ein Draw-Call.
    MeshAssetResource asset;
    asset.debugName = "BoundsLineList";
    asset.AddSubmesh(BuildAABBLineList(candidate.worldBoundsCenter, candidate.worldBoundsRadius));
    const MeshHandle lineMesh = ctx.uploadFrustumMesh(std::move(asset));
    if (!lineMesh.IsValid()) return;

    RenderCommand cmd{};
    cmd.mesh         = lineMesh;
    cmd.material     = mat;
    cmd.shader       = ctx.defaultShader;
    cmd.submeshIndex = 0u;
    cmd.ownerEntity  = candidate.entity;
    cmd.pass         = RenderPass::Opaque;
    cmd.worldMatrix  = Matrix4::Identity();
    cmd.materialData = matRes->data;
    cmd.materialData.baseColor.w = 1.f;

    const ResourceBindingSet bindings = BuildDebugBindings(*matRes, *shaderRes);
    cmd.SetBindings(bindings,
        BuildResourceBindingScopeKey(bindings, ResourceBindingScope::Pass,     cmd.shader.value),
        BuildResourceBindingScopeKey(bindings, ResourceBindingScope::Material, cmd.material.value),
        BuildResourceBindingScopeKey(bindings, ResourceBindingScope::Draw,     candidate.entity.value));

    GDXPipelineStateDesc pso{};
    pso.blendMode        = GDXBlendMode::Opaque;
    pso.cullMode         = GDXCullMode::None;
    pso.depthMode        = GDXDepthMode::ReadOnly;
    pso.depthTestEnabled = true;
    pso.topology         = GDXPrimitiveTopology::LineList;
    cmd.SetPipelineState(pso);

    const float depth = 1.f - CameraSystem::ComputeNDCDepth(cmd.worldMatrix, frame.viewProjMatrix);
    cmd.SetSortKey(RenderPass::Opaque,
        ctx.defaultShader.Index() & 0x0FFFu,
        GDXPipelineStateKey::FromDesc(pso).value & 0x00FFu, 0u, depth);

    queue.Submit(std::move(cmd));
    if (stats) ++stats->debugBoundsDraws;
}

void GDXDebugCullingRenderer::AppendFrustum(
    RenderQueue& queue, const RenderViewData& view,
    MaterialHandle mat, float /*alpha*/,
    const FrameData& frame, const RenderContext& ctx,
    RFG::ViewStats* stats)
{
    if (!mat.IsValid() || !ctx.matStore || !ctx.shaderStore) return;

    const Matrix4& vp = (view.type == RenderViewType::Shadow)
        ? view.frame.shadowViewProjMatrix : view.frame.viewProjMatrix;

    Float3 corners[8]{};
    if (!BuildFrustumCorners(vp, corners)) return;

    const MaterialResource*  matRes    = ctx.matStore->Get(mat);
    const GDXShaderResource* shaderRes = ctx.shaderStore->Get(ctx.defaultShader);
    if (!matRes || !shaderRes) return;

    // LineList — dynamisch pro Frame, ein Draw-Call für alle 12 Kanten.
    MeshAssetResource asset;
    asset.debugName = "FrustumLineList";
    asset.AddSubmesh(BuildFrustumLineList(corners));
    const MeshHandle wireMesh = ctx.uploadFrustumMesh(std::move(asset));
    if (!wireMesh.IsValid()) return;

    RenderCommand cmd{};
    cmd.mesh         = wireMesh;
    cmd.material     = mat;
    cmd.shader       = ctx.defaultShader;
    cmd.submeshIndex = 0u;
    cmd.ownerEntity  = NULL_ENTITY;
    cmd.pass         = RenderPass::Opaque;
    cmd.worldMatrix  = Matrix4::Identity();
    cmd.materialData = matRes->data;
    cmd.materialData.baseColor = { 1.f, 1.f, 1.f, 1.f };

    const ResourceBindingSet bindings = BuildDebugBindings(*matRes, *shaderRes);
    cmd.SetBindings(bindings,
        BuildResourceBindingScopeKey(bindings, ResourceBindingScope::Pass,     cmd.shader.value),
        BuildResourceBindingScopeKey(bindings, ResourceBindingScope::Material, cmd.material.value),
        BuildResourceBindingScopeKey(bindings, ResourceBindingScope::Draw,     0u));

    GDXPipelineStateDesc pso{};
    pso.blendMode        = GDXBlendMode::Opaque;
    pso.cullMode         = GDXCullMode::None;
    pso.depthMode        = GDXDepthMode::ReadOnly;
    pso.depthTestEnabled = true;
    pso.topology         = GDXPrimitiveTopology::LineList;  // ← 1-Pixel-Linien
    cmd.SetPipelineState(pso);

    const float depth = 1.f - CameraSystem::ComputeNDCDepth(cmd.worldMatrix, frame.viewProjMatrix);
    cmd.SetSortKey(RenderPass::Opaque,
        ctx.defaultShader.Index() & 0x0FFFu,
        GDXPipelineStateKey::FromDesc(pso).value & 0x00FFu, 0u, depth);

    queue.Submit(std::move(cmd));
    if (stats) stats->debugFrustumDraws += 1;
}

void GDXDebugCullingRenderer::LogStats(
    const RFG::ViewStats& mainStats,
    const std::vector<RFG::ViewStats>& rttStats,
    uint64_t frameNumber) const
{
    if (!options.enabled || !options.logStats || options.logEveryNFrames == 0u) return;
    if ((frameNumber % options.logEveryNFrames) != 0u) return;

    auto fmt = [](const ViewCullingStats& s)
    {
        std::ostringstream oss;
        oss << "total=" << s.totalCandidates << " visible=" << s.visibleCandidates
            << " inactive=" << s.culledByInactive << " layer=" << s.culledByLayer
            << " frustum=" << s.culledByFrustum << " distance=" << s.culledByDistance
            << " noBounds=" << s.missingBounds;
        return oss.str();
    };

    Debug::Log("DebugCulling Frame=", frameNumber,
        " | Main Graphics: ", fmt(mainStats.graphicsCulling),
        " | Main Shadow: ",   fmt(mainStats.shadowCulling));
}
