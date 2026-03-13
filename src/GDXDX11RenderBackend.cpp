#include "GDXDX11RenderBackend.h"
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
#include <vector>

#pragma comment(lib, "d3dcompiler.lib")

bool GDXTextureLoader_LoadFromFile(ID3D11Device*, ID3D11DeviceContext*,
    const wchar_t*, GDXTextureResource&, bool isSRGB);
bool GDXTextureLoader_Create1x1(ID3D11Device*, uint8_t, uint8_t, uint8_t, uint8_t, GDXTextureResource&);

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
            exe / L"shader" / file,
            exe / L".." / L"shader" / file,
            exe / L"..\\..\\shader" / file,
            exe / L"..\\..\\..\\" / L"shader" / file,
            std::filesystem::current_path() / L"shader" / file,
            std::filesystem::current_path() / L".." / L"shader" / file,
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
        if (!out) return false;
        *out = nullptr;

        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
        ID3DBlob* err = nullptr;
        const HRESULT hr = D3DCompileFromFile(
            path.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
            entry.c_str(), target.c_str(), flags, 0, out, &err);

        if (FAILED(hr))
        {
            if (err)
            {
                const char* msg = static_cast<const char*>(err->GetBufferPointer());
                Debug::LogError("HLSL compile failed: ", path, " [", entry.c_str(), " / ", target.c_str(), "] ", msg ? msg : "");
                err->Release();
            }
            else
            {
                Debug::LogError("HLSL compile failed: ", path, " [", entry.c_str(), " / ", target.c_str(), "] (no compiler message)");
            }
            return false;
        }

        if (err) err->Release();
        return true;
    }

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

        if (flags & GDX_VERTEX_POSITION)     add("POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT);
        if (flags & GDX_VERTEX_NORMAL)       add("NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT);
        if (flags & GDX_VERTEX_COLOR)        add("COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT);
        if (flags & GDX_VERTEX_TEX1)
        {
            // UV0 immer emittieren.
            // UV1 (TEXCOORD1) ebenfalls immer — alle Shader die TEX1 nutzen
            // deklarieren TEXCOORD1 als VS_INPUT (UV2-Patch).
            // Ob echte UV2-Daten vorliegen entscheidet der Executor:
            // kein GDX_VERTEX_TEX2 → UV0-Buffer wird auf Slot 1 aliasiert.
            add("TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT);   // UV0
            add("TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT);   // UV1 (oder Alias)
        }
        // GDX_VERTEX_TEX2 allein (ohne TEX1) ist kein gültiger Zustand — ignorieren.
        if (flags & GDX_VERTEX_TANGENT)      add("TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT);
        if (flags & GDX_VERTEX_BONE_INDICES) add("BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_UINT);
        if (flags & GDX_VERTEX_BONE_WEIGHTS) add("BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT);

        if (elems.empty() || !vsBlob) return E_INVALIDARG;
        return device->CreateInputLayout(elems.data(), (UINT)elems.size(),
            vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), out);
    }
}

GDXDX11RenderBackend::GDXDX11RenderBackend(std::unique_ptr<IGDXDXGIContext> context)
    : m_context(std::move(context))
{
}

GDXDX11RenderBackend::~GDXDX11RenderBackend() = default;

bool GDXDX11RenderBackend::Initialize(ResourceStore<GDXTextureResource, TextureTag>& texStore)
{
    if (!m_context || !m_context->IsValid()) return false;

    m_device = static_cast<ID3D11Device*>(m_context->GetDevice());
    m_ctx = static_cast<ID3D11DeviceContext*>(m_context->GetDeviceContext());
    if (!m_device || !m_ctx) return false;

    if (!CreateRenderStates()) return false;
    if (!m_samplerCache.Init(m_device)) return false;
    if (!InitDefaultTextures(texStore)) return false;
    if (!m_shadowMap.Create(m_device, m_shadowMapSize)) return false;
    if (!m_lightSystem.Init(m_device)) return false;

    m_meshUploader = std::make_unique<GDXDX11MeshUploader>(m_device, m_ctx);

    GDXDX11RenderExecutor::InitParams ep{};
    ep.device = m_device;
    ep.context = m_ctx;
    if (!m_executor.Init(ep)) return false;

    m_executor.defaultWhiteTex = m_defaultTextures.white;
    m_executor.defaultNormalTex = m_defaultTextures.normal;
    m_executor.defaultORMTex = m_defaultTextures.orm;
    m_executor.defaultBlackTex = m_defaultTextures.black;

    return true;
}

void GDXDX11RenderBackend::BeginFrame(const float clearColor[4])
{
    if (!m_context || !m_ctx) return;

    auto* rtv = static_cast<ID3D11RenderTargetView*>(m_context->GetRenderTarget());
    auto* dsv = static_cast<ID3D11DepthStencilView*>(m_context->GetDepthStencil());

    m_ctx->OMSetRenderTargets(1, &rtv, dsv);
    m_ctx->ClearRenderTargetView(rtv, clearColor);
    m_ctx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

    m_ctx->RSSetState(m_rasterizerState);
    m_ctx->OMSetDepthStencilState(m_depthStencilState, 0u);
    const float bf[4] = { 0,0,0,0 };
    m_ctx->OMSetBlendState(m_blendState, bf, 0xFFFFFFFF);
    m_samplerCache.BindAll(m_ctx);
}

void GDXDX11RenderBackend::Present(bool vsync)
{
    if (m_context) m_context->Present(vsync);
}

void GDXDX11RenderBackend::Resize(int w, int h)
{
    if (m_context) m_context->Resize(w, h);
}

ShaderHandle GDXDX11RenderBackend::CreateShader(
    ResourceStore<GDXShaderResource, ShaderTag>& shaderStore,
    const std::wstring& vsFile,
    const std::wstring& psFile,
    uint32_t vertexFlags,
    const std::wstring& debugName)
{
    const auto vsPath = FindShaderPath(vsFile);
    const auto psPath = FindShaderPath(psFile);
    if (vsPath.empty() || psPath.empty() || !m_device) return ShaderHandle::Invalid();

    ID3DBlob* vsBlob = nullptr;
    if (!CompileFromFile(vsPath.wstring(), "main", "vs_5_0", &vsBlob))
        return ShaderHandle::Invalid();

    ID3D11VertexShader* vs = nullptr;
    if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vs)))
    {
        vsBlob->Release();
        return ShaderHandle::Invalid();
    }

    ID3D11InputLayout* layout = nullptr;
    if (FAILED(BuildInputLayout(m_device, vertexFlags, vsBlob, &layout)))
    {
        vs->Release();
        vsBlob->Release();
        return ShaderHandle::Invalid();
    }
    vsBlob->Release();

    ID3DBlob* psBlob = nullptr;
    if (!CompileFromFile(psPath.wstring(), "main", "ps_5_0", &psBlob))
    {
        vs->Release();
        layout->Release();
        return ShaderHandle::Invalid();
    }

    ID3D11PixelShader* ps = nullptr;
    if (FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &ps)))
    {
        psBlob->Release();
        vs->Release();
        layout->Release();
        return ShaderHandle::Invalid();
    }
    psBlob->Release();

    GDXShaderResource res;
    res.vertexFlags = vertexFlags;
    res.vertexShader = vs;
    res.pixelShader = ps;
    res.inputLayout = layout;
    res.debugName = debugName;
    return shaderStore.Add(std::move(res));
}

