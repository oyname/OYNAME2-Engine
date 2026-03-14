#pragma once

#include "IGDXRenderBackend.h"
#include "IGDXDXGIContext.h"
#include "GDXDX11RenderExecutor.h"
#include "GDXDX11LightSystem.h"
#include "GDXSamplerCache.h"
#include "GDXShadowMap.h"
#include "ResourceStore.h"
#include "GDXRenderTargetResource.h"

#include <memory>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RasterizerState;
struct ID3D11DepthStencilState;
struct ID3D11BlendState;

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

    void UpdateLights(Registry& registry, FrameData& frame) override;
    void UpdateFrameConstants(const FrameData& frame) override;

    void ExecuteShadowPass(Registry& registry,
                           const RenderQueue& shadowQueue,
                           ResourceStore<MeshAssetResource, MeshTag>& meshStore,
                           ResourceStore<MaterialResource, MaterialTag>& matStore,
                           ResourceStore<GDXShaderResource, ShaderTag>& shaderStore,
                           ResourceStore<GDXTextureResource, TextureTag>& texStore,
                           const FrameData& frame) override;

    void* ExecuteMainPass(Registry& registry,
                          const RenderQueue& opaqueQueue,
                          ResourceStore<MeshAssetResource, MeshTag>& meshStore,
                          ResourceStore<MaterialResource, MaterialTag>& matStore,
                          ResourceStore<GDXShaderResource, ShaderTag>& shaderStore,
                          ResourceStore<GDXTextureResource, TextureTag>& texStore) override;

    void* ExecuteMainPassToTarget(GDXRenderTargetResource& rt,
                                  const RenderPassClearDesc& clearDesc,
                                  Registry& registry,
                                  const RenderQueue& opaqueQueue,
                                  ResourceStore<MeshAssetResource, MeshTag>& meshStore,
                                  ResourceStore<MaterialResource, MaterialTag>& matStore,
                                  ResourceStore<GDXShaderResource, ShaderTag>& shaderStore,
                                  ResourceStore<GDXTextureResource, TextureTag>& texStore) override;

    uint32_t GetDrawCallCount() const override;
    bool HasShadowResources() const override;
    const DefaultTextureSet& GetDefaultTextures() const override;

    // Render-Target-Erstellung (Offscreen RTT)
    RenderTargetHandle CreateRenderTarget(
        ResourceStore<GDXRenderTargetResource, RenderTargetTag>& rtStore,
        ResourceStore<GDXTextureResource,      TextureTag>&      texStore,
        uint32_t width, uint32_t height,
        const std::wstring& debugName) override;

    void SetShadowMapSize(uint32_t size) { m_shadowMapSize = size; }

private:
    bool CreateRenderStates();
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
};
