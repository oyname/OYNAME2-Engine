#pragma once

#include "IGDXRenderBackend.h"
#include "IGDXOpenGLContext.h"

#include <memory>

class GDXOpenGLRenderBackend final : public IGDXRenderBackend
{
public:
    explicit GDXOpenGLRenderBackend(std::unique_ptr<IGDXOpenGLContext> context);
    ~GDXOpenGLRenderBackend() override = default;

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

    void* ExecuteRenderPass(const BackendRenderPassDesc& passDesc,
                            Registry& registry,
                            const ICommandList& commandList,
                            ResourceStore<MeshAssetResource, MeshTag>& meshStore,
                            ResourceStore<MaterialResource, MaterialTag>& matStore,
                            ResourceStore<GDXShaderResource, ShaderTag>& shaderStore,
                            ResourceStore<GDXTextureResource, TextureTag>& texStore,
                            ResourceStore<GDXRenderTargetResource, RenderTargetTag>* rtStore = nullptr) override;

    void SetShadowMapSize(uint32_t size) override;

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
    const DefaultTextureSet& GetDefaultTextures() const override;

private:
    std::unique_ptr<IGDXOpenGLContext> m_context;
    DefaultTextureSet m_defaultTextures;
    float m_clearColor[4] = { 0.f, 0.f, 0.f, 1.f };
    int m_width = 0;
    int m_height = 0;
};
