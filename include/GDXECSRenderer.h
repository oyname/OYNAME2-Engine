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
#include "GDXVertexFlags.h"
#include "FrameData.h"
#include "RenderQueue.h"
#include "TransformSystem.h"
#include "CameraSystem.h"
#include "RenderGatherSystem.h"

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

    TextureHandle  LoadTexture(const std::wstring& filePath, bool isSRGB = true);

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
        const std::wstring& debugName);

    bool LoadDefaultShaders();

    std::unique_ptr<IGDXRenderBackend> m_backend;

    Registry m_registry;

    ResourceStore<MeshAssetResource, MeshTag>     m_meshStore;
    ResourceStore<MaterialResource, MaterialTag> m_matStore;
    ResourceStore<GDXShaderResource, ShaderTag>   m_shaderStore;
    ResourceStore<GDXTextureResource, TextureTag>  m_texStore;

    TransformSystem    m_transformSystem;
    CameraSystem       m_cameraSystem;
    RenderGatherSystem m_gatherSystem;

    RenderQueue m_opaqueQueue;
    RenderQueue m_shadowQueue;
    FrameData   m_frameData;

    ShaderHandle m_defaultShader;
    ShaderHandle m_shadowShader;

    TextureHandle m_defaultWhiteTex;
    TextureHandle m_defaultNormalTex;
    TextureHandle m_defaultORMTex;
    TextureHandle m_defaultBlackTex;

    float      m_clearColor[4] = { 0.05f, 0.05f, 0.12f, 1.0f };
    FrameStats m_stats;
    bool       m_initialized = false;

    TickFn m_tickCallback;


};
