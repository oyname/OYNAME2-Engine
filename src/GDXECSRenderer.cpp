#include "GDXECSRenderer.h"
#include "GDXDX11RenderBackend.h"
#include "GDXRenderTargetResource.h"
#include "Debug.h"
#include "GDXShaderLayout.h"
#include "RenderPassTargetDesc.h"

#include <DirectXMath.h>

namespace
{
    constexpr uint32_t kRelevantMainFeatures = SVF_SKINNED | SVF_VERTEX_COLOR;
    constexpr uint32_t kRelevantShadowFeatures = SVF_SKINNED | SVF_ALPHA_TEST;

    bool BuildFrameDataFromCameraEntity(Registry& registry, EntityID cameraEntity, FrameData& frame)
    {
        using namespace DirectX;

        const auto* wt = registry.Get<WorldTransformComponent>(cameraEntity);
        const auto* cam = registry.Get<CameraComponent>(cameraEntity);
        if (!wt || !cam) return false;

        const XMMATRIX world = XMLoadFloat4x4(&wt->matrix);
        const XMVECTOR position = world.r[3];
        XMStoreFloat3(&frame.cameraPos, position);
        frame.cullMask = cam->cullMask;

        XMMATRIX rot = world;
        rot.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);

        const XMVECTOR baseForward = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
        const XMVECTOR baseUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

        const XMVECTOR forward = XMVector3Normalize(XMVector3TransformNormal(baseForward, rot));
        const XMVECTOR up = XMVector3Normalize(XMVector3TransformNormal(baseUp, rot));
        const XMVECTOR target = XMVectorAdd(position, forward);

        const XMMATRIX view = XMMatrixLookAtLH(position, target, up);
        XMStoreFloat4x4(&frame.viewMatrix, view);

        XMMATRIX proj;
        if (cam->isOrtho)
        {
            proj = XMMatrixOrthographicLH(cam->orthoWidth, cam->orthoHeight, cam->nearPlane, cam->farPlane);
        }
        else
        {
            const float fovRad = cam->fovDeg * (XM_PI / 180.0f);
            proj = XMMatrixPerspectiveFovLH(fovRad, cam->aspectRatio, cam->nearPlane, cam->farPlane);
        }

        XMStoreFloat4x4(&frame.projMatrix, proj);
        XMStoreFloat4x4(&frame.viewProjMatrix, XMMatrixMultiply(view, proj));
        return true;
    }
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
        ? GDXShaderLayouts::BuildShadow(vertexFlags, skinned)
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

RenderTargetHandle GDXECSRenderer::CreateRenderTarget(uint32_t w, uint32_t h, const std::wstring& name)
{
    if (!m_backend) return RenderTargetHandle::Invalid();
    return m_backend->CreateRenderTarget(m_rtStore, m_texStore, w, h, name);
}

TextureHandle GDXECSRenderer::GetRenderTargetTexture(RenderTargetHandle h)
{
    if (auto* rt = m_rtStore.Get(h))
        return rt->exposedTexture;
    return m_defaultWhiteTex;
}

void GDXECSRenderer::SetShadowMapSize(uint32_t size)
{
    if (auto* dx11 = dynamic_cast<GDXDX11RenderBackend*>(m_backend.get()))
        dx11->SetShadowMapSize(size);
}

void GDXECSRenderer::SetClearColor(float r, float g, float b, float a)
{
    m_clearColor[0] = r;
    m_clearColor[1] = g;
    m_clearColor[2] = b;
    m_clearColor[3] = a;
}

void GDXECSRenderer::BeginFrame()
{
    m_currentFrameIndex = (m_currentFrameIndex + 1u) % GDXMaxFramesInFlight;
    m_frameContexts[m_currentFrameIndex].Begin(m_currentFrameIndex, ++m_frameNumber, &m_frameData);
    m_frameTransients[m_currentFrameIndex].BeginFrame();

    if (m_backend)
        m_backend->BeginFrame(m_clearColor);
}

void GDXECSRenderer::Tick(float dt)
{
    if (m_tickCallback) m_tickCallback(dt);
}

