#include "GDXECSRenderer.h"
#include "GDXVertexFlags.h"
#include "Debug.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <array>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "d3dcompiler.lib")

// Texture loader (GDXTextureLoader.cpp)
bool GDXTextureLoader_LoadFromFile(ID3D11Device*, ID3D11DeviceContext*,
                                   const wchar_t*, GDXTextureResource&, bool isSRGB);
bool GDXTextureLoader_Create1x1(ID3D11Device*, uint8_t, uint8_t, uint8_t, uint8_t, GDXTextureResource&);

// ===========================================================================
// Anonymer Namespace — Shader-Hilfen (identisch zu vorheriger Version)
// ===========================================================================
namespace
{
    std::filesystem::path GetExeDir()
    {
        std::array<wchar_t, 4096> buf{};
        const DWORD len = GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
        if (len == 0 || len >= buf.size()) return std::filesystem::current_path();
        return std::filesystem::path(buf.data()).parent_path();
    }

    std::filesystem::path FindShaderPath(const std::wstring& file)
    {
        const auto exe = GetExeDir();
        const std::vector<std::filesystem::path> candidates =
        {
            exe / L"shader"   / file,
            exe / L".."       / L"shader" / file,
            exe / L"..\\..\\shader"         / file,
            exe / L"..\\..\\..\\"           / L"shader" / file,
            std::filesystem::current_path() / L"shader" / file,
            std::filesystem::current_path() / L".."     / L"shader" / file,
        };
        for (const auto& c : candidates)
        {
            std::error_code ec;
            if (std::filesystem::exists(c, ec))
                return std::filesystem::weakly_canonical(c, ec);
        }
        return {};
    }

    bool CompileFromFile(const std::wstring& path, const std::string& entry,
                         const std::string& target, ID3DBlob** out)
    {
        if (!out) return false; *out = nullptr;
        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
        ID3DBlob* err = nullptr;
        HRESULT hr = D3DCompileFromFile(path.c_str(), nullptr,
            D3D_COMPILE_STANDARD_FILE_INCLUDE,
            entry.c_str(), target.c_str(), flags, 0, out, &err);
        if (FAILED(hr))
        {
            if (err) { err->Release(); }
            return false;
        }
        if (err) err->Release();
        return true;
    }

