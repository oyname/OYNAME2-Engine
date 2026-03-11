#pragma once

#include "FrameData.h"
#include "RenderQueue.h"
#include "ResourceStore.h"
#include "MeshAssetResource.h"
#include "MaterialResource.h"
#include "GDXShaderResource.h"

#include <cstdint>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Buffer;

// ---------------------------------------------------------------------------
// GDXDX11MeshUploader — lädt CPU-Geometrie auf die GPU.
//
// Lädt ALLE in SubmeshData vorhandenen Streams hoch (separate Buffer).
// Der Shader-Vertrag (welche Streams gebunden werden) liegt beim Executor.
// Ein Mesh kann mit verschiedenen Shadern gerendert werden — der Uploader
// muss nicht wissen welcher Shader verwendet wird.
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
// Constant-Buffer Structs
// ---------------------------------------------------------------------------
struct alignas(16) Dx11EntityConstants
{
    float worldMatrix[16];
    float worldInverseTranspose[16];
};
static_assert(sizeof(Dx11EntityConstants) == 128, "EntityConstants size mismatch");

struct alignas(16) Dx11FrameConstants
{
    float viewMatrix[16];
    float projMatrix[16];
    float viewProjMatrix[16];
    float cameraPos[4];
    float shadowViewProj[16];
};
static_assert(sizeof(Dx11FrameConstants) == 272, "FrameConstants size mismatch");

// ---------------------------------------------------------------------------
// GDXDX11RenderExecutor — führt RenderQueue über DX11 aus.
//
// Liest pro RenderCommand den ShaderHandle → holt GDXShaderResource aus dem
// ShaderStore → bindet VS/PS/InputLayout + nur die Vertex-Streams die der
// Shader per vertexFlags erwartet (wie OYNAME SurfaceGpuBuffer::Draw).
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

    // ShaderStore jetzt als Parameter — Executor braucht vertexFlags pro Shader.
    void ExecuteQueue(
        const RenderQueue&                                    queue,
        ResourceStore<MeshAssetResource,  MeshTag>&          meshStore,
        ResourceStore<MaterialResource,   MaterialTag>&       matStore,
        ResourceStore<GDXShaderResource,  ShaderTag>&        shaderStore);

    uint32_t GetDrawCallCount() const { return m_drawCalls; }

private:
    void CreateConstantBuffers();

    // Bindet Vertex-Streams entsprechend vertexFlags (wie OYNAME Draw()).
    // Gibt true zurück wenn alle Pflicht-Streams (aus flags) vorhanden sind.
    bool BindVertexStreams(const GpuMeshBuffer& gpu, uint32_t vertexFlags);

    ID3D11Device*        m_device  = nullptr;
    ID3D11DeviceContext* m_context = nullptr;

    ID3D11Buffer* m_entityCB = nullptr;
    ID3D11Buffer* m_frameCB  = nullptr;

    // State-Batching: letzter Shader + letztes Material
    ShaderHandle   m_lastShader   = ShaderHandle::Invalid();
    MaterialHandle m_lastMaterial = MaterialHandle::Invalid();

    uint32_t m_drawCalls = 0u;
};
