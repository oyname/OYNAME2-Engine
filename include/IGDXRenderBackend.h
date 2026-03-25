#pragma once

#include "ECS/Registry.h"
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

// ---------------------------------------------------------------------------
// ShaderSourceDesc — backend-neutraler Shader-Deskriptor.
//
// sourceType bestimmt wie vertexCode/pixelCode interpretiert werden:
//   HlslFilePath   DX11: vertexCode/pixelCode = UTF-16 encoded Dateipfad
//   HlslSource     DX11: vertexCode/pixelCode = HLSL-Quelltext als UTF-8
//   SpirvBinary    Vulkan: kompiliertes SPIR-V
//   GlslSource     OpenGL: GLSL-Quelltext als UTF-8
//
// Das Frontend erzeugt den Desc, das Backend wertet sourceType aus.
// ---------------------------------------------------------------------------
enum class ShaderSourceType : uint8_t
{
    HlslFilePath  = 0,   // vertexCode/pixelCode = UTF-16 Dateipfad-Bytes
    HlslSource    = 1,   // vertexCode/pixelCode = HLSL UTF-8 Quelltext
    SpirvBinary   = 2,   // vertexCode/pixelCode = SPIR-V Binärdaten
    GlslSource    = 3,   // vertexCode/pixelCode = GLSL UTF-8 Quelltext
};



enum class GDXDebugSmokeTestMode : uint8_t
{
    None = 0,
    FullscreenTriangle = 1,
    PositionOnlyTriangle = 2,
    PositionColorTriangle = 3,
};
struct ShaderSourceDesc
{
    ShaderSourceType     sourceType   = ShaderSourceType::HlslFilePath;
    std::vector<uint8_t> vertexCode;   // Quelltext, Pfad oder Binary
    std::vector<uint8_t> pixelCode;
    GDXShaderLayout      layout;
    uint32_t             vertexFlags  = GDX_VERTEX_DEFAULT;
    std::wstring         debugName;

    // Hilfsmethode: Dateipfad-Desc aus wstring (DX11/HLSL Standardfall)
    static ShaderSourceDesc FromHlslFiles(
        const std::wstring& vsPath,
        const std::wstring& psPath,
        uint32_t            vertexFlags,
        const GDXShaderLayout& layout,
        const std::wstring& debugName = L"")
    {
        ShaderSourceDesc d;
        d.sourceType  = ShaderSourceType::HlslFilePath;
        d.vertexFlags = vertexFlags;
        d.layout      = layout;
        d.debugName   = debugName.empty() ? vsPath + L" / " + psPath : debugName;
        d.vertexCode.assign(
            reinterpret_cast<const uint8_t*>(vsPath.data()),
            reinterpret_cast<const uint8_t*>(vsPath.data() + vsPath.size()));
        d.pixelCode.assign(
            reinterpret_cast<const uint8_t*>(psPath.data()),
            reinterpret_cast<const uint8_t*>(psPath.data() + psPath.size()));
        return d;
    }

    std::wstring VertexFilePath() const
    {
        return std::wstring(
            reinterpret_cast<const wchar_t*>(vertexCode.data()),
            vertexCode.size() / sizeof(wchar_t));
    }

    std::wstring PixelFilePath() const
    {
        return std::wstring(
            reinterpret_cast<const wchar_t*>(pixelCode.data()),
            pixelCode.size() / sizeof(wchar_t));
    }
};

