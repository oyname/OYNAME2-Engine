#include "GDXDX11RenderBackend.h"
#include "GDXRenderTargetResource.h"
#include "GDXVertexFlags.h"
#include "Debug.h"
#include "GDXResourceState.h"

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
    void ExecuteMainPassCommon(
        ID3D11DeviceContext* ctx,
        ID3D11RasterizerState* rasterizerState,
        ID3D11DepthStencilState* depthStencilState,
        ID3D11DepthStencilState* depthStateNoWrite,
        ID3D11BlendState* blendState,
        ID3D11BlendState* blendStateAlpha,
        GDXSamplerCache& samplerCache,
        GDXDX11RenderExecutor& executor,
        GDXShadowMap& shadowMap,
        bool hasShadowPass,
        Registry& registry,
        const RenderQueue& opaqueQueue,
        ResourceStore<MeshAssetResource, MeshTag>& meshStore,
        ResourceStore<MaterialResource, MaterialTag>& matStore,
        ResourceStore<GDXShaderResource, ShaderTag>& shaderStore,
        ResourceStore<GDXTextureResource, TextureTag>& texStore)
    {
        if (!ctx) return;

        const float bf[4] = { 0,0,0,0 };

        RenderQueue solidQueue;
        RenderQueue transparentQueue;
        for (const auto& cmd : opaqueQueue.commands)
        {
            if (cmd.pass == RenderPass::Transparent) transparentQueue.commands.push_back(cmd);
            else solidQueue.commands.push_back(cmd);
        }

        void* shadowSrv = (hasShadowPass && shadowMap.IsReady()) ? shadowMap.GetSRV() : nullptr;

        ctx->RSSetState(rasterizerState);
        ctx->OMSetDepthStencilState(depthStencilState, 0u);
        ctx->OMSetBlendState(blendState, bf, 0xFFFFFFFF);
        samplerCache.BindAll(ctx);
        if (!solidQueue.Empty())
            executor.ExecuteQueue(registry, solidQueue, meshStore, matStore, shaderStore, texStore,
                shadowSrv);

        ctx->OMSetDepthStencilState(depthStateNoWrite, 0u);
        ctx->OMSetBlendState(blendStateAlpha, bf, 0xFFFFFFFF);
        if (!transparentQueue.Empty())
            executor.ExecuteQueue(registry, transparentQueue, meshStore, matStore, shaderStore, texStore,
                shadowSrv);

        ctx->OMSetDepthStencilState(depthStencilState, 0u);
        ctx->OMSetBlendState(blendState, bf, 0xFFFFFFFF);
    }

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
        if (flags & GDX_VERTEX_TEX1)        add("TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT);
        if (flags & GDX_VERTEX_TEX2)        add("TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT);
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
    m_backbufferWidth = 1200;
    m_backbufferHeight = 650;
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

    // Rasterizer States nach Init() übergeben (CreateRenderStates läuft vorher)
    m_executor.SetRasterizerStates(m_rasterizerState, m_rasterizerStateNoCull);
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
    m_backbufferWidth = w;
    m_backbufferHeight = h;
    if (m_context) m_context->Resize(w, h);
}

ShaderHandle GDXDX11RenderBackend::CreateShader(
    ResourceStore<GDXShaderResource, ShaderTag>& shaderStore,
    const std::wstring& vsFile,
    const std::wstring& psFile,
    uint32_t vertexFlags,
    const GDXShaderLayout& shaderLayout,
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

    ID3D11InputLayout* inputLayout = nullptr;
    if (FAILED(BuildInputLayout(m_device, vertexFlags, vsBlob, &inputLayout)))
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
        inputLayout->Release();
        return ShaderHandle::Invalid();
    }

    ID3D11PixelShader* ps = nullptr;
    if (FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &ps)))
    {
        psBlob->Release();
        vs->Release();
        inputLayout->Release();
        return ShaderHandle::Invalid();
    }
    psBlob->Release();

    GDXShaderResource res;
    res.vertexFlags = vertexFlags;
    res.layout = shaderLayout;
    res.vertexShader = vs;
    res.pixelShader = ps;
    res.inputLayout = inputLayout;
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

