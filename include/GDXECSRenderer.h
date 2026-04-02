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
#include "FrameDispatch.h"
#include "Particles/GDXParticleEmitterSystem.h"  // GDXParticleSystem included transitively
// GDXParticleSystem is user-owned; engine holds a non-owning pointer.

#include <unordered_map>
#include <unordered_set>
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
    bool SetFullscreen(bool fullscreen);
    bool IsFullscreen() const;
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

    GDXShaderVariantCache&       GetShaderCache()       { return m_shaderCache; }
    const GDXShaderVariantCache& GetShaderCache() const { return m_shaderCache; }

    // Konfiguriert welche Shader-Dateien + Flags als Engine-Default verwendet werden.
    // Muss vor Initialize() aufgerufen werden.
    void SetShaderConfig(const ShaderPathConfig& config) { m_shaderCache.SetConfig(config); }

    bool SupportsOcclusionCulling() const;

    // -- Particles ---------------------------------------------------------
    // GPU-side renderer init — call once after Engine::Graphics().
    // The particle system itself is user-owned and passed via SetParticleSystem().
    bool InitParticleRenderer(TextureHandle atlasTexture);

    // Register a user-owned GDXParticleSystem with the engine.
    // The engine stores a non-owning pointer; lifetime is the caller's responsibility.
    // Call with nullptr to detach.
    void SetParticleSystem(GDXParticleSystem* ps);

    void SetOcclusionCulling(bool enabled);
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

    PostProcessHandle  CreatePostProcessPass(const PostProcessPassDesc& desc,
                                             PostProcessInsert position = PostProcessInsert::End);
    bool               SetPostProcessConstants(PostProcessHandle h, const void* data, uint32_t size);
    bool               SetPostProcessEnabled(PostProcessHandle h, bool enabled);
    // Setzt eine Textur für einen Custom-Slot eines Passes (Stufe E).
    // slotName muss dem PostProcessInputSlotDesc::name des deklarierten Slots entsprechen.
    bool               SetPostProcessCustomInput(PostProcessHandle h,
                                                 const std::wstring& slotName,
                                                 TextureHandle texture);
    void               ClearPostProcessPasses();

    // Bloom — wraps Bright + BlurH + BlurV + Composite passes.
    // Call once after engine.Initialize(). On resize call again with the new size.
    // Requires RGBA16_FLOAT — check SupportsTextureFormat() before calling.
    void               SetBloom(int viewportW, int viewportH,
                                float threshold = 1.0f,
                                float intensity = 2.0f,
                                float strength  = 0.1f,
                                float tintR = 1.0f, float tintG = 1.0f, float tintB = 1.0f);
    void               DisableBloom();

    // Tone mapping — wraps the PostProcessToneMappingPS.hlsl pass.
    // Call once (or whenever parameters change); creates the pass on first call.
    // SetToneMapping(None) disables without destroying (re-enabling is cheap).
    void               SetToneMapping(ToneMappingMode mode,
                                      float exposure = 1.0f,
                                      float gamma    = 2.2f);
    void               DisableToneMapping();

    // FXAA — wraps PostProcessFXAAPS.hlsl.
    // Call once or on resize; pass current viewport size.
    // FXAA should be the last pass before tone mapping (or the very last if no TM).
    void               SetFXAA(int viewportW, int viewportH,
                                float contrastThreshold = 0.0312f,
                                float relativeThreshold = 0.125f);
    void               DisableFXAA();

    // Minimaler Test-Pass: visualisiert SceneDepth als Graustufenbild.
    // Dient nur zum Nachweis, dass die Depth-SRV im Post-Processing ankommt.
    void               SetDepthDebugView(bool enabled);
    void               SetNormalDebugView(bool enabled);
    void               SetEdgeDebugView(bool enabled,
                                        int viewportW, int viewportH,
                                        float depthScale = 250.0f,
                                        float normalScale = 4.0f,
                                        bool depthOnly = false,
                                        bool normalOnly = false);
    void               SetGTAO(int viewportW, int viewportH,
                               float nearPlane, float farPlane,
                               float radiusPixels = 18.0f,
                               float thickness    = 1.5f,
                               float intensity    = 1.0f,
                               float power        = 1.5f);
    void               DisableGTAO();
    void               SetFog(const FogSettings& settings);
    void               DisableFog();
    void               SetDepthFogTest(bool enabled);
    void               SetVolumetricFog(const VolumetricFogSettings& settings);
    void               DisableVolumetricFog();

