#pragma once

#include "FrameData.h"
#include "RenderQueue.h"
#include "ResourceStore.h"
#include "MeshAssetResource.h"
#include "MaterialResource.h"
#include "GDXShaderResource.h"
#include "GDXTextureResource.h"

#include <cstdint>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Buffer;

// ---------------------------------------------------------------------------
// Constant-Buffer Structs
// ---------------------------------------------------------------------------
struct alignas(16) Dx11EntityConstants
{
    float worldMatrix[16];
    float worldInverseTranspose[16];
};
static_assert(sizeof(Dx11EntityConstants) == 128);

struct alignas(16) Dx11FrameConstants
{
    float viewMatrix[16];
    float projMatrix[16];
    float viewProjMatrix[16];
    float cameraPos[4];
    float shadowViewProj[16];
};
static_assert(sizeof(Dx11FrameConstants) == 272);

// ---------------------------------------------------------------------------
// GDXDX11MeshUploader
// ---------------------------------------------------------------------------
class GDXDX11MeshUploader
{
public:
    GDXDX11MeshUploader(ID3D11Device* device, ID3D11DeviceContext* context)
        : m_device(device), m_context(context) {}

    bool Upload(MeshAssetResource& mesh);
    static void Release(MeshAssetResource& mesh);

private:
    bool UploadSubmesh(SubmeshData& cpu, GpuMeshBuffer& gpu);

    ID3D11Device*        m_device  = nullptr;
    ID3D11DeviceContext* m_context = nullptr;
};

// ---------------------------------------------------------------------------
// GDXDX11RenderExecutor
// ---------------------------------------------------------------------------
class GDXDX11RenderExecutor
{
public:
    struct InitParams
    {
        ID3D11Device*        device  = nullptr;
        ID3D11DeviceContext* context = nullptr;
    };

    bool Init(const InitParams& p);
    void Shutdown();

    void UpdateFrameConstants(const FrameData& frame);

    // Haupt-Pass: Mesh + Material + Texturen + Shader
    void ExecuteQueue(
        const RenderQueue&                                   queue,
        ResourceStore<MeshAssetResource,  MeshTag>&         meshStore,
        ResourceStore<MaterialResource,   MaterialTag>&      matStore,
        ResourceStore<GDXShaderResource,  ShaderTag>&       shaderStore,
        ResourceStore<GDXTextureResource, TextureTag>&      texStore,
        void* shadowSRV = nullptr);

    // Shadow-Pass: Depth-Only, kein Material, nur POSITION-Stream.
    // Separater Pfad — ExecuteQueue() würde alle Draws überspringen weil
    // Shadow-Commands kein gültiges Material haben.
    void ExecuteShadowQueue(
        const RenderQueue&                          queue,
        ResourceStore<MeshAssetResource, MeshTag>&  meshStore,
        ResourceStore<GDXShaderResource, ShaderTag>& shaderStore);

    uint32_t GetDrawCallCount() const { return m_drawCalls; }

private:
    void CreateConstantBuffers();
    bool BindVertexStreams(const GpuMeshBuffer& gpu, uint32_t vertexFlags);

    // Bindet Texturen t0-t3 aus MaterialResource (Fallback auf Default-Handles)
    void BindMaterialTextures(
        const MaterialResource& mat,
        ResourceStore<GDXTextureResource, TextureTag>& texStore,
        TextureHandle defaultWhite,
        TextureHandle defaultNormal,
        TextureHandle defaultORM,
        TextureHandle defaultBlack);

    ID3D11Device*        m_device  = nullptr;
    ID3D11DeviceContext* m_context = nullptr;

    ID3D11Buffer* m_entityCB = nullptr;
    ID3D11Buffer* m_frameCB  = nullptr;

    ShaderHandle   m_lastShader   = ShaderHandle::Invalid();
    MaterialHandle m_lastMaterial = MaterialHandle::Invalid();

    uint32_t m_drawCalls = 0u;

public:
    // Default-Textur-Handles (vom Renderer gesetzt nach Initialize)
    TextureHandle defaultWhiteTex;
    TextureHandle defaultNormalTex;
    TextureHandle defaultORMTex;
    TextureHandle defaultBlackTex;
};
