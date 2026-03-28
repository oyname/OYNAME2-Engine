#include "GDXDX11RenderBackend.h"
#include "GDXDX11GpuResources.h"
#include "GDXIBLBaker.h"
#include "GDXRenderTargetResource.h"
#include "GDXVertexFlags.h"
#include "GDXDX11ShaderCompiler.h"
#include "Core/Debug.h"
#include "GDXResourceState.h"
#include "RenderQueue.h"
#include "PostProcessBindingResolver.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

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


namespace
{
    void LogDx11MainPassState(ID3D11DeviceContext* ctx,
                              ID3D11RenderTargetView* expectedRTV,
                              ID3D11DepthStencilView* expectedDSV,
                              ID3D11RasterizerState* expectedRS,
                              ID3D11DepthStencilState* expectedDS,
                              ID3D11BlendState* expectedBlend,
                              const char* stageTag)
    {
        if (!ctx || !stageTag)
            return;

        ID3D11BlendState* blend = nullptr;
        FLOAT blendFactor[4] = {};
        UINT sampleMask = 0u;
        ctx->OMGetBlendState(&blend, blendFactor, &sampleMask);

        ID3D11DepthStencilState* depth = nullptr;
        UINT stencilRef = 0u;
        ctx->OMGetDepthStencilState(&depth, &stencilRef);

        ID3D11RasterizerState* raster = nullptr;
        ctx->RSGetState(&raster);

        ID3D11RenderTargetView* boundRTV = nullptr;
        ID3D11DepthStencilView* boundDSV = nullptr;
        ctx->OMGetRenderTargets(1, &boundRTV, &boundDSV);

        UINT scissorCount = 0u;
        ctx->RSGetScissorRects(&scissorCount, nullptr);

        //static uint32_t s_logs = 0u;
        //if (s_logs < 16u)
        //{
        //    Debug::Log(GDX_SRC_LOC,
        //               "DX11 main-state ", stageTag,
        //               " rtvMatch=", (boundRTV == expectedRTV) ? 1u : 0u,
        //               " dsvMatch=", (boundDSV == expectedDSV) ? 1u : 0u,
        //               " rsMatch=", (raster == expectedRS) ? 1u : 0u,
        //               " dsMatch=", (depth == expectedDS) ? 1u : 0u,
        //               " blendMatch=", (blend == expectedBlend) ? 1u : 0u,
        //               " scissorRectCount=", scissorCount,
        //               " sampleMask=", sampleMask,
        //               " blendPtr=", static_cast<const void*>(blend),
        //               " rtvPtr=", static_cast<const void*>(boundRTV),
        //               " dsvPtr=", static_cast<const void*>(boundDSV));
        //    ++s_logs;
        //}

        if (boundRTV) boundRTV->Release();
        if (boundDSV) boundDSV->Release();
        if (raster) raster->Release();
        if (depth) depth->Release();
        if (blend) blend->Release();
    }
}

bool GDXTextureLoader_LoadFromFile(ID3D11Device*, ID3D11DeviceContext*,
    const wchar_t*, DX11TextureGpu&, GDXTextureResource&, bool isSRGB);
bool GDXTextureLoader_Create1x1(ID3D11Device*, uint8_t, uint8_t, uint8_t, uint8_t, DX11TextureGpu&, GDXTextureResource&);
bool GDXTextureLoader_CreateFromImage(ID3D11Device*, ID3D11DeviceContext*,
    const ImageBuffer&, DX11TextureGpu&, GDXTextureResource&, bool isSRGB, const wchar_t* debugName);

struct DebugSmokeVsConstants
{
    Matrix4 mvp;
};

struct DebugSmokeVertexPos
{
    float x, y, z;
};

struct DebugSmokeVertexPosColor
{
    float x, y, z;
    float r, g, b, a;
};

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
        GDXDX11TileLightCuller& tileCuller,
        GDXDX11GpuRegistry& gpuRegistry,
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
        ID3D11ShaderResourceView* shadowSrv = (hasShadowPass && shadowMap.IsReady()) ? static_cast<ID3D11ShaderResourceView*>(shadowMap.GetSRV()) : nullptr;

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

        // Forward+: bind tile light SRVs (t20/t21/t22) + tile info cbuffer (b3)
        if (tileCuller.IsReady())
            tileCuller.BindForPS(ctx);

        if (!opaqueQueue.Empty())
            executor.ExecuteQueue(registry, opaqueQueue, meshStore, matStore, shaderStore, texStore, gpuRegistry, shadowSrv);

        ctx->OMSetDepthStencilState(depthStateNoWrite, 0u);
        ctx->OMSetBlendState(blendStateAlpha, bf, 0xFFFFFFFF);
        if (!transparentQueue.Empty())
            executor.ExecuteQueue(registry, transparentQueue, meshStore, matStore, shaderStore, texStore, gpuRegistry, shadowSrv);

        ctx->OMSetDepthStencilState(depthStencilState, 0u);
        ctx->OMSetBlendState(blendState, bf, 0xFFFFFFFF);

        if (tileCuller.IsReady())
            tileCuller.UnbindFromPS(ctx);
    }


    template <typename TVertex>
    ID3D11Buffer* CreateStaticVertexBuffer(ID3D11Device* device, const TVertex* data, UINT byteWidth)
    {
        if (!device || !data || byteWidth == 0)
            return nullptr;

        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = byteWidth;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

        D3D11_SUBRESOURCE_DATA init = {};
        init.pSysMem = data;

        ID3D11Buffer* buffer = nullptr;
        if (FAILED(device->CreateBuffer(&desc, &init, &buffer)))
            return nullptr;

        return buffer;
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

    ID3DBlob* tileCsBlob = nullptr;
    if (GDXDX11CompileShaderFromFile(L"TileLightCullCS.hlsl", "main", "cs_5_0", &tileCsBlob))
    {
        if (SUCCEEDED(m_device->CreateComputeShader(
                tileCsBlob->GetBufferPointer(),
                tileCsBlob->GetBufferSize(),
                nullptr,
                &m_tileLightCullCS)))
        {
            if (!m_tileCuller.Init(m_device, m_tileLightCullCS))
                Debug::LogWarning(GDX_SRC_LOC, L"GDXDX11TileLightCuller: Init fehlgeschlagen — Forward+ deaktiviert");
        }
        else
        {
            Debug::LogWarning(GDX_SRC_LOC, L"GDXDX11TileLightCuller: Compute-Shader-Erzeugung fehlgeschlagen — Forward+ deaktiviert");
        }
        tileCsBlob->Release();
    }
    else
    {
        Debug::LogWarning(GDX_SRC_LOC, L"GDXDX11TileLightCuller: Compute-Shader-Kompilierung fehlgeschlagen — Forward+ deaktiviert");
    }

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
    m_executor.SetRasterizerStates(m_rasterizerState, m_rasterizerStateNoCull, m_depthStencilState, m_depthStateNoWrite, m_depthStateNoTest, m_blendState, m_blendStateAlpha);
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
    ReleaseAllPostProcessSurfacePairs();
    if (m_context) m_context->Resize(w, h);
}

