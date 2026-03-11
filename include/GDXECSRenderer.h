#pragma once

#include "IGDXRenderer.h"
#include "Registry.h"
#include "Components.h"
#include "ResourceStore.h"
#include "MeshAssetResource.h"
#include "MaterialResource.h"
#include "GDXShaderResource.h"
#include "GDXVertexFlags.h"
#include "FrameData.h"
#include "RenderQueue.h"
#include "TransformSystem.h"
#include "CameraSystem.h"
#include "RenderGatherSystem.h"
#include "GDXDX11RenderExecutor.h"
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
// Shader-Workflow (wie OYNAME):
//
//   // Engine-Start: Standard-Shader wird automatisch geladen.
//   // Kein Code nötig — m_defaultShader ist danach gültig.
//
//   // Eigener Shader (ein Aufruf, fertig):
//   ShaderHandle hShader = renderer.CreateShader(
//       L"MyVS.hlsl", L"MyPS.hlsl",
//       GDX_VERTEX_POSITION | GDX_VERTEX_NORMAL | GDX_VERTEX_TEX1);
//
//   // Shader einem Material zuweisen:
//   MaterialResource mat = MaterialResource::FlatColor(1,0,0);
//   mat.shader = hShader;
//   MaterialHandle hMat = renderer.CreateMaterial(mat);
//
//   // Material ohne Shader → Standard-Shader wird automatisch verwendet.
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

    MeshHandle     UploadMesh(MeshAssetResource mesh);
    MaterialHandle CreateMaterial(MaterialResource mat);

    // Shader laden + InputLayout automatisch aus flags bauen (wie OYNAME).
    // vertexFlags steuert:
    //   - InputLayout-Bau         (D3D11_INPUT_ELEMENT_DESC[] dynamisch erzeugt)
    //   - Upload (welche Streams)  (beim UploadMesh)
    //   - Draw   (welche Slots)    (im ExecuteQueue)
    ShaderHandle CreateShader(
        const std::wstring& vsFile,
        const std::wstring& psFile,
        uint32_t            vertexFlags = GDX_VERTEX_DEFAULT);

    // Standard-Shader-Handle (gültig nach Initialize()).
    ShaderHandle GetDefaultShader() const { return m_defaultShader; }

    // --- Stores (für externe Systeme) -------------------------------------
    ResourceStore<MeshAssetResource,  MeshTag>&    GetMeshStore()    { return m_meshStore; }
    ResourceStore<MaterialResource,   MaterialTag>& GetMatStore()     { return m_matStore; }
    ResourceStore<GDXShaderResource,  ShaderTag>&  GetShaderStore()  { return m_shaderStore; }

    // --- Diagnostics -------------------------------------------------------
    struct FrameStats
    {
        uint32_t drawCalls      = 0u;
        uint32_t renderCommands = 0u;
    };
    const FrameStats& GetFrameStats() const { return m_stats; }
    void SetClearColor(float r, float g, float b, float a = 1.0f);

private:
    // Interner Shader-Lade-Helfer (verwendet von CreateShader + Standard-Shader-Init).
    // Baut InputLayout dynamisch aus vertexFlags — analog zu OYNAME InputLayoutManager.
    ShaderHandle LoadShaderInternal(
        const std::wstring& vsFile,
        const std::wstring& psFile,
        uint32_t            vertexFlags,
        const std::wstring& debugName);

    bool LoadDefaultShaders();
    bool CreateRenderStates();
    bool CreateMaterialCBuffer(MaterialResource& mat);

    std::unique_ptr<IGDXDXGIContext> m_context;
    ID3D11Device*        m_device = nullptr;
    ID3D11DeviceContext* m_ctx    = nullptr;

    Registry m_registry;

    ResourceStore<MeshAssetResource,  MeshTag>    m_meshStore;
    ResourceStore<MaterialResource,   MaterialTag> m_matStore;
    ResourceStore<GDXShaderResource,  ShaderTag>  m_shaderStore;

    TransformSystem    m_transformSystem;
    CameraSystem       m_cameraSystem;
    RenderGatherSystem m_gatherSystem;

    GDXDX11MeshUploader*  m_meshUploader = nullptr;
    GDXDX11RenderExecutor m_executor;

    RenderQueue m_opaqueQueue;
    RenderQueue m_shadowQueue;
    FrameData   m_frameData;

    // Standard-Shader — wird in Initialize() geladen.
    // Materialien ohne eigenen Shader verwenden ihn automatisch.
    ShaderHandle m_defaultShader;

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
