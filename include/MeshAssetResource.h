#pragma once
#include "SubmeshData.h"
#include "Handle.h"

#include <vector>
#include <cstdint>
#include <string>

// ---------------------------------------------------------------------------
// GpuMeshBuffer — GPU-Seite eines Sub-Mesh-Slots.
//
// Separate Streams (wie OYNAME SurfaceGpuBuffer):
//   Jeder Vertex-Kanal hat seinen eigenen Buffer + Stride.
//   GDXDX11MeshUploader befüllt alle Streams die in SubmeshData vorhanden sind.
//   GDXDX11RenderExecutor bindet beim Draw nur die Streams die der Shader braucht
//   (gesteuert durch GDXShaderResource::vertexFlags).
//
// Backend-agnostisch: void* statt ID3D11Buffer* — kein <d3d11.h> nötig.
// ---------------------------------------------------------------------------
struct GpuMeshBuffer
{
    // Separate Vertex-Streams
    void* positionBuffer    = nullptr;   // float3  POSITION0
    void* normalBuffer      = nullptr;   // float3  NORMAL0
    void* colorBuffer       = nullptr;   // float4  COLOR0
    void* uv1Buffer         = nullptr;   // float2  TEXCOORD0 (UV0)
    void* uv2Buffer         = nullptr;   // float2  TEXCOORD1 (UV1 / 2. UV-Set)
    void* tangentBuffer     = nullptr;   // float4  TANGENT0   (xyz + Handedness)
    void* boneIndexBuffer   = nullptr;   // uint4   BLENDINDICES0
    void* boneWeightBuffer  = nullptr;   // float4  BLENDWEIGHT0

    // Index Buffer
    void* indexBuffer       = nullptr;

    // Strides (0 = Stream nicht vorhanden / nicht hochgeladen)
    uint32_t stridePosition   = 0u;
    uint32_t strideNormal     = 0u;
    uint32_t strideColor      = 0u;
    uint32_t strideUV1        = 0u;
    uint32_t strideUV2        = 0u;
    uint32_t strideTangent    = 0u;
    uint32_t strideBoneIndex  = 0u;
    uint32_t strideBoneWeight = 0u;

    uint32_t vertexCount = 0u;
    uint32_t indexCount  = 0u;

    bool ready = false;
};

// ---------------------------------------------------------------------------
// MeshAssetResource — geteilte Geometrie-Ressource im ResourceStore.
// ---------------------------------------------------------------------------
struct MeshAssetResource
{
    std::vector<SubmeshData>   submeshes;
    std::vector<GpuMeshBuffer> gpuBuffers;

    std::string debugName;
    bool gpuReady = false;

    void (*gpuReleaseCallback)(MeshAssetResource&) = nullptr;

    uint32_t SubmeshCount() const noexcept
    {
        return static_cast<uint32_t>(submeshes.size());
    }

    bool IsEmpty() const noexcept { return submeshes.empty(); }

    void AddSubmesh(SubmeshData data)
    {
        submeshes.push_back(std::move(data));
        gpuBuffers.push_back({});
    }

    bool IsGpuReadyAt(uint32_t i) const noexcept
    {
        return i < gpuBuffers.size() && gpuBuffers[i].ready;
    }

    ~MeshAssetResource()
    {
        if (gpuReleaseCallback)
            gpuReleaseCallback(*this);
    }

    MeshAssetResource() = default;
    MeshAssetResource(const MeshAssetResource&) = delete;
    MeshAssetResource& operator=(const MeshAssetResource&) = delete;
    MeshAssetResource(MeshAssetResource&&) = default;
    MeshAssetResource& operator=(MeshAssetResource&&) = default;
};