ShaderHandle GDXDX11RenderBackend::UploadShader(
    ResourceStore<GDXShaderResource, ShaderTag>& shaderStore,
    const ShaderSourceDesc& desc)
{
    if (!m_device) return ShaderHandle::Invalid();
    if (desc.sourceType != ShaderSourceType::HlslFilePath)
    {
        Debug::LogWarning(GDX_SRC_LOC, L"DX11 UploadShader: aktuell nur HlslFilePath unterstuetzt");
        return ShaderHandle::Invalid();
    }

    const std::wstring vsFile = desc.VertexFilePath();
    const std::wstring psFile = desc.PixelFilePath();
    const uint32_t vertexFlags = desc.vertexFlags;
    const GDXShaderLayout& shaderLayout = desc.layout;
    const std::wstring debugName = desc.debugName.empty() ? (vsFile + L" / " + psFile) : desc.debugName;

    if (vsFile.empty() || psFile.empty())
        return ShaderHandle::Invalid();

    ID3DBlob* vsBlob = nullptr;
    if (!GDXDX11CompileShaderFromFile(vsFile.c_str(), "main", "vs_5_0", &vsBlob, desc.defines))
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
    if (!GDXDX11CompileShaderFromFile(psFile.c_str(), "main", "ps_5_0", &psBlob, desc.defines))
    {
        vs->Release();
        if (inputLayout) inputLayout->Release();
        return ShaderHandle::Invalid();
    }

    ID3D11PixelShader* ps = nullptr;
    if (FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &ps)))
    {
        psBlob->Release();
        vs->Release();
        if (inputLayout) inputLayout->Release();
        return ShaderHandle::Invalid();
    }
    psBlob->Release();

    GDXShaderResource res;
    res.vertexFlags = vertexFlags;
    res.layout      = shaderLayout;
    res.ready       = true;
    res.debugName   = debugName;
    const ShaderHandle handle = shaderStore.Add(std::move(res));
    if (handle.IsValid())
    {
        DX11ShaderGpu gpu{};
        gpu.vertexShader = vs;
        gpu.pixelShader = ps;
        gpu.inputLayout = inputLayout;
        m_gpuRegistry.SetShader(handle, gpu);
        //static uint64_t s_shaderUploadCount = 0;
        //Debug::Log(GDX_SRC_LOC,
        //           "Shader upload #", ++s_shaderUploadCount,
        //           " handle=", handle.value,
        //           " name=", debugName.c_str(),
        //           " shaderAlive=", shaderStore.AliveCount(),
        //           " pipelineCache=", m_executor.DebugPipelineCacheSize(),
        //           " layoutCache=", m_executor.DebugLayoutCacheSize());
        return handle;
    }

    if (ps) ps->Release();
    if (inputLayout) inputLayout->Release();
    if (vs) vs->Release();
    return ShaderHandle::Invalid();
}

TextureHandle GDXDX11RenderBackend::UploadTexture(
    ResourceStore<GDXTextureResource, TextureTag>& texStore,
    const std::wstring& filePath,
    bool isSRGB,
    TextureHandle fallbackOnFailure)
{
    GDXTextureResource res;
    DX11TextureGpu gpu{};
    if (!GDXTextureLoader_LoadFromFile(m_device, m_ctx, filePath.c_str(), gpu, res, isSRGB))
    {
        Debug::LogError(GDX_SRC_LOC, L"LoadTexture fehlgeschlagen: ", filePath.c_str());
        return fallbackOnFailure;
    }
    const TextureHandle handle = texStore.Add(std::move(res));
    if (handle.IsValid()) m_gpuRegistry.SetTexture(handle, gpu);
    return handle;
}

TextureHandle GDXDX11RenderBackend::UploadTextureFromImage(
    ResourceStore<GDXTextureResource, TextureTag>& texStore,
    const ImageBuffer& image,
    bool isSRGB,
    const std::wstring& debugName,
    TextureHandle fallbackOnFailure)
{
    if (!m_device || !m_ctx || !image.IsValid() || !image.Data())
        return fallbackOnFailure;

    GDXTextureResource res;
    DX11TextureGpu gpu{};
    if (!GDXTextureLoader_CreateFromImage(m_device, m_ctx, image, gpu, res, isSRGB, debugName.c_str()))
        return fallbackOnFailure;

    const TextureHandle handle = texStore.Add(std::move(res));
    if (handle.IsValid()) m_gpuRegistry.SetTexture(handle, gpu);
    return handle;
}

bool GDXDX11RenderBackend::UploadMesh(MeshHandle handle, MeshAssetResource& mesh)
{
    const bool ok = m_meshUploader ? m_meshUploader->Upload(handle, mesh, m_gpuRegistry) : false;
    if (ok)
    {
        static uint64_t s_meshUploadCount = 0;
        size_t totalVertices = 0;
        size_t totalIndices = 0;
        for (const auto& sm : mesh.submeshes)
        {
            totalVertices += sm.positions.size();
            totalIndices += sm.indices.size();
        }
        //Debug::Log(GDX_SRC_LOC,
        //           "Mesh upload #", ++s_meshUploadCount,
        //           " handle=", handle.value,
        //           " submeshes=", mesh.submeshes.size(),
        //           " totalVertices=", totalVertices,
        //           " totalIndices=", totalIndices);
    }
    return ok;
}

bool GDXDX11RenderBackend::UploadMaterial(MaterialHandle handle, MaterialResource& mat)
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

    DX11MaterialGpu gpu;
    gpu.constantBuffer = buf;
    m_gpuRegistry.SetMaterial(handle, gpu);
    mat.cpuDirty = false;
    static uint64_t s_materialUploadCount = 0;
    const TextureHandle albedoTex   = mat.GetTexture(MaterialTextureSlot::Albedo);
    const TextureHandle normalTex   = mat.GetTexture(MaterialTextureSlot::Normal);
    const TextureHandle ormTex      = mat.GetTexture(MaterialTextureSlot::ORM);
    const TextureHandle emissiveTex = mat.GetTexture(MaterialTextureSlot::Emissive);

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
    // Legacy cbuffer upload (keeps shadow + ambient path working)
    m_lightSystem.UploadLightBuffer(frame, m_ctx);

    // Forward+: upload lights to StructuredBuffer, run tile cull compute
    if (m_tileCuller.IsReady())
    {
        m_tileCuller.EnsureSize(m_device,
            static_cast<uint32_t>(frame.viewportWidth),
            static_cast<uint32_t>(frame.viewportHeight));
        m_tileCuller.UploadLights(m_ctx, frame);
        m_tileCuller.Dispatch(m_ctx, frame,
            static_cast<uint32_t>(frame.viewportWidth),
            static_cast<uint32_t>(frame.viewportHeight));
        m_tileCuller.UploadPSInfoCB(m_ctx, frame);
    }
}

