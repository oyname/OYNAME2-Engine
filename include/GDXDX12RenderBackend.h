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

    ShaderHandle UploadShader(ResourceStore<GDXShaderResource, ShaderTag>& shaderStore,
                              const ShaderSourceDesc&) override { (void)shaderStore; return ShaderHandle::Invalid(); }
    TextureHandle UploadTexture(ResourceStore<GDXTextureResource, TextureTag>&,
                                const std::wstring&, bool, TextureHandle fallback) override { return fallback; }
    TextureHandle UploadTextureFromImage(ResourceStore<GDXTextureResource, TextureTag>&,
                                         const ImageBuffer&, bool, const std::wstring&, TextureHandle fallback) override { return fallback; }
    bool UploadMesh(MeshHandle, MeshAssetResource&) override { return false; }
    bool UploadMaterial(MaterialHandle, MaterialResource&) override { return false; }
    void ExtractLightData(Registry&, FrameData&) override {}
    void UploadLightConstants(const FrameData&) override {}
    void UpdateFrameConstants(const FrameData&) override {}
    void ExecuteShadowPass(
        const BackendRenderPassDesc& passDesc,
        Registry& registry,
        const ICommandList& commandList,
        ResourceStore<MeshAssetResource,  MeshTag>&      meshStore,
        ResourceStore<MaterialResource,   MaterialTag>&  matStore,
        ResourceStore<GDXShaderResource,  ShaderTag>&    shaderStore,
        ResourceStore<GDXTextureResource, TextureTag>&   texStore) override {}

    void ExecuteRenderPass(const BackendRenderPassDesc&,
                            Registry&,
                            const ICommandList&,
                            const ICommandList&,
                            ResourceStore<MeshAssetResource, MeshTag>&,
                            ResourceStore<MaterialResource, MaterialTag>&,
                            ResourceStore<GDXShaderResource, ShaderTag>&,
                            ResourceStore<GDXTextureResource, TextureTag>&,
                            ResourceStore<GDXRenderTargetResource, RenderTargetTag>&) override {}
    PostProcessHandle CreatePostProcessPass(ResourceStore<PostProcessResource, PostProcessTag>&, const PostProcessPassDesc&) override { return PostProcessHandle::Invalid(); }
    bool UpdatePostProcessConstants(PostProcessResource&, const void*, uint32_t) override { return false; }
    void DestroyPostProcessPasses(ResourceStore<PostProcessResource, PostProcessTag>&) override {}
    bool ExecutePostProcessChain(const std::vector<PostProcessHandle>&, ResourceStore<PostProcessResource, PostProcessTag>&, ResourceStore<GDXTextureResource, TextureTag>&, ResourceStore<GDXRenderTargetResource, RenderTargetTag>*, const PostProcessExecutionInputs&, float, float, RenderTargetHandle = RenderTargetHandle::Invalid(), bool = true) override { return false; }
    void LoadIBL(const wchar_t*) override {}
    uint32_t GetDrawCallCount() const override { return 0u; }
    bool HasShadowResources() const override { return false; }
    const DefaultTextureSet& GetDefaultTextures() const override { return m_defaults; }

private:
    DefaultTextureSet m_defaults{};
};
