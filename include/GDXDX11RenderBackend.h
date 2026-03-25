#pragma once

#include "IGDXRenderBackend.h"
#include "GDXDX11GpuResources.h"
#include "GDXIBLBaker.h"
#include "IGDXDXGIContext.h"
#include "GDXDX11RenderExecutor.h"
#include "GDXDX11LightSystem.h"
#include "GDXSamplerCache.h"
#include "GDXDX11TileLightCuller.h"
#include "GDXShadowMap.h"
#include "ResourceStore.h"
#include "GDXRenderTargetResource.h"
#include "PostProcessResource.h"

#include <memory>
#include <unordered_map>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RasterizerState;
struct ID3D11DepthStencilState;
struct ID3D11BlendState;
struct ID3D11VertexShader;
struct ID3D11PixelShader;
struct ID3D11Buffer;
struct ID3D11ComputeShader;

class GDXDX11RenderBackend final : public IGDXRenderBackend
{
public:
    explicit GDXDX11RenderBackend(std::unique_ptr<IGDXDXGIContext> context);
    ~GDXDX11RenderBackend() override;

    bool Initialize(ResourceStore<GDXTextureResource, TextureTag>& texStore) override;
    void BeginFrame(const float clearColor[4]) override;
    void Present(bool vsync) override;
    void Resize(int w, int h) override;
    void Shutdown(ResourceStore<MaterialResource, MaterialTag>& matStore,
                  ResourceStore<GDXShaderResource, ShaderTag>& shaderStore,
                  ResourceStore<GDXTextureResource, TextureTag>& texStore) override;

    

    ShaderHandle UploadShader(
        ResourceStore<GDXShaderResource, ShaderTag>& shaderStore,
        const ShaderSourceDesc& desc) override;

    TextureHandle UploadTexture(
        ResourceStore<GDXTextureResource, TextureTag>& texStore,
        const std::wstring& filePath,
        bool isSRGB,
        TextureHandle fallbackOnFailure) override;

    TextureHandle UploadTextureFromImage(
        ResourceStore<GDXTextureResource, TextureTag>& texStore,
        const ImageBuffer& image,
        bool isSRGB,
        const std::wstring& debugName,
        TextureHandle fallbackOnFailure) override;

    bool UploadMesh(MeshHandle handle, MeshAssetResource& mesh) override;
    bool UploadMaterial(MaterialHandle handle, MaterialResource& mat) override;

    void ExtractLightData(Registry& registry, FrameData& frame) override;
    void UploadLightConstants(const FrameData& frame) override;
    void UpdateFrameConstants(const FrameData& frame) override;

    void ExecuteRenderPass(
        const BackendRenderPassDesc& passDesc,
        Registry& registry,
        const ICommandList& opaqueList,
        const ICommandList& alphaList,
        ResourceStore<MeshAssetResource,       MeshTag>&        meshStore,
        ResourceStore<MaterialResource,        MaterialTag>&     matStore,
        ResourceStore<GDXShaderResource,       ShaderTag>&       shaderStore,
        ResourceStore<GDXTextureResource,      TextureTag>&      texStore,
        ResourceStore<GDXRenderTargetResource, RenderTargetTag>& rtStore) override;

    void ExecuteShadowPass(
        const BackendRenderPassDesc& passDesc,
        Registry& registry,
        const ICommandList& commandList,
        ResourceStore<MeshAssetResource,  MeshTag>&      meshStore,
        ResourceStore<MaterialResource,   MaterialTag>&  matStore,
        ResourceStore<GDXShaderResource,  ShaderTag>&    shaderStore,
        ResourceStore<GDXTextureResource, TextureTag>&   texStore) override;


    PostProcessHandle CreatePostProcessPass(ResourceStore<PostProcessResource, PostProcessTag>& postStore,
                                            const PostProcessPassDesc& desc) override;
    bool UpdatePostProcessConstants(PostProcessResource& pass, const void* data, uint32_t size) override;
    void DestroyPostProcessPasses(ResourceStore<PostProcessResource, PostProcessTag>& postStore) override;
    bool ExecutePostProcessChain(const std::vector<PostProcessHandle>& orderedPasses,
                                ResourceStore<PostProcessResource, PostProcessTag>& postStore,
                                ResourceStore<GDXTextureResource, TextureTag>& texStore,
                                ResourceStore<GDXRenderTargetResource, RenderTargetTag>* rtStore,
                                const PostProcessExecutionInputs& execInputs,
                                float viewportWidth,
                                float viewportHeight,
                                RenderTargetHandle outputTarget = RenderTargetHandle::Invalid(),
                                bool outputToBackbuffer = true) override;

    uint32_t GetDrawCallCount() const override;
    bool HasShadowResources() const override;
    bool SupportsTextureFormat(GDXTextureFormat format) const override;
    const DefaultTextureSet& GetDefaultTextures() const override;

    size_t DebugRttSurfacePairCount() const { return m_rttPostProcessSurfaces.size(); }
    size_t DebugTrackedTextureStateCount() const { return m_executor.DebugTrackedTextureStateCount(); }
    size_t DebugPipelineCacheSize() const noexcept { return m_executor.DebugPipelineCacheSize(); }
    size_t DebugLayoutCacheSize() const noexcept { return m_executor.DebugLayoutCacheSize(); }