// ---------------------------------------------------------------------------
// IGDXRenderBackend — backend-neutrales Render-Interface.
//
// Keine DX-Typen, keine void*-Rückgaben, keine HLSL-Pfade.
// Alles DX11-Spezifische lebt in GDXDX11RenderBackend.
// ---------------------------------------------------------------------------
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

    // -- Lifecycle ----------------------------------------------------------
    virtual bool Initialize(ResourceStore<GDXTextureResource, TextureTag>& texStore) = 0;
    virtual void BeginFrame(const float clearColor[4]) = 0;
    virtual void Present(bool vsync) = 0;
    virtual void Resize(int w, int h) = 0;
    virtual void Shutdown(
        ResourceStore<MaterialResource,   MaterialTag>&   matStore,
        ResourceStore<GDXShaderResource,  ShaderTag>&     shaderStore,
        ResourceStore<GDXTextureResource, TextureTag>&    texStore) = 0;

    // -- Resource upload ----------------------------------------------------
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

    virtual bool UploadMesh(
        MeshHandle handle,
        MeshAssetResource& mesh) = 0;

    virtual bool UploadMaterial(
        MaterialHandle handle,
        MaterialResource& mat) = 0;

    // -- Frame constants ----------------------------------------------------
    virtual void ExtractLightData(Registry& registry, FrameData& frame) = 0;
    virtual void UploadLightConstants(const FrameData& frame) = 0;
    virtual void UpdateFrameConstants(const FrameData& frame) = 0;

    // -- Render pass execution ----------------------------------------------
    virtual void ExecuteRenderPass(
        const BackendRenderPassDesc& passDesc,
        Registry& registry,
        const ICommandList& opaqueList,
        const ICommandList& alphaList,
        ResourceStore<MeshAssetResource,           MeshTag>&          meshStore,
        ResourceStore<MaterialResource,            MaterialTag>&       matStore,
        ResourceStore<GDXShaderResource,           ShaderTag>&         shaderStore,
        ResourceStore<GDXTextureResource,          TextureTag>&        texStore,
        ResourceStore<GDXRenderTargetResource,     RenderTargetTag>&   rtStore) = 0;

    virtual void ExecuteShadowPass(
        const BackendRenderPassDesc& passDesc,
        Registry& registry,
        const ICommandList& commandList,
        ResourceStore<MeshAssetResource,           MeshTag>&          meshStore,
        ResourceStore<MaterialResource,            MaterialTag>&       matStore,
        ResourceStore<GDXShaderResource,           ShaderTag>&         shaderStore,
        ResourceStore<GDXTextureResource,          TextureTag>&        texStore) = 0;

    // -- Post processing ----------------------------------------------------
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
        ResourceStore<PostProcessResource,        PostProcessTag>&    postStore,
        ResourceStore<GDXTextureResource,         TextureTag>&        texStore,
        ResourceStore<GDXRenderTargetResource,    RenderTargetTag>*   rtStore,
        const PostProcessExecutionInputs& execInputs,
        float viewportWidth,
        float viewportHeight,
        RenderTargetHandle outputTarget   = RenderTargetHandle::Invalid(),
        bool               outputToBackbuffer = true)
    {
        (void)orderedPasses; (void)postStore; (void)texStore; (void)rtStore;
        (void)execInputs;    (void)viewportWidth; (void)viewportHeight;
        (void)outputTarget;  (void)outputToBackbuffer;
        return false;
    }

    // -- Render targets -----------------------------------------------------
    virtual RenderTargetHandle CreateRenderTarget(
        ResourceStore<GDXRenderTargetResource, RenderTargetTag>& rtStore,
        ResourceStore<GDXTextureResource,      TextureTag>&      texStore,
        uint32_t width, uint32_t height,
        const std::wstring& debugName,
        GDXTextureFormat colorFormat = GDXTextureFormat::RGBA8_UNORM)
    {
        (void)rtStore; (void)texStore; (void)width; (void)height;
        (void)debugName; (void)colorFormat;
        return RenderTargetHandle::Invalid();
    }

    virtual void DestroyRenderTarget(
        RenderTargetHandle handle,
        ResourceStore<GDXRenderTargetResource, RenderTargetTag>& rtStore,
        ResourceStore<GDXTextureResource,      TextureTag>&      texStore)
    {
        (void)handle; (void)rtStore; (void)texStore;
    }


    virtual void ReleaseUnusedRenderTargetCaches(
        const std::vector<RenderTargetHandle>& activeRenderTargets)
    {
        (void)activeRenderTargets;
    }

    // -- IBL / capabilities -------------------------------------------------
    virtual void LoadIBL(const wchar_t* hdrPath) = 0;

    virtual bool SupportsTextureFormat(GDXTextureFormat format) const { (void)format; return true; }
    virtual void SetShadowMapSize(uint32_t size) { (void)size; }
    virtual void SetDebugSmokeTestMode(GDXDebugSmokeTestMode mode) { (void)mode; }

    virtual uint32_t GetDrawCallCount() const = 0;
    virtual bool HasShadowResources() const = 0;
    virtual const DefaultTextureSet& GetDefaultTextures() const = 0;
};
