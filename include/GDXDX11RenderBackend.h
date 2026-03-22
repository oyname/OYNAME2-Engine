#pragma once

#include "IGDXRenderBackend.h"
#include "GDXIBLBaker.h"
#include "IGDXDXGIContext.h"
#include "GDXDX11RenderExecutor.h"
#include "GDXDX11LightSystem.h"
#include "GDXSamplerCache.h"
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

    ShaderHandle CreateShader(ResourceStore<GDXShaderResource, ShaderTag>& shaderStore,
                              const std::wstring& vsFile,
                              const std::wstring& psFile,
                              uint32_t vertexFlags,
                              const GDXShaderLayout& layout,
                              const std::wstring& debugName) override;

    TextureHandle CreateTexture(ResourceStore<GDXTextureResource, TextureTag>& texStore,
                                const std::wstring& filePath,
                                bool isSRGB,
                                TextureHandle fallbackOnFailure) override;

    TextureHandle CreateTextureFromImage(ResourceStore<GDXTextureResource, TextureTag>& texStore,
                                         const ImageBuffer& image,
                                         bool isSRGB,
                                         const std::wstring& debugName,
                                         TextureHandle fallbackOnFailure) override;

    bool UploadMesh(MeshAssetResource& mesh) override;
    bool CreateMaterialGpu(MaterialResource& mat) override;

    void ExtractLightData(Registry& registry, FrameData& frame) override;
    void UploadLightConstants(const FrameData& frame) override;
    void UpdateFrameConstants(const FrameData& frame) override;

    void* ExecuteRenderPass(const BackendRenderPassDesc& passDesc,
                            Registry& registry,
                            const ICommandList& commandList,
                            ResourceStore<MeshAssetResource, MeshTag>& meshStore,
                            ResourceStore<MaterialResource, MaterialTag>& matStore,
                            ResourceStore<GDXShaderResource, ShaderTag>& shaderStore,
                            ResourceStore<GDXTextureResource, TextureTag>& texStore,
                            ResourceStore<GDXRenderTargetResource, RenderTargetTag>* rtStore = nullptr) override;


    PostProcessHandle CreatePostProcessPass(ResourceStore<PostProcessResource, PostProcessTag>& postStore,
                                            const PostProcessPassDesc& desc) override;
    bool UpdatePostProcessConstants(PostProcessResource& pass, const void* data, uint32_t size) override;
    void DestroyPostProcessPasses(ResourceStore<PostProcessResource, PostProcessTag>& postStore) override;
    bool ExecutePostProcessChain(const std::vector<PostProcessHandle>& orderedPasses,
                                 ResourceStore<PostProcessResource, PostProcessTag>& postStore,
                                 ResourceStore<GDXTextureResource, TextureTag>& texStore,
                                 TextureHandle sceneTexture,
                                 float viewportWidth,
                                 float viewportHeight) override;

    uint32_t GetDrawCallCount() const override;
    bool HasShadowResources() const override;
    bool SupportsTextureFormat(GDXTextureFormat format) const override;
    const DefaultTextureSet& GetDefaultTextures() const override;

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
    void LoadIBL(const wchar_t* hdrPath) override;

private:

    struct Dx11PostProcessRuntime
    {
        ID3D11VertexShader* vertexShader = nullptr;
        ID3D11PixelShader* pixelShader = nullptr;
        ID3D11Buffer* constantBuffer = nullptr;
    };

    struct Dx11PostProcessSurface
    {
        void* texture = nullptr;
        void* rtv = nullptr;
        void* srv = nullptr;
        uint32_t width = 0u;
        uint32_t height = 0u;
        GDXTextureFormat format = GDXTextureFormat::Unknown;
    };

    bool CreateRenderStates();
    void ReleasePostProcessSurface(Dx11PostProcessSurface& surface);
    bool EnsurePostProcessSurface(Dx11PostProcessSurface& surface, uint32_t width, uint32_t height, GDXTextureFormat format, const wchar_t* debugName);
    bool InitDefaultTextures(ResourceStore<GDXTextureResource, TextureTag>& texStore);

    std::unique_ptr<IGDXDXGIContext> m_context;
    ID3D11Device*        m_device = nullptr;
    ID3D11DeviceContext* m_ctx    = nullptr;

    std::unique_ptr<GDXDX11MeshUploader> m_meshUploader;
    GDXDX11RenderExecutor m_executor;
    GDXSamplerCache m_samplerCache;
    GDXShadowMap    m_shadowMap;
    GDXDX11LightSystem  m_lightSystem;
    uint32_t        m_shadowMapSize = 2048u;

    DefaultTextureSet m_defaultTextures;

    ID3D11RasterizerState*   m_rasterizerState       = nullptr;
    ID3D11RasterizerState*   m_rasterizerStateNoCull = nullptr;  // CULL_NONE für double-sided / alpha-test
    ID3D11DepthStencilState* m_depthStencilState = nullptr;
    ID3D11DepthStencilState* m_depthStateNoWrite = nullptr;
    ID3D11BlendState*        m_blendState        = nullptr;
    ID3D11BlendState*        m_blendStateAlpha   = nullptr;

    // IBL-SRVs — werden von LoadIBL() aus GDXIBLData hochgeladen
    ID3D11ShaderResourceView* m_iblIrradiance  = nullptr;  // t17
    ID3D11ShaderResourceView* m_iblPrefiltered = nullptr;  // t18
    ID3D11ShaderResourceView* m_iblBrdfLut     = nullptr;  // t19
    bool                      m_iblValid       = false;
    bool m_hasShadowPass = false;
    int m_backbufferWidth  = 1;
    int m_backbufferHeight = 1;

    Dx11PostProcessSurface m_postProcessPing;
    Dx11PostProcessSurface m_postProcessPong;
    std::unordered_map<PostProcessHandle, Dx11PostProcessRuntime> m_postProcessRuntime;
};