void GDXDX11RenderBackend::UpdateFrameConstants(const FrameData& frame)
{
    m_hasShadowPass = frame.hasShadowPass;
    m_executor.UpdateFrameConstants(frame);
    m_executor.UpdateCascadeConstants(frame);
}

void GDXDX11RenderBackend::ExecuteRenderPass(
    const BackendRenderPassDesc& passDesc,
    Registry& registry,
    const ICommandList& opaqueList,
    const ICommandList& alphaList,
    ResourceStore<MeshAssetResource,       MeshTag>&        meshStore,
    ResourceStore<MaterialResource,        MaterialTag>&     matStore,
    ResourceStore<GDXShaderResource,       ShaderTag>&       shaderStore,
    ResourceStore<GDXTextureResource,      TextureTag>&      texStore,
    ResourceStore<GDXRenderTargetResource, RenderTargetTag>& rtStore)
{
    if (!m_ctx)
        return;

    if (passDesc.frame)
        UploadLightConstants(*passDesc.frame);

    const ICommandList& opaqueRef = passDesc.opaqueList ? *passDesc.opaqueList : opaqueList;
    const ICommandList& alphaRef  = passDesc.alphaList  ? *passDesc.alphaList  : alphaList;

    auto executeGraphics = [&](ID3D11RenderTargetView* rtv,
                               ID3D11RenderTargetView* normalRtv,
                               ID3D11DepthStencilView* dsv,
                               float viewportWidth,
                               float viewportHeight)
    {
        ID3D11ShaderResourceView* nullSrvs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
        m_ctx->PSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, nullSrvs);

        if (normalRtv)
        {
            ID3D11RenderTargetView* rtvs[2] = { rtv, normalRtv };
            m_ctx->OMSetRenderTargets(2, rtvs, dsv);
        }
        else
        {
            m_ctx->OMSetRenderTargets(1, &rtv, dsv);
        }

        D3D11_VIEWPORT vp = {};
        vp.Width = viewportWidth;
        vp.Height = viewportHeight;
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        m_ctx->RSSetViewports(1, &vp);
        m_ctx->RSSetState(m_rasterizerState);
        m_ctx->OMSetDepthStencilState(m_depthStencilState, 0u);
        const float bf[4] = { 0,0,0,0 };
        m_ctx->OMSetBlendState(m_blendState, bf, 0xFFFFFFFFu);

        LogDx11MainPassState(m_ctx, rtv, dsv, m_rasterizerState, m_depthStencilState, m_blendState, "before-main");

        //static uint32_t s_mainPassBindLogs = 0u;
        //if (s_mainPassBindLogs < 8u)
        //{
        //    Debug::Log(GDX_SRC_LOC,
        //               "ExecuteRenderPass main-target diag #", (s_mainPassBindLogs + 1u),
        //               " useBackbuffer=", passDesc.target.useBackbuffer ? 1u : 0u,
        //               " rtv=", static_cast<const void*>(rtv),
        //               " dsv=", static_cast<const void*>(dsv),
        //               " viewport=", viewportWidth, "x", viewportHeight,
        //               " opaqueCount=", opaqueRef.GetCommands().size(),
        //               " alphaCount=", alphaRef.GetCommands().size());
        //    ++s_mainPassBindLogs;
        //}

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
            m_tileCuller,
            m_gpuRegistry,
            registry,
            opaqueRef,
            alphaRef,
            meshStore,
            matStore,
            shaderStore,
            texStore);

        LogDx11MainPassState(m_ctx, rtv, dsv, m_rasterizerState, m_depthStencilState, m_blendState, "after-main");
    };

    if (passDesc.target.useBackbuffer || !passDesc.target.renderTarget.IsValid())
    {
        if (!m_context)
            return;

        auto* rtv = static_cast<ID3D11RenderTargetView*>(m_context->GetRenderTarget());
        auto* dsv = static_cast<ID3D11DepthStencilView*>(m_context->GetDepthStencil());
        const RenderPassClearDesc& clearDesc = passDesc.target.clear;

        if (rtv && clearDesc.clearColorEnabled)
            m_ctx->ClearRenderTargetView(rtv, clearDesc.clearColor);

        UINT depthStencilFlags = 0u;
        if (dsv)
        {
            if (clearDesc.clearDepthEnabled)   depthStencilFlags |= D3D11_CLEAR_DEPTH;
            if (clearDesc.clearStencilEnabled) depthStencilFlags |= D3D11_CLEAR_STENCIL;
            if (depthStencilFlags != 0u)
                m_ctx->ClearDepthStencilView(dsv, depthStencilFlags, clearDesc.clearDepthValue, clearDesc.clearStencilValue);
        }

        executeGraphics(rtv, nullptr, dsv, passDesc.target.viewportWidth, passDesc.target.viewportHeight);
        return;
    }

    GDXRenderTargetResource* rt = rtStore.Get(passDesc.target.renderTarget);
    DX11RenderTargetGpu* rtGpu = m_gpuRegistry.GetRenderTarget(passDesc.target.renderTarget);
    if (!rt || !rt->ready || !rtGpu || !rtGpu->rtv || !rtGpu->dsv)
        return;

    auto* rtv = rtGpu->rtv;
    auto* dsv = rtGpu->dsv;
    auto* normalRtv = rtGpu->normalRtv;

    ID3D11ShaderResourceView* nullSrvs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
    m_ctx->PSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, nullSrvs);

    if (rt->exposedTexture.IsValid())
        m_executor.TransitionTexture(rt->exposedTexture, ResourceState::ShaderRead, ResourceState::RenderTarget, "ExecuteRenderPass offscreen begin");
    if (rt->exposedNormalsTexture.IsValid())
        m_executor.TransitionTexture(rt->exposedNormalsTexture, ResourceState::ShaderRead, ResourceState::RenderTarget, "ExecuteRenderPass offscreen normals begin");

    const RenderPassClearDesc& clearDesc = passDesc.target.clear;
    if (clearDesc.clearColorEnabled)
        m_ctx->ClearRenderTargetView(rtv, clearDesc.clearColor);
    if (normalRtv)
    {
        const float normalClear[4] = { 0.5f, 0.5f, 1.0f, 1.0f };
        m_ctx->ClearRenderTargetView(normalRtv, normalClear);
    }

    UINT depthStencilFlags = 0u;
    if (clearDesc.clearDepthEnabled) depthStencilFlags |= D3D11_CLEAR_DEPTH;
    if (clearDesc.clearStencilEnabled) depthStencilFlags |= D3D11_CLEAR_STENCIL;
    if (depthStencilFlags != 0u)
        m_ctx->ClearDepthStencilView(dsv, depthStencilFlags, clearDesc.clearDepthValue, clearDesc.clearStencilValue);

    executeGraphics(rtv, normalRtv, dsv, static_cast<float>(rt->width), static_cast<float>(rt->height));

    m_ctx->PSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, nullSrvs);

    if (rt->exposedTexture.IsValid())
        m_executor.TransitionTexture(rt->exposedTexture, ResourceState::RenderTarget, ResourceState::ShaderRead, "ExecuteRenderPass offscreen end");
    if (rt->exposedNormalsTexture.IsValid())
        m_executor.TransitionTexture(rt->exposedNormalsTexture, ResourceState::RenderTarget, ResourceState::ShaderRead, "ExecuteRenderPass offscreen normals end");

    if (m_context)
    {
        auto* backbufferRTV = static_cast<ID3D11RenderTargetView*>(m_context->GetRenderTarget());
        auto* backbufferDSV = static_cast<ID3D11DepthStencilView*>(m_context->GetDepthStencil());
        m_ctx->OMSetRenderTargets(1, &backbufferRTV, backbufferDSV);

        D3D11_VIEWPORT vp = {};
        vp.Width = static_cast<float>(m_backbufferWidth);
        vp.Height = static_cast<float>(m_backbufferHeight);
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        m_ctx->RSSetViewports(1, &vp);
    }
}

