#include "GDXECSRenderer.h"
#include "GDXDX11RenderBackend.h"
#include "Debug.h"

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
    m_defaultShader = CreateShader(
        L"ECSVertexShader.hlsl", L"ECSPixelShader.hlsl", GDX_VERTEX_DEFAULT);
    if (!m_defaultShader.IsValid()) return false;

    m_shadowShader = CreateShader(
        L"ECSShadowVertexShader.hlsl", L"ECSShadowPixelShader.hlsl", GDX_VERTEX_POSITION);

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

    if (m_frameData.hasShadowPass && m_shadowShader.IsValid() && m_backend && m_backend->HasShadowResources())
    {
        m_gatherSystem.GatherShadow(m_registry, m_frameData,
            m_meshStore,
            MaterialHandle::Invalid(),
            m_shadowShader,
            m_shadowQueue);

        if (!m_shadowQueue.Empty())
            m_backend->ExecuteShadowPass(m_shadowQueue, m_meshStore, m_shaderStore, m_frameData);
    }

    m_gatherSystem.Gather(m_registry, m_frameData,
        m_meshStore, m_matStore,
        m_defaultShader, m_opaqueQueue);

    if (m_backend)
        m_backend->ExecuteMainPass(m_opaqueQueue, m_meshStore, m_matStore, m_shaderStore, m_texStore);

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

    m_initialized = false;
}