TextureHandle GDXDX11RenderBackend::CreateTextureFromImage(
    ResourceStore<GDXTextureResource, TextureTag>& texStore,
    const ImageBuffer& image,
    bool isSRGB,
    const std::wstring& debugName,
    TextureHandle fallbackOnFailure)
{
    if (!m_device || !image.IsValid() || !image.Data())
        return fallbackOnFailure;

    GDXTextureResource res;
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = image.Width();
    td.Height = image.Height();
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = isSRGB ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA init = {};
    init.pSysMem = image.Data();
    init.SysMemPitch = image.Width() * 4u;

    ID3D11Texture2D* tex = nullptr;
    if (FAILED(m_device->CreateTexture2D(&td, &init, &tex)))
        return fallbackOnFailure;

    D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
    sd.Format = td.Format;
    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sd.Texture2D.MipLevels = 1;

    ID3D11ShaderResourceView* srv = nullptr;
    const HRESULT hr = m_device->CreateShaderResourceView(tex, &sd, &srv);
    tex->Release();
    if (FAILED(hr))
        return fallbackOnFailure;

    res.srv = srv;
    res.width = image.Width();
    res.height = image.Height();
    res.ready = true;
    res.isSRGB = isSRGB;
    res.semantic = GDXTextureSemantic::Procedural;
    res.debugName = debugName;
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
    m_hasShadowPass = frame.hasShadowPass;
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
    if (!m_ctx || !m_context) return nullptr;

    auto* rtv = static_cast<ID3D11RenderTargetView*>(m_context->GetRenderTarget());
    auto* dsv = static_cast<ID3D11DepthStencilView*>(m_context->GetDepthStencil());
    m_ctx->OMSetRenderTargets(1, &rtv, dsv);

    ExecuteMainPassCommon(
        m_ctx,
        m_rasterizerState,
        m_depthStencilState,
        m_depthStateNoWrite,
        m_blendState,
        m_blendStateAlpha,
        m_samplerCache,
        m_executor,
        m_shadowMap,
        m_hasShadowPass,
        registry,
        opaqueQueue,
        meshStore,
        matStore,
        shaderStore,
        texStore);

    return nullptr;
}

void* GDXDX11RenderBackend::ExecuteMainPassToTarget(
    GDXRenderTargetResource& rt,
    const RenderPassClearDesc& clearDesc,
    Registry& registry,
    const RenderQueue& opaqueQueue,
    ResourceStore<MeshAssetResource, MeshTag>& meshStore,
    ResourceStore<MaterialResource, MaterialTag>& matStore,
    ResourceStore<GDXShaderResource, ShaderTag>& shaderStore,
    ResourceStore<GDXTextureResource, TextureTag>& texStore)
{
    if (!m_ctx || !rt.ready || !rt.rtv || !rt.dsv) return nullptr;

    auto* rtv = static_cast<ID3D11RenderTargetView*>(rt.rtv);
    auto* dsv = static_cast<ID3D11DepthStencilView*>(rt.dsv);

    ID3D11ShaderResourceView* nullSrvs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
    m_ctx->PSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, nullSrvs);

    m_ctx->OMSetRenderTargets(1, &rtv, dsv);

    if (rt.exposedTexture.IsValid())
        m_executor.TransitionTexture(rt.exposedTexture, ResourceState::ShaderRead, ResourceState::RenderTarget, "ExecuteMainPassToTarget begin");

    if (clearDesc.clearColorEnabled)
        m_ctx->ClearRenderTargetView(rtv, clearDesc.clearColor);

    UINT depthStencilFlags = 0u;
    if (clearDesc.clearDepthEnabled) depthStencilFlags |= D3D11_CLEAR_DEPTH;
    if (clearDesc.clearStencilEnabled) depthStencilFlags |= D3D11_CLEAR_STENCIL;
    if (depthStencilFlags != 0u)
        m_ctx->ClearDepthStencilView(dsv, depthStencilFlags, clearDesc.clearDepthValue, clearDesc.clearStencilValue);

    D3D11_VIEWPORT vp = {};
    vp.Width = static_cast<float>(rt.width);
    vp.Height = static_cast<float>(rt.height);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    m_ctx->RSSetViewports(1, &vp);

    ExecuteMainPassCommon(
        m_ctx,
        m_rasterizerState,
        m_depthStencilState,
        m_depthStateNoWrite,
        m_blendState,
        m_blendStateAlpha,
        m_samplerCache,
        m_executor,
        m_shadowMap,
        m_hasShadowPass,
        registry,
        opaqueQueue,
        meshStore,
        matStore,
        shaderStore,
        texStore);

    m_ctx->PSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, nullSrvs);

    if (rt.exposedTexture.IsValid())
        m_executor.TransitionTexture(rt.exposedTexture, ResourceState::RenderTarget, ResourceState::ShaderRead, "ExecuteMainPassToTarget end");

    if (m_context)
    {
        auto* backbufferRTV = static_cast<ID3D11RenderTargetView*>(m_context->GetRenderTarget());
        auto* backbufferDSV = static_cast<ID3D11DepthStencilView*>(m_context->GetDepthStencil());
        m_ctx->OMSetRenderTargets(1, &backbufferRTV, backbufferDSV);

        D3D11_VIEWPORT vp = {};
        vp.TopLeftX = 0.0f;
        vp.TopLeftY = 0.0f;
        vp.Width = static_cast<float>(m_backbufferWidth);
        vp.Height = static_cast<float>(m_backbufferHeight);
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        m_ctx->RSSetViewports(1, &vp);
    }
    return nullptr;
}

