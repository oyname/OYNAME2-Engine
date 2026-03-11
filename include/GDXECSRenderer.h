#pragma once

#include "IGDXRenderer.h"
#include "Registry.h"
#include "Components.h"
#include "ResourceStore.h"
#include "MeshAssetResource.h"
#include "MaterialResource.h"
#include "GDXShaderResource.h"
#include "GDXTextureResource.h"
#include "GDXVertexFlags.h"
#include "FrameData.h"
#include "RenderQueue.h"
#include "TransformSystem.h"
#include "CameraSystem.h"
#include "RenderGatherSystem.h"
#include "GDXDX11RenderExecutor.h"
#include "GDXLightSystem.h"
#include "GDXSamplerCache.h"
#include "GDXShadowMap.h"
#include "IGDXDXGIContext.h"

#include <memory>
#include <string>
#include <functional>
#include <chrono>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RasterizerState;
struct ID3D11DepthStencilState;
struct ID3D11BlendState;

// ---------------------------------------------------------------------------
// GDXECSRenderer — DX11-Renderer mit ECS-Integration.
//
// Anwender-API (Kurzübersicht):
//
//   // Shader laden (ein Aufruf, InputLayout automatisch):
//   ShaderHandle hShader = renderer.CreateShader(
//       L"MyVS.hlsl", L"MyPS.hlsl", GDX_VERTEX_POSITION | GDX_VERTEX_NORMAL | GDX_VERTEX_TEX1);
//
//   // Textur laden:
//   TextureHandle hTex = renderer.LoadTexture(L"textures/brick.png");
//
//   // Material:
//   MaterialResource mat;
//   mat.albedoTex = hTex;
//   mat.data.metallic = 0.0f; mat.data.roughness = 0.8f;
//   mat.data.flags |= MF_USE_NORMAL_MAP | MF_SHADING_PBR;
//   MaterialHandle hMat = renderer.CreateMaterial(mat);
//
//   // Licht (ECS):
//   EntityID light = registry.Create();
//   registry.Add<LightComponent>(light, { LightKind::Directional, ..., castShadows=true });
//   registry.Add<WorldTransformComponent>(light);
//
//   // Shadow-Map-Größe vor Initialize (optional, Standard 2048):
//   renderer.SetShadowMapSize(4096);
// ---------------------------------------------------------------------------
class GDXECSRenderer final : public IGDXRenderer
{
public:
    explicit GDXECSRenderer(std::unique_ptr<IGDXDXGIContext> context);
    ~GDXECSRenderer() override;

    bool Initialize() override;
    void BeginFrame() override;
    void EndFrame() override;
    void Resize(int w, int h) override;
    void Shutdown() override;

    using TickFn = std::function<void(float)>;
    void SetTickCallback(TickFn fn) { m_tickCallback = std::move(fn); }

    Registry& GetRegistry() { return m_registry; }

    // --- Ressourcen-API ---------------------------------------------------

    // Shader laden + InputLayout automatisch aus flags bauen.
    ShaderHandle   CreateShader(const std::wstring& vsFile,
                                const std::wstring& psFile,
                                uint32_t vertexFlags = GDX_VERTEX_DEFAULT);

    // Textur von Datei laden (gecacht — gleicher Pfad → gleicher Handle).
    // isSRGB=true  → Albedo, Emissive (gamma-korrigiert, Standard)
    // isSRGB=false → Normal, ORM, Roughness (lineare Daten)
    TextureHandle  LoadTexture(const std::wstring& filePath, bool isSRGB = true);

    MeshHandle     UploadMesh(MeshAssetResource mesh);
    MaterialHandle CreateMaterial(MaterialResource mat);

    // Standard-Shader-Handle (gültig nach Initialize).
    ShaderHandle   GetDefaultShader() const { return m_defaultShader; }

    // Shadow-Map-Größe konfigurieren (vor Initialize aufrufen, Standard: 2048).
    void SetShadowMapSize(uint32_t size) { m_shadowMapSize = size; }

    // Szenen-Ambient: globale Grundhelligkeit (kein Licht-spezifisch).
    // Setzt FrameData.sceneAmbient — wirkt ab dem nächsten Frame.
    void SetSceneAmbient(float r, float g, float b)
    {
        m_frameData.sceneAmbient = { r, g, b };
    }

    // --- Stores -----------------------------------------------------------
    ResourceStore<MeshAssetResource,  MeshTag>&    GetMeshStore()    { return m_meshStore; }
    ResourceStore<MaterialResource,   MaterialTag>& GetMatStore()     { return m_matStore; }
    ResourceStore<GDXShaderResource,  ShaderTag>&  GetShaderStore()  { return m_shaderStore; }
    ResourceStore<GDXTextureResource, TextureTag>& GetTextureStore() { return m_texStore; }

    // --- Diagnostics -------------------------------------------------------
    struct FrameStats
    {
        uint32_t drawCalls      = 0u;
        uint32_t renderCommands = 0u;
        uint32_t lightCount     = 0u;
    };
    const FrameStats& GetFrameStats() const { return m_stats; }
    void SetClearColor(float r, float g, float b, float a = 1.0f);

private:
    ShaderHandle LoadShaderInternal(const std::wstring& vsFile,
                                    const std::wstring& psFile,
                                    uint32_t vertexFlags,
                                    const std::wstring& debugName);

    bool LoadDefaultShaders();
    bool CreateRenderStates();
    bool InitDefaultTextures();
    bool CreateMaterialCBuffer(MaterialResource& mat);

    std::unique_ptr<IGDXDXGIContext> m_context;
    ID3D11Device*        m_device = nullptr;
    ID3D11DeviceContext* m_ctx    = nullptr;

    Registry m_registry;

    ResourceStore<MeshAssetResource,  MeshTag>    m_meshStore;
    ResourceStore<MaterialResource,   MaterialTag> m_matStore;
    ResourceStore<GDXShaderResource,  ShaderTag>  m_shaderStore;
    ResourceStore<GDXTextureResource, TextureTag> m_texStore;

    TransformSystem    m_transformSystem;
    CameraSystem       m_cameraSystem;
    RenderGatherSystem m_gatherSystem;
    GDXLightSystem     m_lightSystem;

    GDXDX11MeshUploader*  m_meshUploader = nullptr;
    GDXDX11RenderExecutor m_executor;

    GDXSamplerCache m_samplerCache;
    GDXShadowMap    m_shadowMap;
    uint32_t        m_shadowMapSize = 2048u;

    RenderQueue m_opaqueQueue;
    RenderQueue m_shadowQueue;
    FrameData   m_frameData;

    ShaderHandle m_defaultShader;
    ShaderHandle m_shadowShader;    // Depth-only Shadow Pass Shader

    // Default Fallback-Texturen
    TextureHandle m_defaultWhiteTex;
    TextureHandle m_defaultNormalTex;
    TextureHandle m_defaultORMTex;
    TextureHandle m_defaultBlackTex;

    ID3D11RasterizerState*   m_rasterizerState   = nullptr;
    ID3D11DepthStencilState* m_depthStencilState = nullptr;
    ID3D11BlendState*        m_blendState        = nullptr;

    float      m_clearColor[4] = { 0.05f, 0.05f, 0.12f, 1.0f };
    FrameStats m_stats;
    bool       m_initialized = false;

    TickFn m_tickCallback;

    using Clock     = std::chrono::steady_clock;
    using TimePoint = std::chrono::time_point<Clock>;
    TimePoint m_lastFrameTime;
    bool      m_clockStarted = false;
};
