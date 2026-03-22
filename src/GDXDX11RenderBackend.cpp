#include "GDXDX11RenderBackend.h"
#include "GDXIBLBaker.h"
#include "GDXRenderTargetResource.h"
#include "GDXVertexFlags.h"
#include "Core/Debug.h"
#include "GDXResourceState.h"
#include "RenderQueue.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>

#include <array>
#include <cstring>
#include <filesystem>

namespace
{
    DXGI_FORMAT ToDxgiColorFormat(GDXTextureFormat format)
    {
        switch (format)
        {
        case GDXTextureFormat::RGBA8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case GDXTextureFormat::RGBA16_FLOAT:     return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case GDXTextureFormat::RGBA8_UNORM:
        default:                                 return DXGI_FORMAT_R8G8B8A8_UNORM;
        }
    }

    GDXTextureFormat MakeLinearPostProcessFormat(GDXTextureFormat format)
    {
        switch (format)
        {
        case GDXTextureFormat::RGBA16_FLOAT: return GDXTextureFormat::RGBA16_FLOAT;
        case GDXTextureFormat::RGBA8_UNORM_SRGB: return GDXTextureFormat::RGBA8_UNORM;
        case GDXTextureFormat::RGBA8_UNORM:
        default: return GDXTextureFormat::RGBA8_UNORM;
        }
    }
}
#include <string>
#include <system_error>
#include <vector>
#include <algorithm>

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
        bool iblValid, ID3D11ShaderResourceView* iblIrr, ID3D11ShaderResourceView* iblEnv, ID3D11ShaderResourceView* iblLut,
        Registry& registry,
        const ICommandList& opaqueQueue,
        const ICommandList& transparentQueue,
        ResourceStore<MeshAssetResource, MeshTag>& meshStore,
        ResourceStore<MaterialResource, MaterialTag>& matStore,
        ResourceStore<GDXShaderResource, ShaderTag>& shaderStore,
        ResourceStore<GDXTextureResource, TextureTag>& texStore)
    {
        if (!ctx) return;

        const float bf[4] = { 0,0,0,0 };
        void* shadowSrv = (hasShadowPass && shadowMap.IsReady()) ? shadowMap.GetSRV() : nullptr;

        ctx->RSSetState(rasterizerState);
        ctx->OMSetDepthStencilState(depthStencilState, 0u);
        ctx->OMSetBlendState(blendState, bf, 0xFFFFFFFF);
        samplerCache.BindAll(ctx);

        // IBL auf t17/t18/t19 binden (Irradiance, Prefiltered, BRDF-LUT)
        if (iblValid && iblIrr && iblEnv && iblLut)
        {
            ID3D11ShaderResourceView* iblSRVs[3] = { iblIrr, iblEnv, iblLut };
            ctx->PSSetShaderResources(17u, 3u, iblSRVs);
        }

        if (!opaqueQueue.Empty())
            executor.ExecuteQueue(registry, opaqueQueue, meshStore, matStore, shaderStore, texStore, shadowSrv);

        ctx->OMSetDepthStencilState(depthStateNoWrite, 0u);
        ctx->OMSetBlendState(blendStateAlpha, bf, 0xFFFFFFFF);
        if (!transparentQueue.Empty())
            executor.ExecuteQueue(registry, transparentQueue, meshStore, matStore, shaderStore, texStore, shadowSrv);

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

        if (!vsBlob) return E_INVALIDARG;
        if (elems.empty())
        {
            *out = nullptr;
            return S_OK;
        }
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
    if (!m_shadowMap.Create(m_device, m_shadowMapSize, GDXShadowMap::kMaxCascades)) return false;
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
    m_executor.SetRasterizerStates(m_rasterizerState, m_rasterizerStateNoCull, m_depthStencilState, m_depthStateNoWrite, m_blendState, m_blendStateAlpha);
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
    ReleasePostProcessSurface(m_postProcessPing);
    ReleasePostProcessSurface(m_postProcessPong);
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

void GDXDX11RenderBackend::ExtractLightData(Registry& registry, FrameData& frame)
{
    // CPU-only: scan ECS, fill FrameData light arrays and shadow matrices.
    // No GPU upload here — that happens in UploadLightConstants at execution time.
    m_lightSystem.FillFrameData(registry, frame);
}

void GDXDX11RenderBackend::UploadLightConstants(const FrameData& frame)
{
    m_lightSystem.UploadLightBuffer(frame, m_ctx);
}

void GDXDX11RenderBackend::UpdateFrameConstants(const FrameData& frame)
{
    m_hasShadowPass = frame.hasShadowPass;
    m_executor.UpdateFrameConstants(frame);
    m_executor.UpdateCascadeConstants(frame);
}

void* GDXDX11RenderBackend::ExecuteRenderPass(
    const BackendRenderPassDesc& passDesc,
    Registry& registry,
    const ICommandList& commandList,
    ResourceStore<MeshAssetResource, MeshTag>& meshStore,
    ResourceStore<MaterialResource, MaterialTag>& matStore,
    ResourceStore<GDXShaderResource, ShaderTag>& shaderStore,
    ResourceStore<GDXTextureResource, TextureTag>& texStore,
    ResourceStore<GDXRenderTargetResource, RenderTargetTag>* rtStore)
{
    if (!m_ctx)
        return nullptr;

    if (passDesc.kind == BackendRenderPassDesc::Kind::Shadow)
    {
        if (!m_shadowMap.IsReady())
        {
            m_hasShadowPass = false;
            return nullptr;
        }

        if (commandList.Empty())
        {
            m_hasShadowPass = false;
            return nullptr;
        }

        const FrameData* frame = passDesc.frame;
        if (!frame)
            return nullptr;

        // CSM: pro Kaskade FrameConstants mit der Kaskaden-Matrix überschreiben,
        // dann Shadow-Queue rendern. FrameConstants werden danach wiederhergestellt.
        const uint32_t numCascades = (frame->shadowCascadeCount > 0u)
            ? frame->shadowCascadeCount : 1u;

        for (uint32_t cascade = 0u; cascade < numCascades; ++cascade)
        {
            FrameData cascadeFrame = *frame;
            if (frame->shadowCascadeCount > 0u)
                cascadeFrame.shadowViewProjMatrix = frame->shadowCascadeViewProj[cascade];

            m_executor.UpdateFrameConstants(cascadeFrame);
            m_executor.UpdateCascadeConstants(*frame);

            m_hasShadowPass = true;
            m_shadowMap.BeginPass(m_ctx, cascade);
            m_executor.ExecuteShadowQueue(registry, commandList, meshStore, matStore, shaderStore, texStore);
            m_shadowMap.EndPass(m_ctx);
        }

        // FrameConstants auf Original-Werte zurücksetzen
        m_executor.UpdateFrameConstants(*frame);
        m_executor.UpdateCascadeConstants(*frame);

        if (m_context)
        {
            auto* rtv = static_cast<ID3D11RenderTargetView*>(m_context->GetRenderTarget());
            auto* dsv = static_cast<ID3D11DepthStencilView*>(m_context->GetDepthStencil());
            m_ctx->OMSetRenderTargets(1, &rtv, dsv);
        }

        D3D11_VIEWPORT vp = {};
        vp.Width = frame->viewportWidth;
        vp.Height = frame->viewportHeight;
        vp.MaxDepth = 1.0f;
        m_ctx->RSSetViewports(1, &vp);
        m_ctx->RSSetState(m_rasterizerState);
        m_samplerCache.BindAll(m_ctx);
        return nullptr;
    }

    // --- Graphics pass ---
    // The planning layer pre-splits opaque and alpha queues.
    // Use passDesc queues when available; fall back to commandList for legacy callers.
    const ICommandList* opaqueList = passDesc.opaqueList ? passDesc.opaqueList : &commandList;
    const ICommandList* alphaList  = passDesc.alphaList;

    // Upload light cbuffer now — this is the first point where GPU resources
    // are available and the FrameData is fully frozen for this view.
    if (passDesc.frame)
        UploadLightConstants(*passDesc.frame);

    auto executeGraphics = [&](ID3D11RenderTargetView* rtv,
        ID3D11DepthStencilView* dsv,
        float viewportWidth,
        float viewportHeight) -> void*
        {
            m_ctx->OMSetRenderTargets(1, &rtv, dsv);

            D3D11_VIEWPORT vp = {};
            vp.Width = viewportWidth;
            vp.Height = viewportHeight;
            vp.MinDepth = 0.0f;
            vp.MaxDepth = 1.0f;
            m_ctx->RSSetViewports(1, &vp);

            // Pass the pre-split queues directly — no re-split inside ExecuteMainPassCommon.
            // alphaList may be nullptr if there are no transparent draws this frame.
            static const RenderQueue kEmptyQueue{};
            const ICommandList& opaqueRef = *opaqueList;
            const ICommandList& alphaRef  = alphaList ? *alphaList : static_cast<const ICommandList&>(kEmptyQueue);

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
                m_iblValid, m_iblIrradiance, m_iblPrefiltered, m_iblBrdfLut,
                registry,
                opaqueRef,
                alphaRef,
                meshStore,
                matStore,
                shaderStore,
                texStore);

            return nullptr;
        };

    if (passDesc.target.useBackbuffer || !passDesc.target.renderTarget.IsValid() || !rtStore)
    {
        if (!m_context)
            return nullptr;

        auto* rtv = static_cast<ID3D11RenderTargetView*>(m_context->GetRenderTarget());
        auto* dsv = static_cast<ID3D11DepthStencilView*>(m_context->GetDepthStencil());
        return executeGraphics(rtv, dsv, passDesc.target.viewportWidth, passDesc.target.viewportHeight);
    }

    GDXRenderTargetResource* rt = rtStore->Get(passDesc.target.renderTarget);
    if (!rt || !rt->ready || !rt->rtv || !rt->dsv)
        return nullptr;

    auto* rtv = static_cast<ID3D11RenderTargetView*>(rt->rtv);
    auto* dsv = static_cast<ID3D11DepthStencilView*>(rt->dsv);

    ID3D11ShaderResourceView* nullSrvs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
    m_ctx->PSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, nullSrvs);
    m_ctx->OMSetRenderTargets(1, &rtv, dsv);

    if (rt->exposedTexture.IsValid())
        m_executor.TransitionTexture(rt->exposedTexture, ResourceState::ShaderRead, ResourceState::RenderTarget, "ExecuteRenderPass offscreen begin");

    const RenderPassClearDesc& clearDesc = passDesc.target.clear;
    if (clearDesc.clearColorEnabled)
        m_ctx->ClearRenderTargetView(rtv, clearDesc.clearColor);

    UINT depthStencilFlags = 0u;
    if (clearDesc.clearDepthEnabled) depthStencilFlags |= D3D11_CLEAR_DEPTH;
    if (clearDesc.clearStencilEnabled) depthStencilFlags |= D3D11_CLEAR_STENCIL;
    if (depthStencilFlags != 0u)
        m_ctx->ClearDepthStencilView(dsv, depthStencilFlags, clearDesc.clearDepthValue, clearDesc.clearStencilValue);

    executeGraphics(rtv, dsv, static_cast<float>(rt->width), static_cast<float>(rt->height));

    m_ctx->PSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, nullSrvs);

    if (rt->exposedTexture.IsValid())
        m_executor.TransitionTexture(rt->exposedTexture, ResourceState::RenderTarget, ResourceState::ShaderRead, "ExecuteRenderPass offscreen end");

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

