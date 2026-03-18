#pragma once

#include "IGDXRenderer.h"
#include "IGDXRenderBackend.h"
#include "Registry.h"
#include "Components.h"
#include "ResourceStore.h"
#include "MeshAssetResource.h"
#include "MaterialResource.h"
#include "GDXShaderResource.h"
#include "GDXTextureResource.h"
#include "GDXRenderTargetResource.h"
#include "PostProcessResource.h"
#include "GDXVertexFlags.h"
#include "FrameData.h"
#include "RenderQueue.h"
#include "TransformSystem.h"
#include "CameraSystem.h"
#include "RenderGatherSystem.h"
#include "ShaderVariant.h"
#include "ImageBuffer.h"
#include "FrameTransientResources.h"
#include "FrameContext.h"
#include "JobSystem.h"
#include "SystemScheduler.h"

#include <unordered_map>

#include <memory>
#include <string>
#include <functional>
#include <chrono>

class GDXECSRenderer final : public IGDXRenderer
{
public:
    explicit GDXECSRenderer(std::unique_ptr<IGDXRenderBackend> backend);
    ~GDXECSRenderer() override;

    bool Initialize() override;
    void BeginFrame() override;
    void EndFrame() override;
    void Resize(int w, int h) override;
    void Shutdown() override;

    using TickFn = std::function<void(float)>;
    void SetTickCallback(TickFn fn) { m_tickCallback = std::move(fn); }
    void Tick(float dt);

    Registry& GetRegistry() { return m_registry; }

    ShaderHandle   CreateShader(const std::wstring& vsFile,
        const std::wstring& psFile,
        uint32_t vertexFlags = GDX_VERTEX_DEFAULT);
    ShaderHandle   CreateShader(const std::wstring& vsFile,
        const std::wstring& psFile,
        uint32_t vertexFlags,
        const GDXShaderLayout& layout);

    TextureHandle  LoadTexture(const std::wstring& filePath, bool isSRGB = true);
    TextureHandle  CreateTexture(const ImageBuffer& image, const std::wstring& debugName, bool isSRGB = true);

    MeshHandle     UploadMesh(MeshAssetResource mesh);
    MaterialHandle CreateMaterial(MaterialResource mat);

    ShaderHandle   GetDefaultShader() const { return m_defaultShader; }
    void SetShadowMapSize(uint32_t size);

    void SetSceneAmbient(float r, float g, float b)
    {
        m_frameData.sceneAmbient = { r, g, b };
    }

    ResourceStore<MeshAssetResource, MeshTag>& GetMeshStore() { return m_meshStore; }
    ResourceStore<MaterialResource, MaterialTag>& GetMatStore() { return m_matStore; }
    ResourceStore<GDXShaderResource, ShaderTag>& GetShaderStore() { return m_shaderStore; }
    ResourceStore<GDXTextureResource, TextureTag>& GetTextureStore() { return m_texStore; }

    // Render-Target (Offscreen RTT)
    RenderTargetHandle CreateRenderTarget(uint32_t w, uint32_t h, const std::wstring& name,
                                          GDXTextureFormat colorFormat = GDXTextureFormat::RGBA8_UNORM);
    TextureHandle      GetRenderTargetTexture(RenderTargetHandle h);

    PostProcessHandle  CreatePostProcessPass(const PostProcessPassDesc& desc);
    bool               SetPostProcessConstants(PostProcessHandle h, const void* data, uint32_t size);
    bool               SetPostProcessEnabled(PostProcessHandle h, bool enabled);
    void               ClearPostProcessPasses();

    struct FrameStats
    {
        uint32_t drawCalls = 0u;
        uint32_t renderCommands = 0u;
        uint32_t lightCount = 0u;
    };
    const FrameStats& GetFrameStats() const { return m_stats; }
    void SetClearColor(float r, float g, float b, float a = 1.0f);

private:
    ShaderHandle LoadShaderInternal(const std::wstring& vsFile,
        const std::wstring& psFile,
        uint32_t vertexFlags,
        const std::wstring& debugName,
        const GDXShaderLayout* customLayout = nullptr);

    bool LoadDefaultShaders();

    ShaderVariantKey BuildVariantKey(RenderPass pass, const SubmeshData& submesh, const MaterialResource& mat) const;
    ShaderVariantKey NormalizeVariantKey(const ShaderVariantKey& key) const;
    ShaderHandle ResolveShaderVariant(RenderPass pass, const SubmeshData& submesh, const MaterialResource& mat);
    ShaderHandle CreateShaderVariant(const ShaderVariantKey& key);

    std::unique_ptr<IGDXRenderBackend> m_backend;

    Registry m_registry;

    ResourceStore<MeshAssetResource, MeshTag>     m_meshStore;
    ResourceStore<MaterialResource, MaterialTag> m_matStore;
    ResourceStore<GDXShaderResource, ShaderTag>   m_shaderStore;
    ResourceStore<GDXTextureResource, TextureTag>  m_texStore;
    ResourceStore<GDXRenderTargetResource, RenderTargetTag> m_rtStore;
    ResourceStore<PostProcessResource, PostProcessTag> m_postProcessStore;

    TransformSystem    m_transformSystem;
    CameraSystem       m_cameraSystem;
    RenderGatherSystem m_gatherSystem;

    RenderQueue m_opaqueQueue;
    RenderQueue m_transparentQueue;
    RenderQueue m_shadowQueue;
    FrameData   m_frameData;

    ShaderHandle m_defaultShader;
    ShaderHandle m_shadowShader;
    std::unordered_map<ShaderVariantKey, ShaderHandle, ShaderVariantKeyHash> m_shaderVariantCache;

    TextureHandle m_defaultWhiteTex;
    TextureHandle m_defaultNormalTex;
    TextureHandle m_defaultORMTex;
    TextureHandle m_defaultBlackTex;

    float      m_clearColor[4] = { 0.05f, 0.05f, 0.12f, 1.0f };
    FrameStats m_stats;
    bool       m_initialized = false;

    FrameContextRing m_frameContexts{};
    std::array<FrameTransientResources, GDXMaxFramesInFlight> m_frameTransients{};
    uint32_t m_currentFrameIndex = 0u;
    uint64_t m_frameNumber = 0ull;

    TickFn m_tickCallback;

    std::vector<PostProcessHandle> m_postProcessPassOrder;
    RenderTargetHandle m_mainScenePostProcessTarget = RenderTargetHandle::Invalid();

    JobSystem m_jobSystem;
    SystemScheduler m_systemScheduler;
};