uint32_t GDXDX11RenderBackend::GetDrawCallCount() const
{
    return m_executor.GetDrawCallCount();
}

bool GDXDX11RenderBackend::HasShadowResources() const
{
    return m_shadowMap.GetDSV() != nullptr && m_shadowMap.GetSRV() != nullptr;
}

const IGDXRenderBackend::DefaultTextureSet& GDXDX11RenderBackend::GetDefaultTextures() const
{
    return m_defaultTextures;
}

bool GDXDX11RenderBackend::CreateRenderStates()
{
    if (!m_device) return false;

    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_BACK;
    rd.FrontCounterClockwise = FALSE;
    rd.DepthClipEnable = TRUE;
    if (FAILED(m_device->CreateRasterizerState(&rd, &m_rasterizerState))) return false;

    // CULL_NONE für MF_DOUBLE_SIDED / MF_ALPHA_TEST – gleiche Bias-/Clip-Einstellungen
    D3D11_RASTERIZER_DESC rdNoCull = rd;
    rdNoCull.CullMode = D3D11_CULL_NONE;
    if (FAILED(m_device->CreateRasterizerState(&rdNoCull, &m_rasterizerStateNoCull))) return false;

    D3D11_DEPTH_STENCIL_DESC dsd = {};
    dsd.DepthEnable = TRUE;
    dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dsd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    if (FAILED(m_device->CreateDepthStencilState(&dsd, &m_depthStencilState))) return false;

    D3D11_DEPTH_STENCIL_DESC dsdNoWrite = {};
    dsdNoWrite.DepthEnable = TRUE;
    dsdNoWrite.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    dsdNoWrite.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    if (FAILED(m_device->CreateDepthStencilState(&dsdNoWrite, &m_depthStateNoWrite))) return false;

    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0].BlendEnable = FALSE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_ZERO;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(m_device->CreateBlendState(&bd, &m_blendState))) return false;

    D3D11_BLEND_DESC alphaBd = {};
    alphaBd.RenderTarget[0].BlendEnable = TRUE;
    alphaBd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    alphaBd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    alphaBd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    alphaBd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    alphaBd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    alphaBd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    alphaBd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(m_device->CreateBlendState(&alphaBd, &m_blendStateAlpha))) return false;

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

    matStore.ForEach([](MaterialHandle, MaterialResource& mat)
        {
            if (mat.gpuConstantBuffer)
            {
                static_cast<ID3D11Buffer*>(mat.gpuConstantBuffer)->Release();
                mat.gpuConstantBuffer = nullptr;
            }
        });

    shaderStore.ForEach([](ShaderHandle, GDXShaderResource& sh)
        {
            if (sh.vertexShader) { static_cast<ID3D11VertexShader*>(sh.vertexShader)->Release(); sh.vertexShader = nullptr; }
            if (sh.pixelShader) { static_cast<ID3D11PixelShader*>(sh.pixelShader)->Release(); sh.pixelShader = nullptr; }
            if (sh.inputLayout) { static_cast<ID3D11InputLayout*>(sh.inputLayout)->Release(); sh.inputLayout = nullptr; }
        });

    texStore.ForEach([](TextureHandle, GDXTextureResource& tex)
        {
            if (tex.srv)
            {
                static_cast<ID3D11ShaderResourceView*>(tex.srv)->Release();
                tex.srv = nullptr;
            }
        });

    if (m_blendStateAlpha) { m_blendStateAlpha->Release(); m_blendStateAlpha = nullptr; }
    if (m_blendState) { m_blendState->Release(); m_blendState = nullptr; }
    if (m_depthStateNoWrite) { m_depthStateNoWrite->Release(); m_depthStateNoWrite = nullptr; }
    if (m_depthStencilState) { m_depthStencilState->Release(); m_depthStencilState = nullptr; }
    if (m_rasterizerState) { m_rasterizerState->Release();       m_rasterizerState = nullptr; }
    if (m_rasterizerStateNoCull) { m_rasterizerStateNoCull->Release(); m_rasterizerStateNoCull = nullptr; }

    m_shadowMap.Release();
    m_samplerCache.Shutdown();
    m_meshUploader.reset();
    m_ctx = nullptr;
    m_device = nullptr;
    m_context.reset();
}

