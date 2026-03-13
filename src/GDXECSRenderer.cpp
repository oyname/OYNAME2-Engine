#include "GDXECSRenderer.h"
#include "GDXDX11RenderBackend.h"
#include "Debug.h"

namespace
{
    constexpr uint32_t kRelevantMainFeatures = SVF_SKINNED | SVF_VERTEX_COLOR;
    constexpr uint32_t kRelevantShadowFeatures = SVF_SKINNED | SVF_ALPHA_TEST;
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
    return LoadShaderInternal(vsFile, psFile, vertexFlags, vsFile + L" / " + psFile);
}

ShaderHandle GDXECSRenderer::LoadShaderInternal(
    const std::wstring& vsFile,
    const std::wstring& psFile,
    uint32_t vertexFlags,
    const std::wstring& debugName)
{
    if (!m_backend) return ShaderHandle::Invalid();
    return m_backend->CreateShader(m_shaderStore, vsFile, psFile, vertexFlags, debugName);
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
        if (skinned && vertexColor)
        {
            vsFile = L"ECSVertexShader_SkinnedVertexColor.hlsl";
            psFile = L"ECSPixelShader_VertexColor.hlsl";
        }
        else if (skinned)
        {
            vsFile = L"ECSVertexShader_Skinned.hlsl";
            psFile = L"ECSPixelShader.hlsl";
        }
        else if (vertexColor)
        {
            vsFile = L"ECSVertexShader_VertexColor.hlsl";
            psFile = L"ECSPixelShader_VertexColor.hlsl";
        }
        else
        {
            vsFile = L"ECSVertexShader.hlsl";
            psFile = L"ECSPixelShader.hlsl";
        }
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
    ShaderHandle handle = LoadShaderInternal(vsFile, psFile, vertexFlags, debugName);
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

MeshHandle GDXECSRenderer::UploadMesh(MeshAssetResource mesh)
{
    MeshHandle h = m_meshStore.Add(std::move(mesh));
    if (auto* r = m_meshStore.Get(h); r && m_backend)
        m_backend->UploadMesh(*r);
    return h;
}

MaterialHandle GDXECSRenderer::CreateMaterial(MaterialResource mat)
{
    MaterialHandle h = m_matStore.Add(std::move(mat));
    if (auto* r = m_matStore.Get(h))
    {
        r->sortID = h.Index();
        if (m_backend) m_backend->CreateMaterialGpu(*r);
    }
    return h;
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
    m_cameraSystem.Update(m_registry, m_frameData);
    if (m_backend) m_backend->UpdateLights(m_registry, m_frameData);
    if (m_backend) m_backend->UpdateFrameConstants(m_frameData);

    auto resolveShader = [this](RenderPass pass, const SubmeshData& submesh, const MaterialResource& mat)
    {
        return ResolveShaderVariant(pass, submesh, mat);
    };

    if (m_frameData.hasShadowPass && m_backend && m_backend->HasShadowResources())
    {
        m_gatherSystem.GatherShadow(m_registry, m_frameData,
            m_meshStore, m_matStore,
            resolveShader,
            m_shadowQueue);

        if (!m_shadowQueue.Empty())
            m_backend->ExecuteShadowPass(m_registry, m_shadowQueue, m_meshStore, m_matStore, m_shaderStore, m_texStore, m_frameData);
    }

    m_gatherSystem.Gather(m_registry, m_frameData,
        m_meshStore, m_matStore,
        resolveShader, m_opaqueQueue);

    if (m_backend)
        m_backend->ExecuteMainPass(m_registry, m_opaqueQueue, m_meshStore, m_matStore, m_shaderStore, m_texStore);

    m_stats.drawCalls = m_backend ? m_backend->GetDrawCallCount() : 0u;
    m_stats.renderCommands = static_cast<uint32_t>(m_opaqueQueue.Count());
    m_stats.lightCount = m_frameData.lightCount;

    if (m_backend) m_backend->Present(true);
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
