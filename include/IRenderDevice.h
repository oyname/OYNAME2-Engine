#pragma once

#include "ResourceStore.h"
#include "MaterialResource.h"
#include "GDXShaderResource.h"
#include "GDXTextureResource.h"
#include "GDXRenderTargetResource.h"
#include "ImageBuffer.h"
#include "GDXShaderContracts.h"
#include "PostProcessResource.h"

#include <cstdint>
#include <string>
#include <vector>

class IRenderDevice
{
public:
    struct DefaultTextureSet
    {
        TextureHandle white;
        TextureHandle normal;
        TextureHandle orm;
        TextureHandle black;
    };

    virtual ~IRenderDevice() = default;

    virtual bool Initialize(ResourceStore<GDXTextureResource, TextureTag>& texStore) = 0;
    virtual void BeginFrame(const float clearColor[4]) = 0;
    virtual void Present(bool vsync) = 0;
    virtual void Resize(int w, int h) = 0;
    virtual bool SetFullscreen(bool fullscreen) { (void)fullscreen; return false; }
    virtual bool IsFullscreen() const { return false; }
    virtual void Shutdown(
        ResourceStore<MaterialResource, MaterialTag>& matStore,
        ResourceStore<GDXShaderResource, ShaderTag>& shaderStore,
        ResourceStore<GDXTextureResource, TextureTag>& texStore) = 0;

    virtual ShaderHandle UploadShader(
        ResourceStore<GDXShaderResource, ShaderTag>& shaderStore,
        const ShaderSourceDesc& desc) = 0;

    virtual TextureHandle UploadTexture(
        ResourceStore<GDXTextureResource, TextureTag>& texStore,
        const std::wstring& filePath,
        bool isSRGB,
        TextureHandle fallbackOnFailure) = 0;

    virtual TextureHandle UploadTextureFromImage(
        ResourceStore<GDXTextureResource, TextureTag>& texStore,
        const ImageBuffer& image,
        bool isSRGB,
        const std::wstring& debugName,
        TextureHandle fallbackOnFailure) = 0;

    virtual PostProcessHandle CreatePostProcessPass(
        ResourceStore<PostProcessResource, PostProcessTag>& postStore,
        const PostProcessPassDesc& desc)
    {
        (void)postStore; (void)desc;
        return PostProcessHandle::Invalid();
    }

    virtual bool UpdatePostProcessConstants(
        PostProcessResource& pass, const void* data, uint32_t size)
    {
        (void)pass; (void)data; (void)size;
        return false;
    }

    virtual void DestroyPostProcessPasses(
        ResourceStore<PostProcessResource, PostProcessTag>& postStore)
    {
        (void)postStore;
    }

    virtual bool ExecutePostProcessChain(
        const std::vector<PostProcessHandle>& orderedPasses,
        const std::vector<PostProcessPassConstantOverride>* constantOverrides,
        ResourceStore<PostProcessResource, PostProcessTag>& postStore,
        ResourceStore<GDXTextureResource, TextureTag>& texStore,
        ResourceStore<GDXRenderTargetResource, RenderTargetTag>* rtStore,
        const PostProcessExecutionInputs& execInputs,
        float viewportWidth,
        float viewportHeight,
        RenderTargetHandle outputTarget = RenderTargetHandle::Invalid(),
        bool outputToBackbuffer = true)
    {
        (void)orderedPasses; (void)constantOverrides; (void)postStore; (void)texStore; (void)rtStore;
        (void)execInputs; (void)viewportWidth; (void)viewportHeight; (void)outputTarget; (void)outputToBackbuffer;
        return false;
    }

    virtual RenderTargetHandle CreateRenderTarget(
        ResourceStore<GDXRenderTargetResource, RenderTargetTag>& rtStore,
        ResourceStore<GDXTextureResource, TextureTag>& texStore,
        uint32_t width, uint32_t height,
        const std::wstring& debugName,
        GDXTextureFormat colorFormat = GDXTextureFormat::RGBA8_UNORM)
    {
        (void)rtStore; (void)texStore; (void)width; (void)height; (void)debugName; (void)colorFormat;
        return RenderTargetHandle::Invalid();
    }

    virtual void DestroyRenderTarget(
        RenderTargetHandle handle,
        ResourceStore<GDXRenderTargetResource, RenderTargetTag>& rtStore,
        ResourceStore<GDXTextureResource, TextureTag>& texStore)
    {
        (void)handle; (void)rtStore; (void)texStore;
    }

    virtual void ReleaseUnusedRenderTargetCaches(
        const std::vector<RenderTargetHandle>& activeRenderTargets)
    {
        (void)activeRenderTargets;
    }

    virtual void LoadIBL(const wchar_t* hdrPath) = 0;
    virtual bool SupportsTextureFormat(GDXTextureFormat format) const { (void)format; return true; }
    virtual void SetShadowMapSize(uint32_t size) { (void)size; }
    virtual uint32_t GetDrawCallCount() const = 0;
    virtual bool HasShadowResources() const = 0;
    virtual const DefaultTextureSet& GetDefaultTextures() const = 0;
};