void GDXECSRenderer::EndFrame()
{
    m_transformSystem.Update(m_registry);

    auto resolveShader = [this](RenderPass pass, const SubmeshData& submesh, const MaterialResource& mat)
    {
        return ResolveShaderVariant(pass, submesh, mat);
    };

    m_registry.View<CameraComponent, RenderTargetCameraComponent>(
        [this, &resolveShader](EntityID entity, CameraComponent& cam, RenderTargetCameraComponent& rtCam)
        {
            if (!m_backend || !rtCam.enabled || !rtCam.target.IsValid()) return;

            GDXRenderTargetResource* rt = m_rtStore.Get(rtCam.target);
            if (!rt || !rt->ready) return;

            CameraComponent originalCam = cam;
            if (rtCam.autoAspectFromTarget && rt->height > 0u)
                cam.aspectRatio = static_cast<float>(rt->width) / static_cast<float>(rt->height);

            FrameData rtFrame = m_frameData;
            rtFrame.viewportWidth = static_cast<float>(rt->width);
            rtFrame.viewportHeight = static_cast<float>(rt->height);

            const bool built = BuildFrameDataFromCameraEntity(m_registry, entity, rtFrame);
            cam = originalCam;
            if (!built) return;

            m_backend->UpdateLights(m_registry, rtFrame);
            m_backend->UpdateFrameConstants(rtFrame);

            RenderGatherOptions rtGatherOptions{};
            rtGatherOptions.gatherShadows = rtCam.renderShadows;
            rtGatherOptions.gatherOpaque = rtCam.renderOpaque;
            rtGatherOptions.gatherTransparent = rtCam.renderTransparent;
            rtGatherOptions.skipSelfReferentialDraws = rtCam.skipSelfReferentialDraws;
            rtGatherOptions.forbiddenShaderReadTexture = rt->exposedTexture;
            rtGatherOptions.visibilityLayerMask = rtFrame.cullMask;
            rtGatherOptions.shadowCasterLayerMask = rtFrame.shadowCasterMask;

            if (rtFrame.hasShadowPass && m_backend->HasShadowResources())
            {
                m_gatherSystem.GatherShadow(m_registry, rtFrame,
                    m_meshStore, m_matStore,
                    resolveShader,
                    m_shadowQueue,
                    &rtGatherOptions);

                if (!m_shadowQueue.Empty())
                    m_backend->ExecuteShadowPass(m_registry, m_shadowQueue, m_meshStore, m_matStore, m_shaderStore, m_texStore, rtFrame);
            }

            m_gatherSystem.Gather(m_registry, rtFrame,
                m_meshStore, m_matStore,
                resolveShader, m_opaqueQueue,
                &rtGatherOptions);

            RenderPassTargetDesc targetDesc = RenderPassTargetDesc::Offscreen(rtCam.target, rtCam.clear,
                static_cast<float>(rt->width), static_cast<float>(rt->height), rt->debugName);
            m_backend->ExecutePass(targetDesc, m_registry, m_opaqueQueue,
                m_meshStore, m_matStore, m_shaderStore, m_texStore, &m_rtStore);
        });

    m_cameraSystem.Update(m_registry, m_frameData);
    if (m_backend) m_backend->UpdateLights(m_registry, m_frameData);
    if (m_backend) m_backend->UpdateFrameConstants(m_frameData);

    if (m_frameData.hasShadowPass && m_backend && m_backend->HasShadowResources())
    {
        RenderGatherOptions mainShadowOptions{};
        mainShadowOptions.shadowCasterLayerMask = m_frameData.shadowCasterMask;
        m_gatherSystem.GatherShadow(m_registry, m_frameData,
            m_meshStore, m_matStore,
            resolveShader,
            m_shadowQueue,
            &mainShadowOptions);

        if (!m_shadowQueue.Empty())
            m_backend->ExecuteShadowPass(m_registry, m_shadowQueue, m_meshStore, m_matStore, m_shaderStore, m_texStore, m_frameData);
    }

    m_gatherSystem.Gather(m_registry, m_frameData,
        m_meshStore, m_matStore,
        resolveShader, m_opaqueQueue,
        nullptr);

    if (m_backend)
    {
        RenderPassTargetDesc mainTarget = RenderPassTargetDesc::Backbuffer(m_frameData.viewportWidth, m_frameData.viewportHeight);
        m_backend->ExecutePass(mainTarget, m_registry, m_opaqueQueue, m_meshStore, m_matStore, m_shaderStore, m_texStore, &m_rtStore);
    }

    auto& frameTransient = m_frameTransients[m_currentFrameIndex];
    constexpr size_t kApproxFrameConstantsBytes = 272u;
    constexpr size_t kApproxEntityConstantsBytes = 128u;
    (void)frameTransient.uploadArena.Allocate(kApproxFrameConstantsBytes, 16u);
    (void)frameTransient.uploadArena.Allocate(kApproxEntityConstantsBytes * (m_opaqueQueue.Count() + m_shadowQueue.Count()), 16u);

    m_stats.drawCalls = m_backend ? m_backend->GetDrawCallCount() : 0u;
    m_stats.renderCommands = static_cast<uint32_t>(m_opaqueQueue.Count());
    m_stats.lightCount = m_frameData.lightCount;

    if (m_backend) m_backend->Present(true);

    m_frameContexts[m_currentFrameIndex].MarkSubmitted(m_frameNumber);
    m_frameContexts[m_currentFrameIndex].MarkCompleted(m_frameNumber);
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

    m_frameData.viewportWidth = static_cast<float>(w);
    m_frameData.viewportHeight = static_cast<float>(h);
}

void GDXECSRenderer::Shutdown()
{
    if (!m_initialized && !m_backend) return;

    if (m_backend)
    {
        m_backend->Shutdown(m_matStore, m_shaderStore, m_texStore);
        m_backend.reset();
    }

    m_shaderVariantCache.clear();
    m_initialized = false;
}
