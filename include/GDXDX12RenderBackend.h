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
    void ExecuteShadowPass(Registry&, const RenderQueue&,
                           ResourceStore<MeshAssetResource, MeshTag>&,
                           ResourceStore<MaterialResource, MaterialTag>&,
                           ResourceStore<GDXShaderResource, ShaderTag>&,
                           ResourceStore<GDXTextureResource, TextureTag>&,
                           const FrameData&) override {}
    void* ExecuteMainPass(Registry&, const RenderQueue&, const RenderQueue&,
                          ResourceStore<MeshAssetResource, MeshTag>&,
                          ResourceStore<MaterialResource, MaterialTag>&,
                          ResourceStore<GDXShaderResource, ShaderTag>&,
                          ResourceStore<GDXTextureResource, TextureTag>&) override { return nullptr; }
    uint32_t GetDrawCallCount() const override { return 0u; }
    bool HasShadowResources() const override { return false; }
    const DefaultTextureSet& GetDefaultTextures() const override { return m_defaults; }

private:
    DefaultTextureSet m_defaults{};
};