void GDXDX11RenderBackend::ExecuteShadowPass(
    const BackendRenderPassDesc& passDesc,
    Registry& registry,
    const ICommandList& commandList,
    ResourceStore<MeshAssetResource, MeshTag>& meshStore,
    ResourceStore<MaterialResource, MaterialTag>& matStore,
    ResourceStore<GDXShaderResource, ShaderTag>& shaderStore,
    ResourceStore<GDXTextureResource, TextureTag>& texStore)
{
    if (!m_ctx || !m_shadowMap.IsReady())
    {
        m_hasShadowPass = false;
        return;
    }

    if (commandList.Empty())
    {
        m_hasShadowPass = false;
        return;
    }

    const FrameData* frame = passDesc.frame;
    if (!frame)
        return;

    const uint32_t numCascades = (frame->shadowCascadeCount > 0u) ? frame->shadowCascadeCount : 1u;
    for (uint32_t cascade = 0u; cascade < numCascades; ++cascade)
    {
        FrameData cascadeFrame = *frame;
        if (frame->shadowCascadeCount > 0u)
            cascadeFrame.shadowViewProjMatrix = frame->shadowCascadeViewProj[cascade];

        m_executor.UpdateFrameConstants(cascadeFrame);
        m_executor.UpdateCascadeConstants(*frame);

        m_hasShadowPass = true;
        m_shadowMap.BeginPass(m_ctx, cascade);
        m_executor.ExecuteShadowQueue(registry, commandList, meshStore, matStore, shaderStore, texStore, m_gpuRegistry);
        m_shadowMap.EndPass(m_ctx);
    }

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

void GDXDX11RenderBackend::ReleasePostProcessSurface(DX11PostProcessSurfaceGpu& surface)
{
    if (surface.srv) { static_cast<ID3D11ShaderResourceView*>(surface.srv)->Release(); surface.srv = nullptr; }
    if (surface.rtv) { static_cast<ID3D11RenderTargetView*>(surface.rtv)->Release(); surface.rtv = nullptr; }
    if (surface.texture) { static_cast<ID3D11Texture2D*>(surface.texture)->Release(); surface.texture = nullptr; }
    surface.width = 0u;
    surface.height = 0u;
    surface.format = GDXTextureFormat::Unknown;
}

void GDXDX11RenderBackend::ReleasePostProcessSurfacePair(DX11PostProcessSurfacePair& pair)
{
    ReleasePostProcessSurface(pair.ping);
    ReleasePostProcessSurface(pair.pong);
    ReleasePostProcessSurface(pair.originalCapture);
}

void GDXDX11RenderBackend::ReleaseAllPostProcessSurfacePairs()
{
    ReleasePostProcessSurfacePair(m_mainPostProcessSurfaces);
    for (auto& entry : m_rttPostProcessSurfaces)
        ReleasePostProcessSurfacePair(entry.second);
    m_rttPostProcessSurfaces.clear();
}

GDXDX11RenderBackend::DX11PostProcessSurfacePair& GDXDX11RenderBackend::GetPostProcessSurfacePair(RenderTargetHandle outputTarget, bool outputToBackbuffer)
{
    if (outputToBackbuffer || !outputTarget.IsValid())
        return m_mainPostProcessSurfaces;

    return m_rttPostProcessSurfaces[outputTarget];
}

bool GDXDX11RenderBackend::EnsurePostProcessSurface(DX11PostProcessSurfaceGpu& surface, uint32_t width, uint32_t height, GDXTextureFormat format, const wchar_t* debugName)
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
    surface.format = format;
    (void)debugName;
    return true;
}

PostProcessHandle GDXDX11RenderBackend::CreatePostProcessPass(ResourceStore<PostProcessResource, PostProcessTag>& postStore,
    const PostProcessPassDesc& desc)
{
    if (!m_device) return PostProcessHandle::Invalid();

    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* psBlob = nullptr;
    if (!GDXDX11CompileShaderFromFile(desc.vertexShaderFile, "main", "vs_5_0", &vsBlob))
        return PostProcessHandle::Invalid();
    if (!GDXDX11CompileShaderFromFile(desc.pixelShaderFile, "main", "ps_5_0", &psBlob))
    {
        vsBlob->Release();
        return PostProcessHandle::Invalid();
    }

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
    // Slot-Datenmodell aufbauen: falls desc.inputSlots leer → Default-Mapping (t0=SceneColor).
    pass.inputs = BuildDefaultPostProcessInputs(desc.inputSlots);

    DX11PostProcessGpu runtime{};
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
    m_gpuRegistry.SetPostProcess(handle, runtime);
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
        m_gpuRegistry.ReleasePostProcess(handle);
        // Store-Eintrag freigeben — kein ready=false-Zombie bleibt zurück.
        postStore.Release(handle);
    }
}

