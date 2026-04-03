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
#include "ECS/ECSTypes.h"

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

// Forward declarations — vollständige Definition in RenderViewData.h
struct VisibleRenderCandidate;

#include "GDXShaderContracts.h"

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
    virtual bool SetFullscreen(bool fullscreen) { (void)fullscreen; return false; }
    virtual bool IsFullscreen() const { return false; }
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
        const std::vector<BackendPlannedTransition>& beginTransitions,
        const std::vector<BackendPlannedTransition>& endTransitions,
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
        const std::vector<BackendPlannedTransition>& beginTransitions,
        const std::vector<BackendPlannedTransition>& endTransitions,
        Registry& registry,
        const ICommandList& commandList,
        ResourceStore<MeshAssetResource,           MeshTag>&          meshStore,
        ResourceStore<MaterialResource,            MaterialTag>&       matStore,
        ResourceStore<GDXShaderResource,           ShaderTag>&         shaderStore,
        ResourceStore<GDXTextureResource,          TextureTag>&        texStore) = 0;

    // -- Particles ---------------------------------------------------------
    // Init GPU-side renderer only. The particle system is user-owned;
    // the engine holds a non-owning pointer via GDXECSRenderer::SetParticleSystem().
    virtual bool InitParticleRenderer(TextureHandle atlasTexture)
    {
        (void)atlasTexture;
        return false;
    }


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
        const std::vector<PostProcessPassConstantOverride>* constantOverrides,
        ResourceStore<PostProcessResource,        PostProcessTag>&    postStore,
        ResourceStore<GDXTextureResource,         TextureTag>&        texStore,
        ResourceStore<GDXRenderTargetResource,    RenderTargetTag>*   rtStore,
        const PostProcessExecutionInputs& execInputs,
        float viewportWidth,
        float viewportHeight,
        RenderTargetHandle outputTarget   = RenderTargetHandle::Invalid(),
        bool               outputToBackbuffer = true)
    {
        (void)orderedPasses; (void)constantOverrides; (void)postStore; (void)texStore; (void)rtStore;
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

    // -- Occlusion Culling --------------------------------------------------
    // Gibt false zurück wenn das Backend keine Occlusion Queries implementiert.
    virtual bool SupportsOcclusionCulling() const { return false; }

    // Schickt Occlusion Queries für alle Kandidaten ab (AABB gegen Depth Buffer).
    // Wird nach dem Haupt-Render-Pass aufgerufen.
    // results: Entity-Liste die im nächsten Frame sichtbar war (One-Frame-Delay).
    virtual void SubmitOcclusionQueries(
        const std::vector<VisibleRenderCandidate>& candidates,
        ResourceStore<MeshAssetResource, MeshTag>& meshStore,
        const FrameData& frame,
        RenderTargetHandle depthSourceTarget = RenderTargetHandle::Invalid())
    {
        (void)candidates; (void)meshStore; (void)frame; (void)depthSourceTarget;
    }

    // Liest Ergebnisse vom letzten Frame. Gibt Menge der sichtbaren EntityIDs zurück.
    // Aufruf am Anfang des nächsten Frames vor BuildVisibleSet.
    virtual void CollectOcclusionResults(std::unordered_set<EntityID>& outVisible,
                                         std::unordered_set<EntityID>* outTested = nullptr)
    {
        (void)outVisible;
        (void)outTested;
    }

    virtual uint32_t GetDrawCallCount() const = 0;
    virtual bool HasShadowResources() const = 0;
    virtual const DefaultTextureSet& GetDefaultTextures() const = 0;
};
