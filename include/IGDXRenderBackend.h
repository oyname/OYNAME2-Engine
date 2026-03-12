#pragma once

#include "Registry.h"
#include "ResourceStore.h"
#include "MeshAssetResource.h"
#include "MaterialResource.h"
#include "GDXShaderResource.h"
#include "GDXTextureResource.h"
#include "FrameData.h"
#include "RenderQueue.h"

#include <cstdint>
#include <string>

class IGDXRenderBackend
{
public:
    struct DefaultTextureSet
    {
        TextureHandle white;
        TextureHandle normal;
        TextureHandle orm;
        TextureHandle black;
    };

    virtual ~IGDXRenderBackend() = default;

    virtual bool Initialize(ResourceStore<GDXTextureResource, TextureTag>& texStore) = 0;
    virtual void BeginFrame(const float clearColor[4]) = 0;
    virtual void Present(bool vsync) = 0;
    virtual void Resize(int w, int h) = 0;
    virtual void Shutdown(ResourceStore<MaterialResource, MaterialTag>& matStore,
                          ResourceStore<GDXShaderResource, ShaderTag>& shaderStore,
                          ResourceStore<GDXTextureResource, TextureTag>& texStore) = 0;

    virtual ShaderHandle CreateShader(ResourceStore<GDXShaderResource, ShaderTag>& shaderStore,
                                      const std::wstring& vsFile,
                                      const std::wstring& psFile,
                                      uint32_t vertexFlags,
                                      const std::wstring& debugName) = 0;

    virtual TextureHandle CreateTexture(ResourceStore<GDXTextureResource, TextureTag>& texStore,
                                        const std::wstring& filePath,
                                        bool isSRGB,
                                        TextureHandle fallbackOnFailure) = 0;

    virtual bool UploadMesh(MeshAssetResource& mesh) = 0;
    virtual bool CreateMaterialGpu(MaterialResource& mat) = 0;

    virtual void UpdateLights(Registry& registry, FrameData& frame) = 0;
    virtual void UpdateFrameConstants(const FrameData& frame) = 0;

    virtual void ExecuteShadowPass(const RenderQueue& shadowQueue,
                                   ResourceStore<MeshAssetResource, MeshTag>& meshStore,
                                   ResourceStore<GDXShaderResource, ShaderTag>& shaderStore,
                                   const FrameData& frame) = 0;

    virtual void* ExecuteMainPass(const RenderQueue& opaqueQueue,
                                  ResourceStore<MeshAssetResource, MeshTag>& meshStore,
                                  ResourceStore<MaterialResource, MaterialTag>& matStore,
                                  ResourceStore<GDXShaderResource, ShaderTag>& shaderStore,
                                  ResourceStore<GDXTextureResource, TextureTag>& texStore) = 0;

    virtual uint32_t GetDrawCallCount() const = 0;
    virtual bool HasShadowResources() const = 0;
    virtual const DefaultTextureSet& GetDefaultTextures() const = 0;
};