// ---------------------------------------------------------------------------
// CreateCubemapSRV — DX11-Upload fuer Cubemap-Daten aus GDXIBLData
// ---------------------------------------------------------------------------
static ID3D11ShaderResourceView* IBL_CreateCubemapSRV(
    ID3D11Device* device, const float* data,
    uint32_t faceSize, uint32_t mipLevels)
{
    if (!device || !data || faceSize == 0) return nullptr;

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = td.Height = faceSize;
    td.MipLevels = mipLevels;
    td.ArraySize = 6;
    td.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    td.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

    // Subresource-Daten: face * mipLevels Eintraege
    std::vector<D3D11_SUBRESOURCE_DATA> srd(6u * mipLevels);
    uint32_t mipOffset = 0;
    for (uint32_t mip = 0; mip < mipLevels; ++mip)
    {
        uint32_t ms = faceSize >> mip;
        if (ms < 1) ms = 1;
        for (uint32_t face = 0; face < 6; ++face)
        {
            uint32_t faceOffset = mipOffset + face * ms * ms * 4u;
            auto& s = srd[face * mipLevels + mip];
            s.pSysMem = data + faceOffset;
            s.SysMemPitch = ms * 4u * sizeof(float);
            s.SysMemSlicePitch = ms * ms * 4u * sizeof(float);
        }
        mipOffset += 6u * ms * ms * 4u;
    }

    ID3D11Texture2D* tex = nullptr;
    if (FAILED(device->CreateTexture2D(&td, srd.data(), &tex)) || !tex)
        return nullptr;

    D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
    sd.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
    sd.TextureCube.MostDetailedMip = 0;
    sd.TextureCube.MipLevels = mipLevels;

    ID3D11ShaderResourceView* srv = nullptr;
    device->CreateShaderResourceView(tex, &sd, &srv);
    tex->Release();
    return srv;
}

