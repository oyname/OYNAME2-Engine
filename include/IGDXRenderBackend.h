#pragma once

#include "Registry.h"
#include "ResourceStore.h"
#include "MeshAssetResource.h"
#include "MaterialResource.h"
#include "GDXShaderResource.h"
#include "GDXTextureResource.h"
#include "GDXShaderLayout.h"
#include "GDXRenderTargetResource.h"
#include "ImageBuffer.h"
#include "FrameData.h"
#include "RenderQueue.h"
#include "RenderPassClearDesc.h"
#include "RenderPassTargetDesc.h"

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
                                      const GDXShaderLayout& layout,
                                      const std::wstring& debugName) = 0;

    virtual TextureHandle CreateTexture(ResourceStore<GDXTextureResource, TextureTag>& texStore,
                                        const std::wstring& filePath,
                                        bool isSRGB,
                                        TextureHandle fallbackOnFailure) = 0;

    virtual TextureHandle CreateTextureFromImage(ResourceStore<GDXTextureResource, TextureTag>& texStore,
                                                 const ImageBuffer& image,
                                                 bool isSRGB,
                                                 const std::wstring& debugName,
                                                 TextureHandle fallbackOnFailure) = 0;

    virtual bool UploadMesh(MeshAssetResource& mesh) = 0;
    virtual bool CreateMaterialGpu(MaterialResource& mat) = 0;

    virtual void UpdateLights(Registry& registry, FrameData& frame) = 0;
    virtual void UpdateFrameConstants(const FrameData& frame) = 0;

    virtual void ExecuteShadowPass(Registry& registry,
                                   const RenderQueue& shadowQueue,
                                   ResourceStore<MeshAssetResource, MeshTag>& meshStore,
                                   ResourceStore<MaterialResource, MaterialTag>& matStore,
                                   ResourceStore<GDXShaderResource, ShaderTag>& shaderStore,
                                   ResourceStore<GDXTextureResource, TextureTag>& texStore,
                                   const FrameData& frame) = 0;

    virtual void* ExecuteMainPass(Registry& registry,
                                  const RenderQueue& opaqueQueue,
                                  ResourceStore<MeshAssetResource, MeshTag>& meshStore,
                                  ResourceStore<MaterialResource, MaterialTag>& matStore,
                                  ResourceStore<GDXShaderResource, ShaderTag>& shaderStore,
                                  ResourceStore<GDXTextureResource, TextureTag>& texStore) = 0;

    virtual void* ExecuteMainPassToTarget(GDXRenderTargetResource& rt,
                                          const RenderPassClearDesc& clearDesc,
                                          Registry& registry,
                                          const RenderQueue& opaqueQueue,
                                          ResourceStore<MeshAssetResource, MeshTag>& meshStore,
                                          ResourceStore<MaterialResource, MaterialTag>& matStore,
                                          ResourceStore<GDXShaderResource, ShaderTag>& shaderStore,
                                          ResourceStore<GDXTextureResource, TextureTag>& texStore)
    {
        (void)rt;
        (void)clearDesc;
        return ExecuteMainPass(registry, opaqueQueue, meshStore, matStore, shaderStore, texStore);
    }

    virtual void* ExecutePass(const RenderPassTargetDesc& targetDesc,
                              Registry& registry,
                              const RenderQueue& queue,
                              ResourceStore<MeshAssetResource, MeshTag>& meshStore,
                              ResourceStore<MaterialResource, MaterialTag>& matStore,
                              ResourceStore<GDXShaderResource, ShaderTag>& shaderStore,
                              ResourceStore<GDXTextureResource, TextureTag>& texStore,
                              ResourceStore<GDXRenderTargetResource, RenderTargetTag>* rtStore = nullptr)
    {
        if (targetDesc.useBackbuffer || !targetDesc.renderTarget.IsValid() || !rtStore)
            return ExecuteMainPass(registry, queue, meshStore, matStore, shaderStore, texStore);

        GDXRenderTargetResource* rt = rtStore->Get(targetDesc.renderTarget);
        if (!rt)
            return ExecuteMainPass(registry, queue, meshStore, matStore, shaderStore, texStore);

        return ExecuteMainPassToTarget(*rt, targetDesc.clear, registry, queue, meshStore, matStore, shaderStore, texStore);
    }


    virtual uint32_t GetDrawCallCount() const = 0;
    virtual bool HasShadowResources() const = 0;
    virtual const DefaultTextureSet& GetDefaultTextures() const = 0;

    // Offscreen Render-Target anlegen.
    // Default-Implementierung gibt ungültige Handles zurück (Backends ohne RTT-Support).
    virtual RenderTargetHandle CreateRenderTarget(
        ResourceStore<GDXRenderTargetResource, RenderTargetTag>& rtStore,
        ResourceStore<GDXTextureResource,      TextureTag>&      texStore,
        uint32_t width, uint32_t height,
        const std::wstring& debugName)
    {
        (void)rtStore; (void)texStore;
        (void)width;   (void)height; (void)debugName;
        return RenderTargetHandle::Invalid();
    }
};