bool GDXDX11RenderBackend::ExecutePostProcessChain(const std::vector<PostProcessHandle>& orderedPasses,
    ResourceStore<PostProcessResource, PostProcessTag>& postStore,
    ResourceStore<GDXTextureResource, TextureTag>& texStore,
    ResourceStore<GDXRenderTargetResource, RenderTargetTag>* rtStore,
    const PostProcessExecutionInputs& execInputs,
    float viewportWidth,
    float viewportHeight,
    RenderTargetHandle outputTarget,
    bool outputToBackbuffer)
{
    if (!m_ctx || !m_context || !execInputs.sceneColor.IsValid()) return false;

    //// ------------------------------DEBUG
    //Debug::Log(
    //    GDX_SRC_LOC,
    //    L"ExecutePostProcessChain orderedPasses.size=" +
    //    std::to_wstring(static_cast<unsigned long long>(orderedPasses.size())) +
    //    L" outputToBackbuffer=" +
    //    std::to_wstring(outputToBackbuffer ? 1ull : 0ull));
    //
    //for (size_t i = 0; i < orderedPasses.size(); ++i)
    //{
    //    const PostProcessHandle h = orderedPasses[i];
    //    const PostProcessResource* pass = postStore.Get(h);
    //
    //    std::wstring name = L"<null>";
    //    std::wstring enabled = L"0";
    //    std::wstring ready = L"0";
    //
    //    if (pass)
    //    {
    //        name = pass->desc.debugName;
    //        enabled = std::to_wstring(pass->enabled ? 1ull : 0ull);
    //        ready = std::to_wstring(pass->ready ? 1ull : 0ull);
    //    }
    //
    //    Debug::Log(
    //        GDX_SRC_LOC,
    //        L"ExecutePostProcessChain ordered[" +
    //        std::to_wstring(static_cast<unsigned long long>(i)) +
    //        L"] handle=" +
    //        std::to_wstring(static_cast<unsigned long long>(h.value)) +
    //        L" name=" + name +
    //        L" ready=" + ready +
    //        L" enabled=" + enabled);
    //}

    // --- Aktive Passes sammeln ---
    std::vector<PostProcessHandle> active;
    active.reserve(orderedPasses.size());
    for (const PostProcessHandle handle : orderedPasses)
    {
        const PostProcessResource* pass = postStore.Get(handle);
        if (pass && pass->ready && pass->enabled)
            active.push_back(handle);
    }
    if (active.empty()) return false;

    // --- SceneColor-Textur als Basis für Ping-Pong ---
    const auto* sceneRes = texStore.Get(execInputs.sceneColor);
    DX11TextureGpu* sceneColorGpu = m_gpuRegistry.GetTexture(execInputs.sceneColor);
    if (!sceneRes || !sceneColorGpu || !sceneColorGpu->srv) return false;

    const uint32_t width  = (std::max)(1u, static_cast<uint32_t>(viewportWidth));
    const uint32_t height = (std::max)(1u, static_cast<uint32_t>(viewportHeight));

    DX11PostProcessSurfacePair& surfacePair = GetPostProcessSurfacePair(outputTarget, outputToBackbuffer);
    const GDXTextureFormat intermediateFormat = MakeLinearPostProcessFormat(sceneRes->format);

    if (active.size() > 1u)
    {
        const wchar_t* pingName = outputToBackbuffer ? L"PostProcessPing_Main" : L"PostProcessPing_RTT";
        const wchar_t* pongName = outputToBackbuffer ? L"PostProcessPong_Main" : L"PostProcessPong_RTT";
        if (!EnsurePostProcessSurface(surfacePair.ping, width, height, intermediateFormat, pingName)) return false;
        if (!EnsurePostProcessSurface(surfacePair.pong, width, height, intermediateFormat, pongName)) return false;
    }

    ID3D11RenderTargetView* backbufferRTV = nullptr;
    if (outputToBackbuffer)
    {
        backbufferRTV = static_cast<ID3D11RenderTargetView*>(m_context->GetRenderTarget());
    }
    else
    {
        if (!rtStore || !outputTarget.IsValid()) return false;
        DX11RenderTargetGpu* outputGpu = m_gpuRegistry.GetRenderTarget(outputTarget);
        if (!outputGpu || !outputGpu->rtv) return false;
        backbufferRTV = outputGpu->rtv;
    }
    // inputSrv: wird pro Pass auf das letzte Ergebnis umgebogen (Ping-Pong SceneColor)
    ID3D11ShaderResourceView* inputSrv = sceneColorGpu->srv;
    ID3D11Texture2D* inputTexture = sceneColorGpu->texture;
    // fallbackOriginalSceneSrv: stabile Szene vom Anfang der gesamten Post-Process-Kette.
    ID3D11ShaderResourceView* fallbackOriginalSceneSrv = sceneColorGpu->srv;
    if (execInputs.originalSceneColor.IsValid())
    {
        if (DX11TextureGpu* originalGpu = m_gpuRegistry.GetTexture(execInputs.originalSceneColor))
            if (originalGpu->srv)
                fallbackOriginalSceneSrv = originalGpu->srv;
    }
    // branchOriginalSceneSrv: Basisbild fuer den aktuellen Branch-Effekt (z. B. GTAO/Bloom Composite).
    ID3D11ShaderResourceView* branchOriginalSceneSrv = fallbackOriginalSceneSrv;

    ID3D11ShaderResourceView* depthSrv   = nullptr;
    ID3D11ShaderResourceView* normalsSrv = nullptr;
    if (execInputs.sceneDepth.IsValid())
    {
        if (DX11TextureGpu* depthGpu = m_gpuRegistry.GetTexture(execInputs.sceneDepth))
            depthSrv = depthGpu->srv;
    }
    if (execInputs.sceneNormals.IsValid())
    {
        if (DX11TextureGpu* normalsGpu = m_gpuRegistry.GetTexture(execInputs.sceneNormals))
            normalsSrv = normalsGpu->srv;
    }

    ID3D11RenderTargetView* tempTargets[2] = {
        static_cast<ID3D11RenderTargetView*>(surfacePair.ping.rtv),
        static_cast<ID3D11RenderTargetView*>(surfacePair.pong.rtv)
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
    vp.Width    = static_cast<float>(width);
    vp.Height   = static_cast<float>(height);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    m_ctx->RSSetViewports(1, &vp);

    // Maximale Anzahl gebundener SRV-Slots: D3D11 erlaubt 128 PS-SRV-Slots.
    // Wir nutzen maximal so viele wie der Pass mit dem meisten Slots braucht.
    constexpr uint32_t kMaxBindSlots = 16u;

    bool executedAtLeastOnePass = false;

    for (size_t i = 0; i < active.size(); ++i)
    {
        PostProcessResource* pass = postStore.Get(active[i]);
        if (!pass)
        {
            Debug::LogWarning(
                GDX_SRC_LOC,
                L"ExecutePostProcess: active[" + std::to_wstring(static_cast<unsigned long long>(i)) +
                L"] hat keinen gueltigen PostProcessResource-Eintrag.");
            continue;
        }
        DX11PostProcessGpu* runtimePtr = m_gpuRegistry.GetPostProcess(active[i]);
        if (!runtimePtr)
        {
            Debug::LogWarning(
                GDX_SRC_LOC,
                L"ExecutePostProcess: active[" + std::to_wstring(static_cast<unsigned long long>(i)) +
                L"] name=" + pass->desc.debugName +
                L" hat kein gueltiges Runtime-GPU-Objekt.");
            continue;
        }
        DX11PostProcessGpu& runtime = *runtimePtr;

        //Debug::Log(
        //    GDX_SRC_LOC,
        //    L"ExecutePostProcess active[" + std::to_wstring(static_cast<unsigned long long>(i)) +
        //    L"] name=" + pass->desc.debugName +
        //    L" enabled=" + std::to_wstring(pass->enabled ? 1ull : 0ull) +
        //    L" PS=" + pass->desc.pixelShaderFile);

        // --- Constant Buffer Upload ---
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

        // --- Deklarative Slot-Auflösung (Stufen C + D) ---
        // inputSrv zeigt auf das aktuelle Ping-Pong-Ergebnis (SceneColor fuer diesen Pass).
        // branchOriginalSceneSrv ist die Basis fuer den aktuellen Branch-Composite.
        // Wichtig: Das muss ein echter Snapshot sein, kein nackter SRV-Zeiger auf Ping/Pong.
        if (pass->desc.captureSceneColorAsOriginal)
        {
            if (inputTexture && EnsurePostProcessSurface(surfacePair.originalCapture, width, height, intermediateFormat, L"PP_OriginalCapture"))
            {
                m_ctx->CopyResource(surfacePair.originalCapture.texture, inputTexture);
                branchOriginalSceneSrv = surfacePair.originalCapture.srv;
            }
            else
            {
                branchOriginalSceneSrv = inputSrv;
            }
        }

        // SRV-Array mit kMaxBindSlots Einträgen vorbereiten (alle null).
        ID3D11ShaderResourceView* srvArray[kMaxBindSlots] = {};
        uint32_t maxBoundSlot = 0u;

        if (pass->inputs.empty())
        {
            // Kein Slot deklariert -> Default-Mapping: t0=SceneColor
            srvArray[0] = inputSrv;
            maxBoundSlot = 1u;
        }
        else
        {
            // Deklarierte Slots auflösen.
            bool hasMissingRequired = false;
            for (const PostProcessInputSlot& slot : pass->inputs)
            {
                // sceneColor: inputSrv (wird pro Pass umgebogen)
                ID3D11ShaderResourceView* srv = nullptr;
                switch (slot.semantic)
                {
                case PostProcessInputSemantic::SceneColor:
                    srv = inputSrv;
                    break;
                case PostProcessInputSemantic::OriginalSceneColor:
                    srv = branchOriginalSceneSrv ? branchOriginalSceneSrv : fallbackOriginalSceneSrv;
                    break;
                case PostProcessInputSemantic::SceneDepth:
                    srv = depthSrv;
                    break;
                case PostProcessInputSemantic::SceneNormals:
                    srv = normalsSrv;
                    break;
                case PostProcessInputSemantic::Custom:
                    if (slot.customTexture.IsValid())
                    {
                        if (const auto* customRes = texStore.Get(slot.customTexture))
                            if (DX11TextureGpu* cGpu = m_gpuRegistry.GetTexture(slot.customTexture))
                                srv = cGpu->srv;
                    }
                    break;
                default: break;
                }

                if (!srv && slot.required)
                {
                    hasMissingRequired = true;
                    Debug::LogWarning(GDX_SRC_LOC,
                        L"PostProcess: Required Slot '" + slot.name + L"' hat keine gueltige Textur — Pass wird uebersprungen.");
                    break;
                }

                const uint32_t reg = slot.shaderRegister;
                if (reg < kMaxBindSlots)
                {
                    srvArray[reg] = srv;
                    if (reg + 1u > maxBoundSlot) maxBoundSlot = reg + 1u;
                }
            }

            if (hasMissingRequired)
            {
                Debug::LogWarning(
                    GDX_SRC_LOC,
                    L"ExecutePostProcess SKIP name=" + pass->desc.debugName +
                    L" reason=missing required input sceneColor=" + std::to_wstring(inputSrv ? 1ull : 0ull) +
                    L" branchOriginalSceneColor=" + std::to_wstring(branchOriginalSceneSrv ? 1ull : 0ull) +
                    L" fallbackOriginalSceneColor=" + std::to_wstring(fallbackOriginalSceneSrv ? 1ull : 0ull) +
                    L" sceneDepth=" + std::to_wstring(depthSrv ? 1ull : 0ull) +
                    L" sceneNormals=" + std::to_wstring(normalsSrv ? 1ull : 0ull));
                continue; // Pass überspringen
            }
        }

        // --- Render Target setzen (Ping-Pong / Backbuffer) ---
        const bool isLast = (i + 1u) == active.size();
        ID3D11RenderTargetView* outputRtv = isLast ? backbufferRTV : tempTargets[i % 2u];
        m_ctx->OMSetRenderTargets(1, &outputRtv, nullptr);
        m_ctx->ClearRenderTargetView(outputRtv, clearColor);

        // --- Shader + SRVs binden ---
        m_ctx->VSSetShader(runtime.vertexShader, nullptr, 0);
        m_ctx->PSSetShader(runtime.pixelShader, nullptr, 0);
        m_ctx->PSSetShaderResources(0, maxBoundSlot, srvArray);

        //Debug::Log(
        //    GDX_SRC_LOC,
        //    L"ExecutePostProcess DRAW name=" + pass->desc.debugName +
        //    L" isLast=" + std::to_wstring(isLast ? 1ull : 0ull) +
        //    L" sceneColor=" + std::to_wstring(inputSrv ? 1ull : 0ull) +
        //    L" originalSceneColor=" + std::to_wstring(originalSceneSrv ? 1ull : 0ull) +
        //    L" sceneDepth=" + std::to_wstring(depthSrv ? 1ull : 0ull) +
        //    L" sceneNormals=" + std::to_wstring(normalsSrv ? 1ull : 0ull));

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
        //Debug::Log(GDX_SRC_LOC,
        //    L"ExecutePostProcess DRAW pass=" + pass->desc.debugName +
        //    L" captureSceneColorAsOriginal=" + std::to_wstring(pass->desc.captureSceneColorAsOriginal ? 1ull : 0ull) +
        //    L" isLast=" + std::to_wstring(isLast ? 1ull : 0ull) +
        //    L" inputSrv=" + std::to_wstring(reinterpret_cast<uintptr_t>(inputSrv)) +
        //    L" branchOriginalSrv=" + std::to_wstring(reinterpret_cast<uintptr_t>(branchOriginalSceneSrv)) +
        //    L" outputRtv=" + std::to_wstring(reinterpret_cast<uintptr_t>(outputRtv)));
        executedAtLeastOnePass = true;

        // SRVs lösen (alle gebundenen Slots nullen)
        ID3D11ShaderResourceView* nullSrvs[kMaxBindSlots] = {};
        m_ctx->PSSetShaderResources(0, maxBoundSlot, nullSrvs);

        // --- Ping-Pong: inputSrv auf das gerade geschriebene Intermediate umbiegen ---
        if (!isLast)
        {
            const DX11PostProcessSurfaceGpu& src =
                (i % 2u == 0u) ? surfacePair.ping : surfacePair.pong;
            inputSrv = src.srv;
            inputTexture = src.texture;
            //Debug::Log(GDX_SRC_LOC,
            //    L"ExecutePostProcess NEXT_SOURCE pass=" + pass->desc.debugName +
            //    L" nextInputSrv=" + std::to_wstring(reinterpret_cast<uintptr_t>(inputSrv)));
        }
        //else
        //{
        //    Debug::Log(GDX_SRC_LOC,
        //        L"ExecutePostProcess FINAL_PRESENT_SOURCE pass=" + pass->desc.debugName +
        //        L" finalInputSrv=" + std::to_wstring(reinterpret_cast<uintptr_t>(inputSrv)) +
        //        L" finalOutputRtv=" + std::to_wstring(reinterpret_cast<uintptr_t>(outputRtv)));
        //}
    }

    // Cleanup
    ID3D11ShaderResourceView* nullSrvs[kMaxBindSlots] = {};
    m_ctx->PSSetShaderResources(0, kMaxBindSlots, nullSrvs);
    ID3D11Buffer* nullCb = nullptr;
    m_ctx->VSSetConstantBuffers(0, 1, &nullCb);
    m_ctx->PSSetConstantBuffers(0, 1, &nullCb);
    m_ctx->VSSetShader(nullptr, nullptr, 0);
    m_ctx->PSSetShader(nullptr, nullptr, 0);
    return executedAtLeastOnePass;
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

    D3D11_DEPTH_STENCIL_DESC dsdNoTest = {};
    dsdNoTest.DepthEnable    = FALSE;
    dsdNoTest.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    dsdNoTest.DepthFunc      = D3D11_COMPARISON_ALWAYS;
    if (FAILED(m_device->CreateDepthStencilState(&dsdNoTest, &m_depthStateNoTest))) return false;

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
            DX11TextureGpu gpu{};
            res.debugName = name;
            res.format = GDXTextureFormat::RGBA8_UNORM;
            if (!GDXTextureLoader_Create1x1(m_device, r, g, b, a, gpu, res)) return false;
            handle = texStore.Add(std::move(res));
            if (handle.IsValid()) m_gpuRegistry.SetTexture(handle, gpu);
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
    // PostProcess resources released via m_gpuRegistry.ReleaseAll()

    if (m_iblIrradiance)  { m_iblIrradiance->Release();  m_iblIrradiance = nullptr; }
    if (m_iblPrefiltered) { m_iblPrefiltered->Release(); m_iblPrefiltered = nullptr; }
    if (m_iblBrdfLut)     { m_iblBrdfLut->Release();     m_iblBrdfLut = nullptr; }
    if (m_tileLightCullCS){ m_tileLightCullCS->Release(); m_tileLightCullCS = nullptr; }


    m_executor.Shutdown();
    m_lightSystem.Shutdown();
    m_gpuRegistry.ReleaseAll();

    {
        std::vector<MaterialHandle> handles;
        // Material GPU resources released via m_gpuRegistry.ReleaseAll()
        for (MaterialHandle h : handles) matStore.Release(h);
    }
    {
        std::vector<ShaderHandle> handles;
        // Shader GPU resources released via m_gpuRegistry.ReleaseAll()
        for (ShaderHandle h : handles) shaderStore.Release(h);
    }
    {
        std::vector<TextureHandle> handles;
        // Texture GPU resources released via m_gpuRegistry.ReleaseAll()
        for (TextureHandle h : handles) texStore.Release(h);
    }

    if (m_blendStateAlpha)      { m_blendStateAlpha->Release();      m_blendStateAlpha = nullptr; }
    if (m_blendState)           { m_blendState->Release();           m_blendState = nullptr; }
    if (m_depthStateNoTest)     { m_depthStateNoTest->Release();     m_depthStateNoTest = nullptr; }
    if (m_depthStateNoWrite)    { m_depthStateNoWrite->Release();    m_depthStateNoWrite = nullptr; }
    if (m_depthStencilState)    { m_depthStencilState->Release();    m_depthStencilState = nullptr; }
    if (m_rasterizerState)      { m_rasterizerState->Release();      m_rasterizerState = nullptr; }
    if (m_rasterizerStateNoCull){ m_rasterizerStateNoCull->Release();m_rasterizerStateNoCull = nullptr; }

    ReleaseAllPostProcessSurfacePairs();
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

    // --- Depth-Textur (typeless, damit DSV + SRV moeglich sind) ---
    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = width;
    depthDesc.Height = height;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

    ID3D11Texture2D* depthTex = nullptr;
    hr = m_device->CreateTexture2D(&depthDesc, nullptr, &depthTex);
    if (FAILED(hr)) { srv->Release(); rtv->Release(); colorTex->Release(); return RenderTargetHandle::Invalid(); }

    D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Texture2D.MipSlice = 0;

    ID3D11DepthStencilView* dsv = nullptr;
    hr = m_device->CreateDepthStencilView(depthTex, &dsvDesc, &dsv);
    if (FAILED(hr)) { depthTex->Release(); srv->Release(); rtv->Release(); colorTex->Release(); return RenderTargetHandle::Invalid(); }

    D3D11_SHADER_RESOURCE_VIEW_DESC depthSrvDesc = {};
    depthSrvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    depthSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    depthSrvDesc.Texture2D.MostDetailedMip = 0;
    depthSrvDesc.Texture2D.MipLevels = 1;

    ID3D11ShaderResourceView* depthSrv = nullptr;
    hr = m_device->CreateShaderResourceView(depthTex, &depthSrvDesc, &depthSrv);
    if (FAILED(hr))
    {
        dsv->Release();
        depthTex->Release();
        srv->Release();
        rtv->Release();
        colorTex->Release();
        return RenderTargetHandle::Invalid();
    }

    // --- Normal-Textur (MRT / Screen Normal Buffer) ---
    D3D11_TEXTURE2D_DESC normalDesc = {};
    normalDesc.Width = width;
    normalDesc.Height = height;
    normalDesc.MipLevels = 1;
    normalDesc.ArraySize = 1;
    normalDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    normalDesc.SampleDesc.Count = 1;
    normalDesc.Usage = D3D11_USAGE_DEFAULT;
    normalDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    ID3D11Texture2D* normalTex = nullptr;
    hr = m_device->CreateTexture2D(&normalDesc, nullptr, &normalTex);
    if (FAILED(hr))
    {
        depthSrv->Release();
        dsv->Release();
        depthTex->Release();
        srv->Release();
        rtv->Release();
        colorTex->Release();
        return RenderTargetHandle::Invalid();
    }

    ID3D11RenderTargetView* normalRtv = nullptr;
    hr = m_device->CreateRenderTargetView(normalTex, nullptr, &normalRtv);
    if (FAILED(hr))
    {
        normalTex->Release();
        depthSrv->Release();
        dsv->Release();
        depthTex->Release();
        srv->Release();
        rtv->Release();
        colorTex->Release();
        return RenderTargetHandle::Invalid();
    }

    ID3D11ShaderResourceView* normalSrv = nullptr;
    hr = m_device->CreateShaderResourceView(normalTex, nullptr, &normalSrv);
    if (FAILED(hr))
    {
        normalRtv->Release();
        normalTex->Release();
        depthSrv->Release();
        dsv->Release();
        depthTex->Release();
        srv->Release();
        rtv->Release();
        colorTex->Release();
        return RenderTargetHandle::Invalid();
    }

    // --- SRVs als Engine-Texturen registrieren ---
    GDXTextureResource texRes;
    texRes.width    = width;
    texRes.height   = height;
    texRes.ready    = true;
    texRes.isSRGB   = (colorFormat == GDXTextureFormat::RGBA8_UNORM_SRGB);
    texRes.format   = colorFormat;
    texRes.semantic = GDXTextureSemantic::RenderTarget;
    texRes.debugName = debugName + L"_Tex";
    TextureHandle exposedTex = texStore.Add(std::move(texRes));
    if (exposedTex.IsValid())
    {
        srv->AddRef();
        DX11TextureGpu texGpu{};
        texGpu.srv = srv;
        m_gpuRegistry.SetTexture(exposedTex, texGpu);
        m_executor.TransitionTexture(exposedTex, ResourceState::Unknown, ResourceState::ShaderRead, "CreateRenderTarget initial state");
    }

    GDXTextureResource depthTexRes;
    depthTexRes.width    = width;
    depthTexRes.height   = height;
    depthTexRes.ready    = true;
    depthTexRes.isSRGB   = false;
    depthTexRes.format   = GDXTextureFormat::Unknown;
    depthTexRes.semantic = GDXTextureSemantic::Depth;
    depthTexRes.debugName = debugName + L"_Depth";
    TextureHandle exposedDepthTex = texStore.Add(std::move(depthTexRes));
    if (exposedDepthTex.IsValid())
    {
        depthSrv->AddRef();
        DX11TextureGpu depthGpu{};
        depthGpu.srv = depthSrv;
        m_gpuRegistry.SetTexture(exposedDepthTex, depthGpu);
        m_executor.TransitionTexture(exposedDepthTex, ResourceState::Unknown, ResourceState::ShaderRead, "CreateRenderTarget depth initial state");
    }

    GDXTextureResource normalsTexRes;
    normalsTexRes.width    = width;
    normalsTexRes.height   = height;
    normalsTexRes.ready    = true;
    normalsTexRes.isSRGB   = false;
    normalsTexRes.format   = GDXTextureFormat::RGBA8_UNORM;
    normalsTexRes.semantic = GDXTextureSemantic::RenderTarget;
    normalsTexRes.debugName = debugName + L"_Normals";
    TextureHandle exposedNormalsTex = texStore.Add(std::move(normalsTexRes));
    if (exposedNormalsTex.IsValid())
    {
        normalSrv->AddRef();
        DX11TextureGpu normalsGpu{};
        normalsGpu.srv = normalSrv;
        m_gpuRegistry.SetTexture(exposedNormalsTex, normalsGpu);
        m_executor.TransitionTexture(exposedNormalsTex, ResourceState::Unknown, ResourceState::ShaderRead, "CreateRenderTarget normals initial state");
    }

    // --- RenderTargetResource anlegen ---
    GDXRenderTargetResource rt;
    rt.width = width;
    rt.height = height;
    rt.ready = true;
    rt.colorFormat = colorFormat;
    rt.exposedTexture = exposedTex;
    rt.exposedDepthTexture = exposedDepthTex;
    rt.exposedNormalsTexture = exposedNormalsTex;
    rt.debugName = debugName;

    const RenderTargetHandle handle = rtStore.Add(std::move(rt));
    if (handle.IsValid())
    {
        DX11RenderTargetGpu rtGpu{};
        rtGpu.colorTexture = colorTex;
        rtGpu.rtv = rtv;
        rtGpu.srv = srv;
        rtGpu.depthTexture = depthTex;
        rtGpu.dsv = dsv;
        rtGpu.depthSrv = depthSrv;
        rtGpu.normalTexture = normalTex;
        rtGpu.normalRtv = normalRtv;
        rtGpu.normalSrv = normalSrv;
        m_gpuRegistry.SetRenderTarget(handle, rtGpu);

        //static uint64_t s_rtCreateCount = 0;
        //Debug::Log(GDX_SRC_LOC, "RT create #", ++s_rtCreateCount,
        //           " handle=", handle.value,
        //           " name=", debugName.c_str(),
        //           " size=", width, "x", height,
        //           " fmt=", static_cast<uint32_t>(colorFormat),
        //           " colorTex=", exposedTex.value,
        //           " depthTex=", exposedDepthTex.value,
        //           " rtAlive=", rtStore.AliveCount(),
        //           " texAlive=", texStore.AliveCount(),
        //           " trackedTexStates=", m_executor.DebugTrackedTextureStateCount());
    }
    return handle;
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
    if (auto it = m_rttPostProcessSurfaces.find(handle); it != m_rttPostProcessSurfaces.end())
    {
        ReleasePostProcessSurfacePair(it->second);
        m_rttPostProcessSurfaces.erase(it);
    }

    m_gpuRegistry.ReleaseRenderTarget(handle);

    GDXRenderTargetResource* rt = rtStore.Get(handle);
    if (rt)
    {
        static uint64_t s_rtDestroyCount = 0;
        //Debug::LogWarning(GDX_SRC_LOC, "RT destroy #", ++s_rtDestroyCount,
        //                  " handle=", handle.value,
        //                  " colorTex=", rt->exposedTexture.value,
        //                  " depthTex=", rt->exposedDepthTexture.value,
        //                  " normalsTex=", rt->exposedNormalsTexture.value,
        //                  " rtAliveBefore=", rtStore.AliveCount(),
        //                  " texAliveBefore=", texStore.AliveCount(),
        //                  " trackedTexStatesBefore=", m_executor.DebugTrackedTextureStateCount());
        
        if (rt->exposedTexture.IsValid())
        {
            m_executor.ForgetTextureState(rt->exposedTexture);
            m_gpuRegistry.ReleaseTexture(rt->exposedTexture);
            texStore.Remove(rt->exposedTexture);
        }
        if (rt->exposedDepthTexture.IsValid())
        {
            m_executor.ForgetTextureState(rt->exposedDepthTexture);
            m_gpuRegistry.ReleaseTexture(rt->exposedDepthTexture);
            texStore.Remove(rt->exposedDepthTexture);
        }
        if (rt->exposedNormalsTexture.IsValid())
        {
            m_executor.ForgetTextureState(rt->exposedNormalsTexture);
            m_gpuRegistry.ReleaseTexture(rt->exposedNormalsTexture);
            texStore.Remove(rt->exposedNormalsTexture);
        }
    }
    rtStore.Remove(handle);

    //Debug::LogWarning(GDX_SRC_LOC, "RT destroy done handle=", handle.value,
    //                  " rtAliveNow=", rtStore.AliveCount(),
    //                  " texAliveNow=", texStore.AliveCount(),
    //                  " rttSurfacePairs=", m_rttPostProcessSurfaces.size(),
    //                  " trackedTexStatesNow=", m_executor.DebugTrackedTextureStateCount());
}