static ID3D11ShaderResourceView* IBL_CreateLutSRV(
    ID3D11Device* device, const float* data, uint32_t size)
{
    if (!device || !data || size == 0) return nullptr;

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = td.Height = size;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R32G32_FLOAT;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA init = {};
    init.pSysMem = data;
    init.SysMemPitch = size * 2u * sizeof(float);

    ID3D11Texture2D* tex = nullptr;
    if (FAILED(device->CreateTexture2D(&td, &init, &tex)) || !tex)
        return nullptr;

    D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
    sd.Format = DXGI_FORMAT_R32G32_FLOAT;
    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sd.Texture2D.MostDetailedMip = 0;
    sd.Texture2D.MipLevels = 1;

    ID3D11ShaderResourceView* srv = nullptr;
    device->CreateShaderResourceView(tex, &sd, &srv);
    tex->Release();
    return srv;
}

void GDXDX11RenderBackend::LoadIBL(const wchar_t* hdrPath)
{
    if (!m_device || !m_ctx) return;

    // Alte SRVs freigeben
    if (m_iblIrradiance) { m_iblIrradiance->Release();  m_iblIrradiance = nullptr; }
    if (m_iblPrefiltered) { m_iblPrefiltered->Release(); m_iblPrefiltered = nullptr; }
    if (m_iblBrdfLut) { m_iblBrdfLut->Release();     m_iblBrdfLut = nullptr; }
    m_iblValid = false;

    // CPU-Baking (backend-agnostisch)
    GDXIBLData data = (hdrPath && hdrPath[0] != L'\0')
        ? GDXIBLBaker::Bake(hdrPath)
        : GDXIBLBaker::MakeFallback();

    if (!data.valid) return;

    // DX11-Upload
    m_iblIrradiance = IBL_CreateCubemapSRV(m_device,
        data.irradiance.data(), data.irrSize, 1u);
    m_iblPrefiltered = IBL_CreateCubemapSRV(m_device,
        data.prefiltered.data(), data.envSize, data.envMips);
    m_iblBrdfLut = IBL_CreateLutSRV(m_device,
        data.brdfLut.data(), data.lutSize);

    m_iblValid = m_iblIrradiance && m_iblPrefiltered && m_iblBrdfLut;
    if (!m_iblValid)
        Debug::LogError(GDX_SRC_LOC, L"GDXDX11RenderBackend::LoadIBL: SRV-Upload fehlgeschlagen");
}