private:
    bool               EnsureGTAOPassesCreated();
    void               PrewarmPostProcessShaders();

public:

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
        Float3 sceneAmbient = { 0.08f, 0.08f, 0.12f };
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

    FreeCamera& GetFreeCamera() { return m_freeCamera; }

private:
    enum class RenderFramePhase : uint8_t
    {
        Idle          = 0,
        UpdateWrite   = 1,
        FreezeSnapshot = 2,
        VisibilityBuild = 3,
        ExecuteSubmit  = 4,
    };

    ShaderHandle LoadShaderInternal(const std::wstring& vsFile,
        const std::wstring& psFile,
        uint32_t vertexFlags,
        const std::wstring& debugName,
        const GDXShaderLayout* customLayout = nullptr);
    ShaderHandle ResolveShaderVariant(RenderPass pass, const SubmeshData& submesh, const MaterialResource& mat);

    // Scene extraction
    void CaptureFrameSnapshot(FrameData& outFrame);

    // Fills m_frameDispatch with all per-frame contexts.
    // Must be called at the start of EndFrame before scheduling tasks.
    void FillFrameDispatch(const RenderGatherSystem::ShaderResolver& rs);

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
    // Context builders — used internally by FillFrameDispatch
    // ---------------------------------------------------------------------------
    RenderViewPrep::Context            MakeViewPrepContext()   const;
    RenderPassBuilder::PostProcContext MakePostProcContext();
    CullGather::Context                MakeCullGatherContext(const RenderGatherSystem::ShaderResolver& rs);

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
    FreeCamera              m_freeCamera;
    bool       m_initialized = false;
    bool       m_shadowResourcesAvailable = false;
    bool       m_occlusionCullingEnabled = false;
    std::unordered_set<EntityID> m_occlusionVisible;

    FrameContextRing m_frameContexts{};
    std::array<FrameTransientResources, GDXMaxFramesInFlight> m_frameTransients{};
    uint32_t m_currentFrameIndex = 0u;
    uint64_t m_frameNumber       = 0ull;

    TickFn m_tickCallback;
    float  m_lastDeltaTime = 0.0f;
    bool   m_particlesRenderReady = false;
    GDXParticleSystem*       m_particleSystemPtr     = nullptr;  // user-owned, non-owning
    GDXParticleEmitterSystem m_particleEmitterSystem;

    std::vector<PostProcessHandle> m_postProcessPassOrder;
    RenderTargetHandle m_mainScenePostProcessTarget = RenderTargetHandle::Invalid();
    std::unordered_map<RenderTargetHandle, RenderTargetHandle> m_rttPostProcessTargets;
    PostProcessHandle  m_toneMappingPass            = PostProcessHandle::Invalid();
    ToneMappingMode    m_toneMappingMode             = ToneMappingMode::None;
    PostProcessHandle  m_fxaaPass                   = PostProcessHandle::Invalid();
    PostProcessHandle  m_bloomBrightPass             = PostProcessHandle::Invalid();
    PostProcessHandle  m_bloomBlurHPass              = PostProcessHandle::Invalid();
    PostProcessHandle  m_bloomBlurVPass              = PostProcessHandle::Invalid();
    PostProcessHandle  m_bloomCompositePass          = PostProcessHandle::Invalid();
    PostProcessHandle  m_depthDebugPass              = PostProcessHandle::Invalid();
    PostProcessHandle  m_normalDebugPass             = PostProcessHandle::Invalid();
    PostProcessHandle  m_edgeDebugPass               = PostProcessHandle::Invalid();
    PostProcessHandle  m_gtaoPass                    = PostProcessHandle::Invalid();
    PostProcessHandle  m_gtaoBlurPass                = PostProcessHandle::Invalid();
    PostProcessHandle  m_gtaoCompositePass           = PostProcessHandle::Invalid();
    PostProcessHandle  m_fogPass                     = PostProcessHandle::Invalid();
    FogSettings        m_fogSettings{};
    PostProcessHandle  m_volumetricFogPass           = PostProcessHandle::Invalid();
    VolumetricFogSettings m_volumetricFogSettings{};

    JobSystem        m_jobSystem;
    SystemScheduler  m_systemScheduler;
    RenderFramePhase m_framePhase = RenderFramePhase::Idle;

    FrameDispatch       m_frameDispatch{};
    GDXRenderFrameGraph m_frameGraph;
};
