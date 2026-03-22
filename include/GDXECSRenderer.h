#pragma once

#include "IGDXRenderer.h"
#include "IGDXRenderBackend.h"
#include "ECS/Registry.h"
#include "Components.h"
#include "RenderComponents.h"
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
#include "ECS/TransformSystem.h"
#include "CameraSystem.h"
#include "RenderGatherSystem.h"
#include "ViewCullingSystem.h"
#include "RenderViewData.h"
#include "ShaderVariant.h"
#include "ImageBuffer.h"
#include "FrameTransientResources.h"
#include "FrameContext.h"
#include "Core/JobSystem.h"
#include "SystemScheduler.h"
#include "RenderFramePipeline.h"
#include "GDXRenderFrameGraph.h"
#include "GDXShaderVariantCache.h"
#include "GDXDebugCullingRenderer.h"
#include "DebugCamera.h"
#include "RenderViewPrep.h"
#include "RenderPassBuilder.h"
#include "CullGatherSystem.h"

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

    // IBL: HDR-Panorama laden und Irradiance/Prefiltered/BRDF-LUT backen.
    void           LoadIBL(const std::wstring& hdrPath);
    TextureHandle  CreateTexture(const ImageBuffer& image, const std::wstring& debugName, bool isSRGB = true);

    MeshHandle     UploadMesh(MeshAssetResource mesh);
    MeshHandle     UploadMesh(MeshAssetResource mesh, RenderBoundsComponent& outBounds);
    MaterialHandle CreateMaterial(MaterialResource mat);

    ShaderHandle   GetDefaultShader() const { return m_defaultShader; }
    void SetShadowMapSize(uint32_t size);
    bool SupportsTextureFormat(GDXTextureFormat format) const;

    void SetSceneAmbient(float r, float g, float b)
    {
        m_persistentFrameState.sceneAmbient = { r, g, b };
        m_frameData.sceneAmbient = { r, g, b };
    }

    ResourceStore<MeshAssetResource, MeshTag>&    GetMeshStore()    { return m_meshStore; }
    ResourceStore<MaterialResource, MaterialTag>& GetMatStore()     { return m_matStore; }
    ResourceStore<GDXShaderResource, ShaderTag>&  GetShaderStore()  { return m_shaderStore; }
    ResourceStore<GDXTextureResource, TextureTag>& GetTextureStore() { return m_texStore; }

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
        ViewCullingStats mainCulling{};
        ViewCullingStats shadowCulling{};
        uint32_t rttViewCount = 0u;
        uint32_t debugBoundsDraws = 0u;
        uint32_t debugFrustumDraws = 0u;
    };

    struct RendererPersistentFrameState
    {
        GIDX::Float3 sceneAmbient = { 0.08f, 0.08f, 0.12f };
        float viewportWidth  = 1280.0f;
        float viewportHeight = 720.0f;

        void ApplyTo(FrameData& frame) const
        {
            frame.sceneAmbient   = sceneAmbient;
            frame.viewportWidth  = viewportWidth;
            frame.viewportHeight = viewportHeight;
        }
    };

    const FrameStats& GetFrameStats() const { return m_stats; }
    void SetClearColor(float r, float g, float b, float a = 1.0f);

    using DebugCullingOptions = GDXDebugCullingRenderer::Options;
    void SetDebugCullingOptions(const DebugCullingOptions& options) { m_debugCulling.options = options; }
    const DebugCullingOptions& GetDebugCullingOptions() const { return m_debugCulling.options; }

    DebugCamera& GetDebugCamera() { return m_debugCamera; }