uint32_t GDXDX11RenderBackend::GetDrawCallCount() const
{
    return m_executor.GetDrawCallCount();
}

bool GDXDX11RenderBackend::HasShadowResources() const
{
    return m_shadowMap.GetDSV() != nullptr && m_shadowMap.GetSRV() != nullptr;
}

bool GDXDX11RenderBackend::SupportsTextureFormat(GDXTextureFormat format) const
{
    if (!m_device) return false;
    UINT support = 0;
    const HRESULT hr = m_device->CheckFormatSupport(ToDxgiColorFormat(format), &support);
    if (FAILED(hr)) return false;
    return (support & D3D11_FORMAT_SUPPORT_RENDER_TARGET) != 0 &&
        (support & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE) != 0;
}

const IGDXRenderBackend::DefaultTextureSet& GDXDX11RenderBackend::GetDefaultTextures() const
{
    return m_defaultTextures;
}

void GDXDX11RenderBackend::ReleasePostProcessSurface(Dx11PostProcessSurface& surface)
{
    if (surface.srv) { static_cast<ID3D11ShaderResourceView*>(surface.srv)->Release(); surface.srv = nullptr; }
    if (surface.rtv) { static_cast<ID3D11RenderTargetView*>(surface.rtv)->Release(); surface.rtv = nullptr; }
    if (surface.texture) { static_cast<ID3D11Texture2D*>(surface.texture)->Release(); surface.texture = nullptr; }
    surface.width = 0u;
    surface.height = 0u;
    surface.format = GDXTextureFormat::Unknown;
}

