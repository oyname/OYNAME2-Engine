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
#include "PostProcessResource.h"
#include "BackendRenderPassDesc.h"
#include "ICommandList.h"

#include <cstdint>
#include <string>
#include <vector>

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

    virtual void* ExecuteRenderPass(const BackendRenderPassDesc& passDesc,
                                    Registry& registry,
                                    const ICommandList& commandList,
                                    ResourceStore<MeshAssetResource, MeshTag>& meshStore,
                                    ResourceStore<MaterialResource, MaterialTag>& matStore,
                                    ResourceStore<GDXShaderResource, ShaderTag>& shaderStore,
                                    ResourceStore<GDXTextureResource, TextureTag>& texStore,
                                    ResourceStore<GDXRenderTargetResource, RenderTargetTag>* rtStore = nullptr) = 0;


    virtual PostProcessHandle CreatePostProcessPass(ResourceStore<PostProcessResource, PostProcessTag>& postStore,
                                                    const PostProcessPassDesc& desc)
    {
        (void)postStore; (void)desc;
        return PostProcessHandle::Invalid();
    }

    virtual bool UpdatePostProcessConstants(PostProcessResource& pass, const void* data, uint32_t size)
    {
        (void)pass; (void)data; (void)size;
        return false;
    }


    virtual void DestroyPostProcessPasses(ResourceStore<PostProcessResource, PostProcessTag>& postStore)
    {
        (void)postStore;
    }

    virtual bool ExecutePostProcessChain(const std::vector<PostProcessHandle>& orderedPasses,
                                         ResourceStore<PostProcessResource, PostProcessTag>& postStore,
                                         ResourceStore<GDXTextureResource, TextureTag>& texStore,
                                         TextureHandle sceneTexture,
                                         float viewportWidth,
                                         float viewportHeight)
    {
        (void)orderedPasses; (void)postStore; (void)texStore;
        (void)sceneTexture; (void)viewportWidth; (void)viewportHeight;
        return false;
    }

    // IBL: HDR-Datei laden, Cubemaps backen und intern binden.
    // Muss nach Initialize() aufgerufen werden.
    // Wenn hdrPath leer oder Datei fehlt → neutraler Fallback.
    virtual void LoadIBL(const wchar_t* hdrPath) = 0;

    virtual uint32_t GetDrawCallCount() const = 0;
    virtual bool HasShadowResources() const = 0;
    virtual const DefaultTextureSet& GetDefaultTextures() const = 0;

    // Prüft ob die GPU das angegebene Format als Render Target und Shader Resource unterstützt.
    // Sollte vor CreateRenderTarget() aufgerufen werden wenn das Format optional ist (z.B. RGBA16_FLOAT für HDR).
    virtual bool SupportsTextureFormat(GDXTextureFormat format) const { return true; }

    virtual void SetShadowMapSize(uint32_t size)
    {
        (void)size;
    }

    virtual RenderTargetHandle CreateRenderTarget(
        ResourceStore<GDXRenderTargetResource, RenderTargetTag>& rtStore,
        ResourceStore<GDXTextureResource,      TextureTag>&      texStore,
        uint32_t width, uint32_t height,
        const std::wstring& debugName,
        GDXTextureFormat colorFormat = GDXTextureFormat::RGBA8_UNORM)
    {
        (void)rtStore; (void)texStore;
        (void)width;   (void)height; (void)debugName;
        return RenderTargetHandle::Invalid();
    }

    // Gibt native GPU-Ressourcen des RTs frei und entfernt exposedTexture aus texStore.
    // Sicher bei Invalid-Handle. Muss vor jedem Überschreiben/Shutdown aufgerufen werden.
    virtual void DestroyRenderTarget(
        RenderTargetHandle handle,
        ResourceStore<GDXRenderTargetResource, RenderTargetTag>& rtStore,
        ResourceStore<GDXTextureResource,      TextureTag>&      texStore)
    {
        (void)handle; (void)rtStore; (void)texStore;
    }
};