    // InputLayout dynamisch aus GDXVertexFlags bauen (wie OYNAME InputLayoutManager)
    HRESULT BuildInputLayout(ID3D11Device* device, uint32_t flags,
                              ID3DBlob* vsBlob, ID3D11InputLayout** out)
    {
        std::vector<D3D11_INPUT_ELEMENT_DESC> elems;
        elems.reserve(8);
        UINT slot = 0u;

        auto add = [&](const char* sem, UINT idx, DXGI_FORMAT fmt)
        {
            elems.push_back({ sem, idx, fmt, slot++, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 });
        };

        if (flags & GDX_VERTEX_POSITION)     add("POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT);
        if (flags & GDX_VERTEX_NORMAL)       add("NORMAL",       0, DXGI_FORMAT_R32G32B32_FLOAT);
        if (flags & GDX_VERTEX_COLOR)        add("COLOR",        0, DXGI_FORMAT_R32G32B32A32_FLOAT);
        if (flags & GDX_VERTEX_TEX1)         add("TEXCOORD",     0, DXGI_FORMAT_R32G32_FLOAT);
        if (flags & GDX_VERTEX_TEX2)         add("TEXCOORD",     1, DXGI_FORMAT_R32G32_FLOAT);
        if (flags & GDX_VERTEX_TANGENT)      add("TANGENT",      0, DXGI_FORMAT_R32G32B32A32_FLOAT);
        if (flags & GDX_VERTEX_BONE_INDICES) add("BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_UINT);
        if (flags & GDX_VERTEX_BONE_WEIGHTS) add("BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT);

        if (elems.empty() || !vsBlob) return E_INVALIDARG;
        return device->CreateInputLayout(elems.data(), (UINT)elems.size(),
            vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), out);
    }
} // namespace

// ===========================================================================
// GDXECSRenderer
// ===========================================================================
GDXECSRenderer::GDXECSRenderer(std::unique_ptr<IGDXDXGIContext> ctx)
    : m_context(std::move(ctx)) {}

GDXECSRenderer::~GDXECSRenderer() { Shutdown(); }

bool GDXECSRenderer::Initialize()
{
    if (!m_context || !m_context->IsValid()) return false;

    m_device = static_cast<ID3D11Device*>(m_context->GetDevice());
    m_ctx    = static_cast<ID3D11DeviceContext*>(m_context->GetDeviceContext());
    if (!m_device || !m_ctx) return false;

    if (!CreateRenderStates())               return false;
    if (!m_samplerCache.Init(m_device))      return false;
    if (!InitDefaultTextures())              return false;
    if (!m_shadowMap.Create(m_device, m_shadowMapSize)) return false;
    if (!m_lightSystem.Init(m_device))       return false;

    m_meshUploader = new GDXDX11MeshUploader(m_device, m_ctx);

    GDXDX11RenderExecutor::InitParams ep;
    ep.device  = m_device;
    ep.context = m_ctx;
    if (!m_executor.Init(ep)) return false;

    // Default-Textur-Handles an Executor übergeben
    m_executor.defaultWhiteTex  = m_defaultWhiteTex;
    m_executor.defaultNormalTex = m_defaultNormalTex;
    m_executor.defaultORMTex    = m_defaultORMTex;
    m_executor.defaultBlackTex  = m_defaultBlackTex;

    if (!LoadDefaultShaders()) return false;

    m_initialized = true;
    return true;
}

// ---------------------------------------------------------------------------
// InitDefaultTextures — White, FlatNormal, ORM, Black (wie OYNAME TexturePool)
// ---------------------------------------------------------------------------
bool GDXECSRenderer::InitDefaultTextures()
{
    auto make1x1 = [&](uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                        TextureHandle& handle, const wchar_t* name) -> bool
    {
        GDXTextureResource res;
        res.debugName = name;
        if (!GDXTextureLoader_Create1x1(m_device, r, g, b, a, res)) return false;
        handle = m_texStore.Add(std::move(res));
        return handle.IsValid();
    };

    return make1x1(255, 255, 255, 255, m_defaultWhiteTex,  L"Default_White")
        && make1x1(128, 128, 255, 255, m_defaultNormalTex, L"Default_FlatNormal")
        && make1x1(255, 128,   0, 255, m_defaultORMTex,    L"Default_ORM")
        && make1x1(  0,   0,   0, 255, m_defaultBlackTex,  L"Default_Black");
}

// ---------------------------------------------------------------------------
// LoadDefaultShaders
// ---------------------------------------------------------------------------
bool GDXECSRenderer::LoadDefaultShaders()
{
    // Standard-Shader: POSITION + NORMAL + TEX1
    m_defaultShader = CreateShader(
        L"ECSVertexShader.hlsl", L"ECSPixelShader.hlsl", GDX_VERTEX_DEFAULT);

    if (!m_defaultShader.IsValid()) return false;

    // Shadow-Shader: nur POSITION (Depth-Only Pass)
    m_shadowShader = CreateShader(
        L"ECSShadowVertexShader.hlsl", L"ECSShadowPixelShader.hlsl",
        GDX_VERTEX_POSITION);

    // Shadow-Shader optional — Engine läuft auch ohne
    if (!m_shadowShader.IsValid())
        Debug::Log("GDXECSRenderer: Kein Shadow-Shader gefunden — Shadow Pass deaktiviert.");

    return true;
}

// ---------------------------------------------------------------------------
// CreateShader (öffentliche API)
// ---------------------------------------------------------------------------
ShaderHandle GDXECSRenderer::CreateShader(
    const std::wstring& vsFile, const std::wstring& psFile, uint32_t vertexFlags)
{
    return LoadShaderInternal(vsFile, psFile, vertexFlags, vsFile + L" / " + psFile);
}

ShaderHandle GDXECSRenderer::LoadShaderInternal(
    const std::wstring& vsFile, const std::wstring& psFile,
    uint32_t vertexFlags, const std::wstring& debugName)
{
    const auto vsPath = FindShaderPath(vsFile);
    const auto psPath = FindShaderPath(psFile);
    if (vsPath.empty() || psPath.empty()) return ShaderHandle::Invalid();

    ID3DBlob* vsBlob = nullptr;
    if (!CompileFromFile(vsPath.wstring(), "main", "vs_5_0", &vsBlob))
        return ShaderHandle::Invalid();

    ID3D11VertexShader* vs = nullptr;
    if (FAILED(m_device->CreateVertexShader(
            vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vs)))
    { vsBlob->Release(); return ShaderHandle::Invalid(); }

    ID3D11InputLayout* layout = nullptr;
    if (FAILED(BuildInputLayout(m_device, vertexFlags, vsBlob, &layout)))
    { vs->Release(); vsBlob->Release(); return ShaderHandle::Invalid(); }

    vsBlob->Release();

    ID3DBlob* psBlob = nullptr;
    if (!CompileFromFile(psPath.wstring(), "main", "ps_5_0", &psBlob))
    { vs->Release(); layout->Release(); return ShaderHandle::Invalid(); }

    ID3D11PixelShader* ps = nullptr;
    if (FAILED(m_device->CreatePixelShader(
            psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &ps)))
    { psBlob->Release(); vs->Release(); layout->Release(); return ShaderHandle::Invalid(); }
    psBlob->Release();

    GDXShaderResource res;
    res.vertexFlags  = vertexFlags;
    res.vertexShader = vs;
    res.pixelShader  = ps;
    res.inputLayout  = layout;
    res.debugName    = debugName;
    return m_shaderStore.Add(std::move(res));
}

// ---------------------------------------------------------------------------
// LoadTexture — gecacht per Dateiname (wie OYNAME TexturePool)
// ---------------------------------------------------------------------------
TextureHandle GDXECSRenderer::LoadTexture(const std::wstring& filePath, bool isSRGB)
{
    // Cache-Check: gleicher Pfad → gleicher Handle
    TextureHandle existing = TextureHandle::Invalid();
    m_texStore.ForEach([&](TextureHandle h, const GDXTextureResource& res)
    {
        if (!existing.IsValid() && res.debugName == filePath)
            existing = h;
    });
    if (existing.IsValid()) return existing;

    GDXTextureResource res;
    if (!GDXTextureLoader_LoadFromFile(m_device, m_ctx, filePath.c_str(), res, isSRGB))
    {
        Debug::LogError(GDX_SRC_LOC, L"LoadTexture fehlgeschlagen: ", filePath.c_str());
        return m_defaultWhiteTex;
    }
    return m_texStore.Add(std::move(res));
}

// ---------------------------------------------------------------------------
// CreateRenderStates
// ---------------------------------------------------------------------------
bool GDXECSRenderer::CreateRenderStates()
{
    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_BACK;
    rd.FrontCounterClockwise = FALSE; rd.DepthClipEnable = TRUE;
    if (FAILED(m_device->CreateRasterizerState(&rd, &m_rasterizerState))) return false;

    D3D11_DEPTH_STENCIL_DESC dsd = {};
    dsd.DepthEnable    = TRUE;
    dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dsd.DepthFunc      = D3D11_COMPARISON_LESS;
    if (FAILED(m_device->CreateDepthStencilState(&dsd, &m_depthStencilState))) return false;

    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0].BlendEnable           = FALSE;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(m_device->CreateBlendState(&bd, &m_blendState))) return false;