bool GDXDX11RenderBackend::EnsurePostProcessSurface(Dx11PostProcessSurface& surface, uint32_t width, uint32_t height, GDXTextureFormat format, const wchar_t* debugName)
{
    if (!m_device || width == 0u || height == 0u) return false;
    if (surface.texture && surface.width == width && surface.height == height && surface.format == format)
        return true;

    ReleasePostProcessSurface(surface);

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = ToDxgiColorFormat(format);
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    ID3D11Texture2D* texture = nullptr;
    if (FAILED(m_device->CreateTexture2D(&texDesc, nullptr, &texture)))
        return false;

    ID3D11RenderTargetView* rtv = nullptr;
    if (FAILED(m_device->CreateRenderTargetView(texture, nullptr, &rtv)))
    {
        texture->Release();
        return false;
    }

    ID3D11ShaderResourceView* srv = nullptr;
    if (FAILED(m_device->CreateShaderResourceView(texture, nullptr, &srv)))
    {
        rtv->Release();
        texture->Release();
        return false;
    }

    surface.texture = texture;
    surface.rtv = rtv;
    surface.srv = srv;
    surface.width = width;
    surface.height = height;
    (void)debugName;
    return true;
}

PostProcessHandle GDXDX11RenderBackend::CreatePostProcessPass(ResourceStore<PostProcessResource, PostProcessTag>& postStore,
    const PostProcessPassDesc& desc)
{
    if (!m_device) return PostProcessHandle::Invalid();

    const auto vsPath = FindShaderPath(desc.vertexShaderFile);
    const auto psPath = FindShaderPath(desc.pixelShaderFile);
    if (vsPath.empty() || psPath.empty())
    {
        Debug::LogError("PostProcess shader not found: ", desc.debugName.empty() ? desc.pixelShaderFile : desc.debugName);
        return PostProcessHandle::Invalid();
    }

    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* psBlob = nullptr;
    if (!CompileFromFile(vsPath.wstring(), "main", "vs_5_0", &vsBlob)) return PostProcessHandle::Invalid();
    if (!CompileFromFile(psPath.wstring(), "main", "ps_5_0", &psBlob)) { vsBlob->Release(); return PostProcessHandle::Invalid(); }

    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    const HRESULT vsHr = m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vs);
    const HRESULT psHr = m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &ps);
    vsBlob->Release();
    psBlob->Release();
    if (FAILED(vsHr) || FAILED(psHr))
    {
        if (vs) vs->Release();
        if (ps) ps->Release();
        return PostProcessHandle::Invalid();
    }

    PostProcessResource pass{};
    pass.desc = desc;
    pass.enabled = desc.enabled;
    pass.constantBufferBytes = desc.constantBufferBytes;

    Dx11PostProcessRuntime runtime{};
    runtime.vertexShader = vs;
    runtime.pixelShader = ps;

    if (desc.constantBufferBytes > 0u)
    {
        const uint32_t alignedBytes = (desc.constantBufferBytes + 15u) & ~15u;
        pass.constantData.resize(alignedBytes, 0u);

        D3D11_BUFFER_DESC cbDesc = {};
        cbDesc.ByteWidth = alignedBytes;
        cbDesc.Usage = D3D11_USAGE_DYNAMIC;
        cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        ID3D11Buffer* cb = nullptr;
        if (FAILED(m_device->CreateBuffer(&cbDesc, nullptr, &cb)))
        {
            vs->Release();
            ps->Release();
            return PostProcessHandle::Invalid();
        }
        runtime.constantBuffer = cb;
        pass.cpuDirty = true;
    }
    pass.ready = true;
    PostProcessHandle handle = postStore.Add(std::move(pass));
    if (!handle.IsValid())
    {
        if (runtime.constantBuffer) runtime.constantBuffer->Release();
        if (runtime.pixelShader) runtime.pixelShader->Release();
        if (runtime.vertexShader) runtime.vertexShader->Release();
        return PostProcessHandle::Invalid();
    }
    m_postProcessRuntime.emplace(handle, runtime);
    return handle;
}

bool GDXDX11RenderBackend::UpdatePostProcessConstants(PostProcessResource& pass, const void* data, uint32_t size)
{
    if (!pass.ready || pass.constantBufferBytes == 0u)
        return false;
    if (!data || size > pass.constantData.size())
        return false;

    std::memcpy(pass.constantData.data(), data, size);
    if (size < pass.constantData.size())
        std::memset(pass.constantData.data() + size, 0, pass.constantData.size() - size);
    pass.cpuDirty = true;
    return true;
}

