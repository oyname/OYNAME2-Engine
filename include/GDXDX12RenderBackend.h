#pragma once

#include "IGDXRenderBackend.h"

class GDXDX12RenderBackend final : public IGDXRenderBackend
{
public:
    GDXDX12RenderBackend() = default;
    ~GDXDX12RenderBackend() override = default;

    bool Initialize(ResourceStore<GDXTextureResource, TextureTag>&) override { return false; }
    void BeginFrame(const float[4]) override {}
    void Present(bool) override {}
    void Resize(int, int) override {}
    void Shutdown(ResourceStore<MaterialResource, MaterialTag>&,
                  ResourceStore<GDXShaderResource, ShaderTag>&,
                  ResourceStore<GDXTextureResource, TextureTag>&) override {}

    ShaderHandle CreateShader(ResourceStore<GDXShaderResource, ShaderTag>&,
                              const std::wstring&, const std::wstring&, uint32_t,
                              const GDXShaderLayout&, const std::wstring&) override { return ShaderHandle::Invalid(); }
    TextureHandle CreateTexture(ResourceStore<GDXTextureResource, TextureTag>&,
                                const std::wstring&, bool, TextureHandle fallback) override { return fallback; }
    TextureHandle CreateTextureFromImage(ResourceStore<GDXTextureResource, TextureTag>&,
                                         const ImageBuffer&, bool, const std::wstring&, TextureHandle fallback) override { return fallback; }
    bool UploadMesh(MeshAssetResource&) override { return false; }
    bool CreateMaterialGpu(MaterialResource&) override { return false; }
    void UpdateLights(Registry&, FrameData&) override {}
    void UpdateFrameConstants(const FrameData&) override {}
    void* ExecuteRenderPass(const BackendRenderPassDesc&,
                            Registry&,
                            const ICommandList&,
                            ResourceStore<MeshAssetResource, MeshTag>&,
                            ResourceStore<MaterialResource, MaterialTag>&,
                            ResourceStore<GDXShaderResource, ShaderTag>&,
                            ResourceStore<GDXTextureResource, TextureTag>&,
                            ResourceStore<GDXRenderTargetResource, RenderTargetTag>* = nullptr) override { return nullptr; }
    PostProcessHandle CreatePostProcessPass(ResourceStore<PostProcessResource, PostProcessTag>&, const PostProcessPassDesc&) override { return PostProcessHandle::Invalid(); }
    bool UpdatePostProcessConstants(PostProcessResource&, const void*, uint32_t) override { return false; }
    void DestroyPostProcessPasses(ResourceStore<PostProcessResource, PostProcessTag>&) override {}
    bool ExecutePostProcessChain(const std::vector<PostProcessHandle>&, ResourceStore<PostProcessResource, PostProcessTag>&, ResourceStore<GDXTextureResource, TextureTag>&, TextureHandle, float, float) override { return false; }
    uint32_t GetDrawCallCount() const override { return 0u; }
    bool HasShadowResources() const override { return false; }
    const DefaultTextureSet& GetDefaultTextures() const override { return m_defaults; }

private:
    DefaultTextureSet m_defaults{};
};