    return true;
}

bool GDXECSRenderer::CreateMaterialCBuffer(MaterialResource& mat)
{
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeof(MaterialData); desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER; desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    D3D11_SUBRESOURCE_DATA init = {}; init.pSysMem = &mat.data;
    ID3D11Buffer* buf = nullptr;
    if (FAILED(m_device->CreateBuffer(&desc, &init, &buf))) return false;
    mat.gpuConstantBuffer = buf; mat.cpuDirty = false;
    return true;
}

// ---------------------------------------------------------------------------
// Ressourcen-API
// ---------------------------------------------------------------------------
MeshHandle GDXECSRenderer::UploadMesh(MeshAssetResource mesh)
{
    MeshHandle h = m_meshStore.Add(std::move(mesh));
    if (auto* r = m_meshStore.Get(h); r && m_meshUploader)
        m_meshUploader->Upload(*r);
    return h;
}

MaterialHandle GDXECSRenderer::CreateMaterial(MaterialResource mat)
{
    MaterialHandle h = m_matStore.Add(std::move(mat));
    if (auto* r = m_matStore.Get(h))
    {
        r->sortID = h.Index();
        CreateMaterialCBuffer(*r);
    }
    return h;
}

void GDXECSRenderer::SetClearColor(float r, float g, float b, float a)
{
    m_clearColor[0]=r; m_clearColor[1]=g; m_clearColor[2]=b; m_clearColor[3]=a;
}