void GDXDX11RenderBackend::DestroyPostProcessPasses(ResourceStore<PostProcessResource, PostProcessTag>& postStore)
{
    // Phase 1: Handles sammeln — kein Modify während ForEach.
    std::vector<PostProcessHandle> handles;
    postStore.ForEach([&handles](PostProcessHandle handle, PostProcessResource&)
        {
            handles.push_back(handle);
        });

    // Phase 2: GPU-Ressourcen freigeben, Store-Einträge releasen.
    for (const PostProcessHandle handle : handles)
    {
        auto it = m_postProcessRuntime.find(handle);
        if (it != m_postProcessRuntime.end())
        {
            if (it->second.constantBuffer) { it->second.constantBuffer->Release(); it->second.constantBuffer = nullptr; }
            if (it->second.pixelShader) { it->second.pixelShader->Release();    it->second.pixelShader = nullptr; }
            if (it->second.vertexShader) { it->second.vertexShader->Release();   it->second.vertexShader = nullptr; }
            m_postProcessRuntime.erase(it);
        }
        // Store-Eintrag freigeben — kein ready=false-Zombie bleibt zurück.
        postStore.Release(handle);
    }
}

bool GDXDX11RenderBackend::ExecutePostProcessChain(const std::vector<PostProcessHandle>& orderedPasses,
    ResourceStore<PostProcessResource, PostProcessTag>& postStore,
    ResourceStore<GDXTextureResource, TextureTag>& texStore,
    TextureHandle sceneTexture,
    float viewportWidth,
    float viewportHeight)
{
    if (!m_ctx || !m_context || !sceneTexture.IsValid()) return false;

    std::vector<PostProcessHandle> active;
    active.reserve(orderedPasses.size());
    for (const PostProcessHandle handle : orderedPasses)
    {
        const PostProcessResource* pass = postStore.Get(handle);
        if (pass && pass->ready && pass->enabled)
            active.push_back(handle);
    }
    if (active.empty()) return false;

    const auto* sceneRes = texStore.Get(sceneTexture);
    if (!sceneRes || !sceneRes->srv) return false;

    const uint32_t width = (std::max)(1u, static_cast<uint32_t>(viewportWidth));
    const uint32_t height = (std::max)(1u, static_cast<uint32_t>(viewportHeight));
    if (active.size() > 1u)
    {
        const GDXTextureFormat intermediateFormat = MakeLinearPostProcessFormat(sceneRes->format);
        if (!EnsurePostProcessSurface(m_postProcessPing, width, height, intermediateFormat, L"PostProcessPing")) return false;
        if (!EnsurePostProcessSurface(m_postProcessPong, width, height, intermediateFormat, L"PostProcessPong")) return false;
    }

    auto* backbufferRTV = static_cast<ID3D11RenderTargetView*>(m_context->GetRenderTarget());
    ID3D11ShaderResourceView* inputSrv = static_cast<ID3D11ShaderResourceView*>(sceneRes->srv);
    ID3D11ShaderResourceView* originalSceneSrv = static_cast<ID3D11ShaderResourceView*>(sceneRes->srv);
    ID3D11RenderTargetView* tempTargets[2] = {
        static_cast<ID3D11RenderTargetView*>(m_postProcessPing.rtv),
        static_cast<ID3D11RenderTargetView*>(m_postProcessPong.rtv)
    };
    const float clearColor[4] = { 0,0,0,0 };

    m_ctx->IASetInputLayout(nullptr);
    m_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_ctx->OMSetDepthStencilState(nullptr, 0u);
    const float blendFactor[4] = { 0,0,0,0 };
    m_ctx->OMSetBlendState(nullptr, blendFactor, 0xFFFFFFFF);
    m_ctx->RSSetState(m_rasterizerStateNoCull);
    m_samplerCache.BindAll(m_ctx);

    D3D11_VIEWPORT vp = {};
    vp.Width = static_cast<float>(width);
    vp.Height = static_cast<float>(height);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    m_ctx->RSSetViewports(1, &vp);

    for (size_t i = 0; i < active.size(); ++i)
    {
        PostProcessResource* pass = postStore.Get(active[i]);
        if (!pass) continue;
        auto runtimeIt = m_postProcessRuntime.find(active[i]);
        if (runtimeIt == m_postProcessRuntime.end()) continue;
        Dx11PostProcessRuntime& runtime = runtimeIt->second;

        if (runtime.constantBuffer && pass->cpuDirty)
        {
            D3D11_MAPPED_SUBRESOURCE mapped = {};
            if (SUCCEEDED(m_ctx->Map(runtime.constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
            {
                std::memcpy(mapped.pData, pass->constantData.data(), pass->constantData.size());
                m_ctx->Unmap(runtime.constantBuffer, 0);
                pass->cpuDirty = false;
            }
        }

        const bool isLast = (i + 1u) == active.size();
        ID3D11RenderTargetView* outputRtv = isLast ? backbufferRTV : tempTargets[i % 2u];
        m_ctx->OMSetRenderTargets(1, &outputRtv, nullptr);
        m_ctx->ClearRenderTargetView(outputRtv, clearColor);

        m_ctx->VSSetShader(runtime.vertexShader, nullptr, 0);
        m_ctx->PSSetShader(runtime.pixelShader, nullptr, 0);
        ID3D11ShaderResourceView* srvs[2] = { inputSrv, originalSceneSrv };
        m_ctx->PSSetShaderResources(0, 2, srvs);

        if (runtime.constantBuffer)
        {
            ID3D11Buffer* cb = runtime.constantBuffer;
            m_ctx->VSSetConstantBuffers(0, 1, &cb);
            m_ctx->PSSetConstantBuffers(0, 1, &cb);
        }
        else
        {
            ID3D11Buffer* nullCb = nullptr;
            m_ctx->VSSetConstantBuffers(0, 1, &nullCb);
            m_ctx->PSSetConstantBuffers(0, 1, &nullCb);
        }

        m_ctx->Draw(3, 0);

        ID3D11ShaderResourceView* nullSrvs[2] = { nullptr, nullptr };
        m_ctx->PSSetShaderResources(0, 2, nullSrvs);

        if (!isLast)
            inputSrv = (i % 2u == 0u) ? static_cast<ID3D11ShaderResourceView*>(m_postProcessPing.srv)
            : static_cast<ID3D11ShaderResourceView*>(m_postProcessPong.srv);
    }

    ID3D11ShaderResourceView* nullSrvs[2] = { nullptr, nullptr };
    ID3D11Buffer* nullCb = nullptr;
    m_ctx->PSSetShaderResources(0, 2, nullSrvs);
    m_ctx->VSSetConstantBuffers(0, 1, &nullCb);
    m_ctx->PSSetConstantBuffers(0, 1, &nullCb);
    m_ctx->VSSetShader(nullptr, nullptr, 0);
    m_ctx->PSSetShader(nullptr, nullptr, 0);
    return true;
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
            res.format = GDXTextureFormat::RGBA8_UNORM;
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
    // Defensiver Runtime-Sweep: gibt alle PostProcess-GPU-Ressourcen frei, die
    // DestroyPostProcessPasses() noch nicht erfasst hat (z.B. bei vorzeitigem Shutdown).
    for (auto& [handle, runtime] : m_postProcessRuntime)
    {
        if (runtime.constantBuffer) { runtime.constantBuffer->Release(); runtime.constantBuffer = nullptr; }
        if (runtime.pixelShader) { runtime.pixelShader->Release();    runtime.pixelShader = nullptr; }
        if (runtime.vertexShader) { runtime.vertexShader->Release();   runtime.vertexShader = nullptr; }
    }
    m_postProcessRuntime.clear();

    if (m_iblIrradiance) { m_iblIrradiance->Release();  m_iblIrradiance = nullptr; }
    if (m_iblPrefiltered) { m_iblPrefiltered->Release(); m_iblPrefiltered = nullptr; }
    if (m_iblBrdfLut) { m_iblBrdfLut->Release();     m_iblBrdfLut = nullptr; }
    m_executor.Shutdown();
    m_lightSystem.Shutdown();

    {
        std::vector<MaterialHandle> matHandles;
        matStore.ForEach([&matHandles](MaterialHandle h, MaterialResource& mat)
            {
                if (mat.gpuConstantBuffer)
                {
                    static_cast<ID3D11Buffer*>(mat.gpuConstantBuffer)->Release();
                    mat.gpuConstantBuffer = nullptr;
                }
                matHandles.push_back(h);
            });
        for (const MaterialHandle h : matHandles)
            matStore.Release(h);
    }

    {
        std::vector<ShaderHandle> shaderHandles;
        shaderStore.ForEach([&shaderHandles](ShaderHandle h, GDXShaderResource& sh)
            {
                if (sh.vertexShader) { static_cast<ID3D11VertexShader*>(sh.vertexShader)->Release(); sh.vertexShader = nullptr; }
                if (sh.pixelShader) { static_cast<ID3D11PixelShader*>(sh.pixelShader)->Release();  sh.pixelShader = nullptr; }
                if (sh.inputLayout) { static_cast<ID3D11InputLayout*>(sh.inputLayout)->Release();  sh.inputLayout = nullptr; }
                shaderHandles.push_back(h);
            });
        for (const ShaderHandle h : shaderHandles)
            shaderStore.Release(h);
    }

    {
        // RT-Texturen sind durch DestroyRenderTarget() bereits aus texStore entfernt.
        // Hier verbleiben nur reguläre Texturen (von Datei/ImageBuffer geladen) —
        // jede besitzt ihren SRV vollständig.
        std::vector<TextureHandle> texHandles;
        texStore.ForEach([&texHandles](TextureHandle h, GDXTextureResource& tex)
            {
                if (tex.srv)
                {
                    static_cast<ID3D11ShaderResourceView*>(tex.srv)->Release();
                    tex.srv = nullptr;
                }
                texHandles.push_back(h);
            });
        for (const TextureHandle h : texHandles)
            texStore.Release(h);
    }

    if (m_blendStateAlpha) { m_blendStateAlpha->Release(); m_blendStateAlpha = nullptr; }
    if (m_blendState) { m_blendState->Release(); m_blendState = nullptr; }
    if (m_depthStateNoWrite) { m_depthStateNoWrite->Release(); m_depthStateNoWrite = nullptr; }
    if (m_depthStencilState) { m_depthStencilState->Release(); m_depthStencilState = nullptr; }
    if (m_rasterizerState) { m_rasterizerState->Release();       m_rasterizerState = nullptr; }
    if (m_rasterizerStateNoCull) { m_rasterizerStateNoCull->Release(); m_rasterizerStateNoCull = nullptr; }

    ReleasePostProcessSurface(m_postProcessPing);
    ReleasePostProcessSurface(m_postProcessPong);
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
    const std::wstring& debugName,
    GDXTextureFormat colorFormat)
{
    if (!m_device || !m_ctx) return RenderTargetHandle::Invalid();

    HRESULT hr = S_OK;

    // --- Farb-Textur ---
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = ToDxgiColorFormat(colorFormat);
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
    texRes.isSRGB = (colorFormat == GDXTextureFormat::RGBA8_UNORM_SRGB);
    texRes.format = colorFormat;
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
    rt.colorFormat = colorFormat;
    rt.exposedTexture = exposedTex;
    rt.debugName = debugName;

    return rtStore.Add(std::move(rt));
}

// ---------------------------------------------------------------------------
// DestroyRenderTarget — gibt alle nativen DX11-Ressourcen frei.
// Entfernt außerdem die exposedTexture aus texStore (SRV + Eintrag).
// Sicher bei Invalid-Handle.
// ---------------------------------------------------------------------------
void GDXDX11RenderBackend::DestroyRenderTarget(
    RenderTargetHandle handle,
    ResourceStore<GDXRenderTargetResource, RenderTargetTag>& rtStore,
    ResourceStore<GDXTextureResource, TextureTag>& texStore)
{
    if (!handle.IsValid()) return;

    GDXRenderTargetResource* rt = rtStore.Get(handle);
    if (!rt) return;
    if (!rt->ready) return; // bereits zerstört — doppelter Aufruf ist ein Programmfehler

    // exposedTexture: SRV-Zeiger in rt->srv und texRes->srv zeigen auf dasselbe
    // COM-Objekt. Wir releasen genau einmal hier, nulcen beide Seiten, dann
    // releasen den texStore-Slot. Shutdown().texStore.ForEach() sieht den Slot
    // nicht mehr (alive=false) — kein Double-Release.
    if (rt->exposedTexture.IsValid())
    {
        GDXTextureResource* texRes = texStore.Get(rt->exposedTexture);
        if (texRes && texRes->srv)
        {
            static_cast<ID3D11ShaderResourceView*>(texRes->srv)->Release();
            texRes->srv = nullptr;
        }
        texStore.Release(rt->exposedTexture);
        rt->exposedTexture = TextureHandle::Invalid();
    }
    rt->srv = nullptr; // COM-Release oben via texStore-Eintrag erfolgt

    if (rt->dsv) { static_cast<ID3D11DepthStencilView*>(rt->dsv)->Release();   rt->dsv = nullptr; }
    if (rt->depthTexture) { static_cast<ID3D11Texture2D*>(rt->depthTexture)->Release(); rt->depthTexture = nullptr; }
    if (rt->rtv) { static_cast<ID3D11RenderTargetView*>(rt->rtv)->Release();   rt->rtv = nullptr; }
    if (rt->colorTexture) { static_cast<ID3D11Texture2D*>(rt->colorTexture)->Release(); rt->colorTexture = nullptr; }

    rt->ready = false;
    rtStore.Release(handle);
}