TextureHandle GDXDX11RenderBackend::CreateTexture(
    ResourceStore<GDXTextureResource, TextureTag>& texStore,
    const std::wstring& filePath,
    bool isSRGB,
    TextureHandle fallbackOnFailure)
{
    GDXTextureResource res;
    if (!GDXTextureLoader_LoadFromFile(m_device, m_ctx, filePath.c_str(), res, isSRGB))
    {
        Debug::LogError(GDX_SRC_LOC, L"LoadTexture fehlgeschlagen: ", filePath.c_str());
        return fallbackOnFailure;
    }
    return texStore.Add(std::move(res));
}

bool GDXDX11RenderBackend::UploadMesh(MeshAssetResource& mesh)
{
    return m_meshUploader ? m_meshUploader->Upload(mesh) : false;
}

bool GDXDX11RenderBackend::CreateMaterialGpu(MaterialResource& mat)
{
    if (!m_device) return false;

    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeof(MaterialData);
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    D3D11_SUBRESOURCE_DATA init = {};
    init.pSysMem = &mat.data;

    ID3D11Buffer* buf = nullptr;
    if (FAILED(m_device->CreateBuffer(&desc, &init, &buf))) return false;

    mat.gpuConstantBuffer = buf;
    mat.cpuDirty = false;
    return true;
}

void GDXDX11RenderBackend::UpdateLights(Registry& registry, FrameData& frame)
{
    m_lightSystem.Update(registry, frame, m_ctx);
}

void GDXDX11RenderBackend::UpdateFrameConstants(const FrameData& frame)
{
    m_executor.UpdateFrameConstants(frame);
}

void GDXDX11RenderBackend::ExecuteShadowPass(
    Registry& registry,
    const RenderQueue& shadowQueue,
    ResourceStore<MeshAssetResource, MeshTag>& meshStore,
    ResourceStore<MaterialResource, MaterialTag>& matStore,
    ResourceStore<GDXShaderResource, ShaderTag>& shaderStore,
    ResourceStore<GDXTextureResource, TextureTag>& texStore,
    const FrameData& frame)
{
    if (!m_shadowMap.IsReady() || shadowQueue.Empty()) return;

    m_shadowMap.BeginPass(m_ctx);
    m_executor.ExecuteShadowQueue(registry, shadowQueue, meshStore, matStore, shaderStore, texStore);
    m_shadowMap.EndPass(m_ctx);

    auto* rtv = static_cast<ID3D11RenderTargetView*>(m_context->GetRenderTarget());
    auto* dsv = static_cast<ID3D11DepthStencilView*>(m_context->GetDepthStencil());
    m_ctx->OMSetRenderTargets(1, &rtv, dsv);

    D3D11_VIEWPORT vp = {};
    vp.Width = frame.viewportWidth;
    vp.Height = frame.viewportHeight;
    vp.MaxDepth = 1.0f;
    m_ctx->RSSetViewports(1, &vp);
    m_ctx->RSSetState(m_rasterizerState);
    m_samplerCache.BindAll(m_ctx);
}