// ---------------------------------------------------------------------------
// BeginFrame
// ---------------------------------------------------------------------------
void GDXECSRenderer::BeginFrame()
{
    if (!m_context || !m_ctx) return;

    auto* rtv = static_cast<ID3D11RenderTargetView*>(m_context->GetRenderTarget());
    auto* dsv = static_cast<ID3D11DepthStencilView*>(m_context->GetDepthStencil());

    m_ctx->OMSetRenderTargets(1, &rtv, dsv);
    m_ctx->ClearRenderTargetView(rtv, m_clearColor);
    m_ctx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

    m_ctx->RSSetState(m_rasterizerState);
    m_ctx->OMSetDepthStencilState(m_depthStencilState, 0u);
    const float bf[4] = { 0,0,0,0 };
    m_ctx->OMSetBlendState(m_blendState, bf, 0xFFFFFFFF);

    // Geteilte Sampler einmal pro Frame binden (s0, s1, s2, s7)
    m_samplerCache.BindAll(m_ctx);
}

// ---------------------------------------------------------------------------
// EndFrame — Systeme, Shadow Pass, Haupt-Pass
// ---------------------------------------------------------------------------
void GDXECSRenderer::EndFrame()
{
    // --- Delta Time ---------------------------------------------------------
    const auto now = Clock::now();
    float dt = 0.0f;
    if (m_clockStarted)
    {
        dt = std::chrono::duration<float>(now - m_lastFrameTime).count();
        if (dt > 0.1f) dt = 0.1f;
    }
    m_lastFrameTime = now; m_clockStarted = true;

    if (m_tickCallback) m_tickCallback(dt);

    // --- ECS Systeme --------------------------------------------------------
    m_transformSystem.Update(m_registry);
    m_cameraSystem.Update(m_registry, m_frameData);
    m_lightSystem.Update(m_registry, m_frameData, m_ctx);

    m_executor.UpdateFrameConstants(m_frameData);

    // --- Shadow Pass (wenn ein Licht mit castShadows vorhanden) -----------
    void* shadowSRV = nullptr;

    if (m_frameData.hasShadowPass && m_shadowShader.IsValid() && m_shadowMap.IsReady())
    {
        m_gatherSystem.GatherShadow(m_registry, m_frameData,
                                    m_meshStore,
                                    MaterialHandle::Invalid(),
                                    m_shadowShader,
                                    m_shadowQueue);

        if (!m_shadowQueue.Empty())
        {
            m_shadowMap.BeginPass(m_ctx);

            m_executor.ExecuteShadowQueue(m_shadowQueue, m_meshStore, m_shaderStore);

            m_shadowMap.EndPass(m_ctx);

            // RTV + DSV für Haupt-Pass wiederherstellen
            auto* rtv = static_cast<ID3D11RenderTargetView*>(m_context->GetRenderTarget());
            auto* dsv = static_cast<ID3D11DepthStencilView*>(m_context->GetDepthStencil());
            m_ctx->OMSetRenderTargets(1, &rtv, dsv);

            // Viewport wieder auf Backbuffer-Größe setzen
            D3D11_VIEWPORT vp = {};
            vp.Width    = m_frameData.viewportWidth;
            vp.Height   = m_frameData.viewportHeight;
            vp.MaxDepth = 1.0f;
            m_ctx->RSSetViewports(1, &vp);
            m_ctx->RSSetState(m_rasterizerState);
            m_samplerCache.BindAll(m_ctx);

            shadowSRV = m_shadowMap.GetSRV();
        }
    }

    // --- Haupt-Pass (Opaque) -----------------------------------------------
    m_gatherSystem.Gather(m_registry, m_frameData,
                          m_meshStore, m_matStore,
                          m_defaultShader, m_opaqueQueue);

    m_executor.ExecuteQueue(m_opaqueQueue, m_meshStore, m_matStore,
                            m_shaderStore, m_texStore, shadowSRV);

    m_stats.drawCalls      = m_executor.GetDrawCallCount();
    m_stats.renderCommands = static_cast<uint32_t>(m_opaqueQueue.Count());
    m_stats.lightCount     = m_frameData.lightCount;

    if (m_context) m_context->Present(true);
}