    // Render-Target-Erstellung (Offscreen RTT)
    RenderTargetHandle CreateRenderTarget(
        ResourceStore<GDXRenderTargetResource, RenderTargetTag>& rtStore,
        ResourceStore<GDXTextureResource,      TextureTag>&      texStore,
        uint32_t width, uint32_t height,
        const std::wstring& debugName,
        GDXTextureFormat colorFormat = GDXTextureFormat::RGBA8_UNORM) override;

    void DestroyRenderTarget(
        RenderTargetHandle handle,
        ResourceStore<GDXRenderTargetResource, RenderTargetTag>& rtStore,
        ResourceStore<GDXTextureResource,      TextureTag>&      texStore) override;

    void SetShadowMapSize(uint32_t size) override { m_shadowMapSize = size; }
    void SetDebugSmokeTestMode(GDXDebugSmokeTestMode mode) override { m_smokeTestMode = mode; }
    void LoadIBL(const wchar_t* hdrPath) override;

private:

    bool CreateRenderStates();
    struct DX11PostProcessSurfacePair
    {
        DX11PostProcessSurfaceGpu ping;
        DX11PostProcessSurfaceGpu pong;
    };

    void ReleasePostProcessSurface(DX11PostProcessSurfaceGpu& surface);
    void ReleasePostProcessSurfacePair(DX11PostProcessSurfacePair& pair);
    void ReleaseAllPostProcessSurfacePairs();
    DX11PostProcessSurfacePair& GetPostProcessSurfacePair(RenderTargetHandle outputTarget, bool outputToBackbuffer);
    bool EnsurePostProcessSurface(DX11PostProcessSurfaceGpu& surface, uint32_t width, uint32_t height, GDXTextureFormat format, const wchar_t* debugName);
    bool InitDefaultTextures(ResourceStore<GDXTextureResource, TextureTag>& texStore);

    bool EnsureDebugSmokeResources();
    bool ExecuteDebugSmokePass(const FrameData* frame, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, float viewportWidth, float viewportHeight);
    bool DrawDebugFullscreenTriangle(ID3D11RenderTargetView* rtv, float viewportWidth, float viewportHeight);
    bool DrawDebugPositionTriangle(const FrameData* frame, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, float viewportWidth, float viewportHeight, bool withVertexColor);

    GDXDebugSmokeTestMode m_smokeTestMode = GDXDebugSmokeTestMode::None;

    std::unique_ptr<IGDXDXGIContext> m_context;
    ID3D11Device*        m_device = nullptr;
    ID3D11DeviceContext* m_ctx    = nullptr;

    std::unique_ptr<GDXDX11MeshUploader> m_meshUploader;
    GDXDX11RenderExecutor m_executor;
    GDXSamplerCache m_samplerCache;
    GDXShadowMap    m_shadowMap;
    GDXDX11LightSystem  m_lightSystem;
    GDXDX11TileLightCuller  m_tileCuller;
    ID3D11ComputeShader* m_tileLightCullCS = nullptr;
    uint32_t        m_shadowMapSize = 2048u;

    DefaultTextureSet     m_defaultTextures;
    GDXDX11GpuRegistry    m_gpuRegistry;

    ID3D11RasterizerState*   m_rasterizerState       = nullptr;
    ID3D11RasterizerState*   m_rasterizerStateNoCull = nullptr;  // CULL_NONE für double-sided / alpha-test
    ID3D11DepthStencilState* m_depthStencilState = nullptr;
    ID3D11DepthStencilState* m_depthStateNoWrite = nullptr;
    ID3D11DepthStencilState* m_depthStateNoTest  = nullptr;
    ID3D11BlendState*        m_blendState        = nullptr;
    ID3D11BlendState*        m_blendStateAlpha   = nullptr;

    ID3D11VertexShader* m_debugFullscreenVS   = nullptr;
    ID3D11VertexShader* m_debugPositionOnlyVS = nullptr;
    ID3D11VertexShader* m_debugVertexColorVS  = nullptr;
    ID3D11PixelShader*  m_debugSolidPS        = nullptr;
    ID3D11PixelShader*  m_debugVertexColorPS  = nullptr;
    ID3D11InputLayout*  m_debugPosInputLayout = nullptr;
    ID3D11InputLayout*  m_debugPosColorInputLayout = nullptr;
    ID3D11Buffer*       m_debugSmokeCB        = nullptr;
    ID3D11Buffer*       m_debugPosTriangleVB  = nullptr;
    ID3D11Buffer*       m_debugPosColorTriangleVB = nullptr;

    // IBL-SRVs — werden von LoadIBL() aus GDXIBLData hochgeladen
    ID3D11ShaderResourceView* m_iblIrradiance  = nullptr;  // t17
    ID3D11ShaderResourceView* m_iblPrefiltered = nullptr;  // t18
    ID3D11ShaderResourceView* m_iblBrdfLut     = nullptr;  // t19
    bool                      m_iblValid       = false;
    bool m_hasShadowPass = false;
    int m_backbufferWidth  = 1;
    int m_backbufferHeight = 1;

    DX11PostProcessSurfacePair m_mainPostProcessSurfaces;
    std::unordered_map<RenderTargetHandle, DX11PostProcessSurfacePair> m_rttPostProcessSurfaces;
};
