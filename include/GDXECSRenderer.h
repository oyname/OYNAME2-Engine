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
#include "ViewCullingSystem.h"
#include "RenderViewData.h"
#include "ShaderVariant.h"
#include "ImageBuffer.h"
#include "FrameTransientResources.h"
#include "FrameContext.h"
#include "JobSystem.h"
#include "SystemScheduler.h"
#include "RenderFramePipeline.h"
#include "GDXRenderFrameGraph.h"
#include "GDXShaderVariantCache.h"
#include "GDXDebugCullingRenderer.h"

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
    // Einmaliger Aufruf nach Initialize(). Pfad leer oder fehlend → Fallback.
    void           LoadIBL(const std::wstring& hdrPath);
    TextureHandle  CreateTexture(const ImageBuffer& image, const std::wstring& debugName, bool isSRGB = true);

    MeshHandle     UploadMesh(MeshAssetResource mesh);

    // Lädt Mesh hoch und berechnet gleichzeitig korrekte RenderBoundsComponent.
    // Lösung für localCenter={0,0,0}-Problem bei Meshes die nicht am Ursprung liegen.
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
        ViewCullingStats mainCulling{};
        ViewCullingStats shadowCulling{};
        uint32_t rttViewCount = 0u;
        uint32_t debugBoundsDraws = 0u;
        uint32_t debugFrustumDraws = 0u;
    };

    struct RendererPersistentFrameState
    {
        GIDX::Float3 sceneAmbient = { 0.08f, 0.08f, 0.12f };
        float viewportWidth = 1280.0f;
        float viewportHeight = 720.0f;

        void ApplyTo(FrameData& frame) const
        {
            frame.sceneAmbient = sceneAmbient;
            frame.viewportWidth = viewportWidth;
            frame.viewportHeight = viewportHeight;
        }
    };

    const FrameStats& GetFrameStats() const { return m_stats; }
    void SetClearColor(float r, float g, float b, float a = 1.0f);
    // Rückwärtskompatibilität — App-Code nutzt GDXECSRenderer::DebugCullingOptions.
    using DebugCullingOptions = GDXDebugCullingRenderer::Options;

    void SetDebugCullingOptions(const DebugCullingOptions& options) { m_debugCulling.options = options; }
    const DebugCullingOptions& GetDebugCullingOptions() const { return m_debugCulling.options; }

private:
    enum class RenderFramePhase : uint8_t
    {
        Idle = 0,
        UpdateWrite = 1,
        FreezeSnapshot = 2,
        VisibilityBuild = 3,
        ExecuteSubmit = 4,
    };

    ShaderHandle LoadShaderInternal(const std::wstring& vsFile,
        const std::wstring& psFile,
        uint32_t vertexFlags,
        const std::wstring& debugName,
        const GDXShaderLayout* customLayout = nullptr);

    bool LoadDefaultShaders();

    void LogDebugCullingStats() const;
    bool EnsureDebugCullingResources();
    void AppendDebugVisibleSet(RenderQueue& queue, const VisibleSet& set,
                               const RenderViewData& view, RFG::ViewStats* viewStats = nullptr);
    ShaderHandle ResolveShaderVariant(RenderPass pass, const SubmeshData& submesh, const MaterialResource& mat);
    void AggregatePreparedFrameStats(const RFG::ViewPassData& mainView, const std::vector<RFG::ViewPassData>& rttViews);

    void CaptureFrameSnapshot(FrameData& outFrame);
    void PrepareMainViewData(const FrameData& frameSnapshot, RFG::ViewPassData& outView);
    void PrepareRenderTargetViewData(const FrameData& frameSnapshot, std::vector<RFG::ViewPassData>& outViews);

    // Single-view core — used by both Main and RTT paths.
    // js = nullptr → serial (RTT inner loop), js = &m_jobSystem → parallel (Main view).
    void CullViewGraphics(RFG::ViewPassData& view, JobSystem* js);
    void CullViewShadow(RFG::ViewPassData& view, JobSystem* js);
    void GatherViewGraphics(const RenderGatherSystem::ShaderResolver& rs, RFG::ViewPassData& view, JobSystem* js);
    void GatherViewShadow(const RenderGatherSystem::ShaderResolver& rs, RFG::ViewPassData& view, JobSystem* js);

    // RTT loops — iterate over views, dispatch single-view core in parallel.
    void CullPreparedRenderTargetViews(std::vector<RFG::ViewPassData>& views);
    void GatherPreparedRenderTargetViews(const RenderGatherSystem::ShaderResolver& rs, std::vector<RFG::ViewPassData>& views);
    void FinalizePreparedRenderTargetQueues(std::vector<RFG::ViewPassData>& views);
    void BuildPreparedRenderTargetExecuteInputs(std::vector<RFG::ViewPassData>& views);

    // Main view — single call, uses &m_jobSystem for inner parallelism.
    void CullPreparedMainView(RFG::ViewPassData& view);
    void GatherPreparedMainView(const RenderGatherSystem::ShaderResolver& rs, RFG::ViewPassData& view);
    void BuildPreparedMainViewExecuteInputs(RFG::ViewPassData& view);

    void FinalizePreparedFrameQueues(RFG::PipelineData& pipeline);
    void BuildPreparedFrameExecuteInputs(RFG::PipelineData& pipeline);
    void UpdatePreparedMainViewFrameTransient(RFG::ViewPassData& preparedView);

    void FinalizePreparedViewQueues(RFG::ViewPassData& preparedView);
    void ConfigurePreparedCommonExecuteInputs(RFG::ViewPassData& preparedView, bool presentAfterExecute);
    bool PrepareMainViewPostProcessPresentation(RFG::ViewPassData& preparedView);
    void BuildPreparedShadowPassExecuteInput(RFG::ViewPassData& preparedView);
    void BuildPreparedGraphicsPassExecuteInput(RFG::ViewPassData& preparedView, const RenderPassTargetDesc& targetDesc, bool appendGraphicsVisibleSet, bool appendShadowVisibleSet);
    void BuildPreparedExecutionQueues(RFG::ViewPassData& preparedView);

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
    ViewCullingSystem  m_viewCullingSystem;
    RenderGatherSystem m_gatherSystem;

    RFG::PipelineData m_renderPipeline;
    RendererPersistentFrameState m_persistentFrameState{};
    FrameData   m_frameData;

    ShaderHandle m_defaultShader;  // cached from m_shaderCache for quick access
    ShaderHandle m_shadowShader;
    GDXShaderVariantCache m_shaderCache;

    TextureHandle m_defaultWhiteTex;
    TextureHandle m_defaultNormalTex;
    TextureHandle m_defaultORMTex;
    TextureHandle m_defaultBlackTex;

    float      m_clearColor[4] = { 0.05f, 0.05f, 0.12f, 1.0f };
    FrameStats m_stats;
    GDXDebugCullingRenderer m_debugCulling;
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
    RenderFramePhase m_framePhase = RenderFramePhase::Idle;

    GDXRenderFrameGraph m_frameGraph;
};