// ---------------------------------------------------------------------------
// CreateRenderTarget — Offscreen RTT (DX11)
// ---------------------------------------------------------------------------
RenderTargetHandle GDXDX11RenderBackend::CreateRenderTarget(
    ResourceStore<GDXRenderTargetResource, RenderTargetTag>& rtStore,
    ResourceStore<GDXTextureResource, TextureTag>& texStore,
    uint32_t width, uint32_t height,
    const std::wstring& debugName)
{
    if (!m_device || !m_ctx) return RenderTargetHandle::Invalid();

    HRESULT hr = S_OK;

    // --- Farb-Textur (RGBA8) ---
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    ID3D11Texture2D* colorTex = nullptr;
    hr = m_device->CreateTexture2D(&texDesc, nullptr, &colorTex);
    if (FAILED(hr)) { return RenderTargetHandle::Invalid(); }

    ID3D11RenderTargetView* rtv = nullptr;
    hr = m_device->CreateRenderTargetView(colorTex, nullptr, &rtv);
    if (FAILED(hr)) { colorTex->Release(); return RenderTargetHandle::Invalid(); }

    ID3D11ShaderResourceView* srv = nullptr;
    hr = m_device->CreateShaderResourceView(colorTex, nullptr, &srv);
    if (FAILED(hr)) { rtv->Release(); colorTex->Release(); return RenderTargetHandle::Invalid(); }

    // --- Depth-Textur ---
    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = width;
    depthDesc.Height = height;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    ID3D11Texture2D* depthTex = nullptr;
    hr = m_device->CreateTexture2D(&depthDesc, nullptr, &depthTex);
    if (FAILED(hr)) { srv->Release(); rtv->Release(); colorTex->Release(); return RenderTargetHandle::Invalid(); }

    ID3D11DepthStencilView* dsv = nullptr;
    hr = m_device->CreateDepthStencilView(depthTex, nullptr, &dsv);
    if (FAILED(hr)) { depthTex->Release(); srv->Release(); rtv->Release(); colorTex->Release(); return RenderTargetHandle::Invalid(); }

    // --- SRV als Engine-Textur registrieren ---
    GDXTextureResource texRes;
    texRes.srv = srv;
    texRes.width = width;
    texRes.height = height;
    texRes.ready = true;
    texRes.isSRGB = false;
    texRes.semantic = GDXTextureSemantic::RenderTarget;
    texRes.debugName = debugName + L"_Tex";
    TextureHandle exposedTex = texStore.Add(std::move(texRes));
    if (exposedTex.IsValid())
        m_executor.TransitionTexture(exposedTex, ResourceState::Unknown, ResourceState::ShaderRead, "CreateRenderTarget initial state");

    // --- RenderTargetResource anlegen ---
    GDXRenderTargetResource rt;
    rt.colorTexture = colorTex;
    rt.rtv = rtv;
    rt.srv = srv;
    rt.depthTexture = depthTex;
    rt.dsv = dsv;
    rt.width = width;
    rt.height = height;
    rt.ready = true;
    rt.exposedTexture = exposedTex;
    rt.debugName = debugName;

    return rtStore.Add(std::move(rt));
}