private:
    enum class RenderFramePhase : uint8_t
    {
        Idle          = 0,
        UpdateWrite   = 1,
        FreezeSnapshot = 2,
        VisibilityBuild = 3,
        ExecuteSubmit  = 4,
    };

    bool LoadDefaultShaders();
    ShaderHandle LoadShaderInternal(const std::wstring& vsFile,
        const std::wstring& psFile,
        uint32_t vertexFlags,
        const std::wstring& debugName,
        const GDXShaderLayout* customLayout = nullptr);
    ShaderHandle ResolveShaderVariant(RenderPass pass, const SubmeshData& submesh, const MaterialResource& mat);

    // Scene extraction
    void CaptureFrameSnapshot(FrameData& outFrame);

    // Cull + Gather — delegated to CullGather free functions via context
    CullGather::Context MakeCullGatherContext(const RenderGatherSystem::ShaderResolver& rs);

    // FrameGraph build — named method, no longer inline in EndFrame
    void BuildFrameGraph();

    // Queue finalization — delegates to CullGather::FinalizeFrameQueues
    void FinalizeFrameQueues();

    // Debug culling — uses m_debugCulling (owned state)
    bool EnsureDebugCullingResources();
    void AppendDebugVisibleSet(RenderQueue& queue, const VisibleSet& set,
                               const RenderViewData& view, RFG::ViewStats* viewStats = nullptr);
    void LogDebugCullingStats() const;

    // Stats
    void AggregatePreparedFrameStats(const RFG::ViewPassData& mainView,
                                      const std::vector<RFG::ViewPassData>& rttViews);
    void UpdatePreparedMainViewFrameTransient(RFG::ViewPassData& preparedView);

    // ---------------------------------------------------------------------------
    // Context builders — return filled contexts for free functions
    // ---------------------------------------------------------------------------
    RenderViewPrep::Context  MakeViewPrepContext()  const;
    RenderPassBuilder::PostProcContext MakePostProcContext();

    // ---------------------------------------------------------------------------
    // Owned state
    // ---------------------------------------------------------------------------
    std::unique_ptr<IGDXRenderBackend> m_backend;

    Registry m_registry;

    ResourceStore<MeshAssetResource,   MeshTag>          m_meshStore;
    ResourceStore<MaterialResource,    MaterialTag>       m_matStore;
    ResourceStore<GDXShaderResource,   ShaderTag>         m_shaderStore;
    ResourceStore<GDXTextureResource,  TextureTag>        m_texStore;
    ResourceStore<GDXRenderTargetResource, RenderTargetTag> m_rtStore;
    ResourceStore<PostProcessResource, PostProcessTag>    m_postProcessStore;

    TransformSystem    m_transformSystem;
    CameraSystem       m_cameraSystem;
    ViewCullingSystem  m_viewCullingSystem;
    RenderGatherSystem m_gatherSystem;

    RFG::PipelineData            m_renderPipeline;
    RendererPersistentFrameState m_persistentFrameState{};
    FrameData                    m_frameData;

    ShaderHandle        m_defaultShader;
    ShaderHandle        m_shadowShader;
    GDXShaderVariantCache m_shaderCache;

    TextureHandle m_defaultWhiteTex;
    TextureHandle m_defaultNormalTex;
    TextureHandle m_defaultORMTex;
    TextureHandle m_defaultBlackTex;

    float      m_clearColor[4] = { 0.05f, 0.05f, 0.12f, 1.0f };
    FrameStats m_stats;
    GDXDebugCullingRenderer m_debugCulling;
    DebugCamera             m_debugCamera;
    bool       m_initialized = false;
    bool       m_shadowResourcesAvailable = false;

    FrameContextRing m_frameContexts{};
    std::array<FrameTransientResources, GDXMaxFramesInFlight> m_frameTransients{};
    uint32_t m_currentFrameIndex = 0u;
    uint64_t m_frameNumber       = 0ull;

    TickFn m_tickCallback;

    std::vector<PostProcessHandle> m_postProcessPassOrder;
    RenderTargetHandle m_mainScenePostProcessTarget = RenderTargetHandle::Invalid();

    JobSystem        m_jobSystem;
    SystemScheduler  m_systemScheduler;
    RenderFramePhase m_framePhase = RenderFramePhase::Idle;

    GDXRenderFrameGraph m_frameGraph;
};