void GDXECSRenderer::Resize(int w, int h)
{
    if (m_context) m_context->Resize(w, h);
    if (h > 0)
    {
        const float aspect = static_cast<float>(w) / static_cast<float>(h);
        m_registry.View<CameraComponent>([aspect](EntityID, CameraComponent& cam)
        { cam.aspectRatio = aspect; });
    }
    m_frameData.viewportWidth  = static_cast<float>(w);
    m_frameData.viewportHeight = static_cast<float>(h);
}

// ---------------------------------------------------------------------------
// Shutdown
// ---------------------------------------------------------------------------
void GDXECSRenderer::Shutdown()
{
    m_executor.Shutdown();
    m_lightSystem.Shutdown();
    m_samplerCache.Shutdown();
    m_shadowMap.Release();

    m_matStore.ForEach([](MaterialHandle, MaterialResource& mat)
    {
        if (mat.gpuConstantBuffer)
        {
            static_cast<ID3D11Buffer*>(mat.gpuConstantBuffer)->Release();
            mat.gpuConstantBuffer = nullptr;
        }
    });

    m_shaderStore.ForEach([](ShaderHandle, GDXShaderResource& s)
    {
        if (s.vertexShader) { static_cast<ID3D11VertexShader*>(s.vertexShader)->Release(); s.vertexShader = nullptr; }
        if (s.pixelShader)  { static_cast<ID3D11PixelShader*> (s.pixelShader)->Release();  s.pixelShader  = nullptr; }
        if (s.inputLayout)  { static_cast<ID3D11InputLayout*> (s.inputLayout)->Release();  s.inputLayout  = nullptr; }
    });

    // Texturen explizit freigeben — muss VOR m_context.reset() passieren.
    // Ohne diesen Block läuft der ResourceStore-Destruktor nach dem Device-Release
    // → ReleaseTextureSRV() crasht auf ungültigem COM-Objekt (Zugriffsverletzung).
    m_texStore.ForEach([](TextureHandle, GDXTextureResource& tex)
    {
        if (tex.srv)
        {
            static_cast<ID3D11ShaderResourceView*>(tex.srv)->Release();
            tex.srv = nullptr;
        }
    });

    delete m_meshUploader; m_meshUploader = nullptr;

    if (m_rasterizerState)   { m_rasterizerState->Release();   m_rasterizerState   = nullptr; }
    if (m_depthStencilState) { m_depthStencilState->Release(); m_depthStencilState = nullptr; }
    if (m_blendState)        { m_blendState->Release();        m_blendState        = nullptr; }

    m_context.reset();
    m_initialized = false;
}