void* GDXDX11RenderBackend::ExecuteMainPass(
    Registry& registry,
    const RenderQueue& opaqueQueue,
    ResourceStore<MeshAssetResource, MeshTag>& meshStore,
    ResourceStore<MaterialResource, MaterialTag>& matStore,
    ResourceStore<GDXShaderResource, ShaderTag>& shaderStore,
    ResourceStore<GDXTextureResource, TextureTag>& texStore)
{
    void* shadowSRV = m_shadowMap.IsReady() ? m_shadowMap.GetSRV() : nullptr;
    m_executor.ExecuteQueue(registry, opaqueQueue, meshStore, matStore, shaderStore, texStore, shadowSRV);
    return shadowSRV;
}

uint32_t GDXDX11RenderBackend::GetDrawCallCount() const
{
    return m_executor.GetDrawCallCount();
}

bool GDXDX11RenderBackend::HasShadowResources() const
{
    return m_shadowMap.IsReady();
}

const IGDXRenderBackend::DefaultTextureSet& GDXDX11RenderBackend::GetDefaultTextures() const
{
    return m_defaultTextures;
}

bool GDXDX11RenderBackend::CreateRenderStates()
{
    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_BACK;
    rd.FrontCounterClockwise = FALSE;
    rd.DepthClipEnable = TRUE;
    if (FAILED(m_device->CreateRasterizerState(&rd, &m_rasterizerState))) return false;

    D3D11_DEPTH_STENCIL_DESC dsd = {};
    dsd.DepthEnable = TRUE;
    dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dsd.DepthFunc = D3D11_COMPARISON_LESS;
    if (FAILED(m_device->CreateDepthStencilState(&dsd, &m_depthStencilState))) return false;

    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0].BlendEnable = FALSE;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(m_device->CreateBlendState(&bd, &m_blendState))) return false;

    return true;
}

bool GDXDX11RenderBackend::InitDefaultTextures(ResourceStore<GDXTextureResource, TextureTag>& texStore)
{
    auto make1x1 = [&](uint8_t r, uint8_t g, uint8_t b, uint8_t a,
        TextureHandle& handle, const wchar_t* name) -> bool
        {
            GDXTextureResource res;
            res.debugName = name;
            if (!GDXTextureLoader_Create1x1(m_device, r, g, b, a, res)) return false;
            handle = texStore.Add(std::move(res));
            return handle.IsValid();
        };

    return make1x1(255, 255, 255, 255, m_defaultTextures.white, L"Default_White")
        && make1x1(128, 128, 255, 255, m_defaultTextures.normal, L"Default_FlatNormal")
        && make1x1(255, 128, 0, 255, m_defaultTextures.orm, L"Default_ORM")
        && make1x1(0, 0, 0, 255, m_defaultTextures.black, L"Default_Black");
}

void GDXDX11RenderBackend::Shutdown(
    ResourceStore<MaterialResource, MaterialTag>& matStore,
    ResourceStore<GDXShaderResource, ShaderTag>& shaderStore,
    ResourceStore<GDXTextureResource, TextureTag>& texStore)
{
    m_executor.Shutdown();
    m_lightSystem.Shutdown();
    m_samplerCache.Shutdown();
    m_shadowMap.Release();

    matStore.ForEach([](MaterialHandle, MaterialResource& mat)
        {
            if (mat.gpuConstantBuffer)
            {
                static_cast<ID3D11Buffer*>(mat.gpuConstantBuffer)->Release();
                mat.gpuConstantBuffer = nullptr;
            }
        });

    shaderStore.ForEach([](ShaderHandle, GDXShaderResource& s)
        {
            if (s.vertexShader) { static_cast<ID3D11VertexShader*>(s.vertexShader)->Release(); s.vertexShader = nullptr; }
            if (s.pixelShader) { static_cast<ID3D11PixelShader*> (s.pixelShader)->Release();  s.pixelShader = nullptr; }
            if (s.inputLayout) { static_cast<ID3D11InputLayout*> (s.inputLayout)->Release();  s.inputLayout = nullptr; }
        });

    texStore.ForEach([](TextureHandle, GDXTextureResource& tex)
        {
            if (tex.srv)
            {
                static_cast<ID3D11ShaderResourceView*>(tex.srv)->Release();
                tex.srv = nullptr;
            }
        });

    m_meshUploader.reset();

    if (m_rasterizerState) { m_rasterizerState->Release();   m_rasterizerState = nullptr; }
    if (m_depthStencilState) { m_depthStencilState->Release(); m_depthStencilState = nullptr; }
    if (m_blendState) { m_blendState->Release();        m_blendState = nullptr; }

    m_context.reset();
    m_device = nullptr;
    m_ctx = nullptr;
}
