#include "GDXDX11RenderExecutor.h"
#include "GDXVertexFlags.h"
#include "SubmeshData.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <DirectXMath.h>
#include <cstring>
#include <vector>

// ---------------------------------------------------------------------------
// Helper: DX11 Buffer erstellen
// ---------------------------------------------------------------------------
static ID3D11Buffer* CreateBuffer(
    ID3D11Device* device,
    const void*   data,
    uint32_t      byteSize,
    D3D11_BIND_FLAG bind,
    bool dynamic = false)
{
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth      = byteSize;
    desc.Usage          = dynamic ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT;
    desc.BindFlags      = bind;
    desc.CPUAccessFlags = dynamic ? D3D11_CPU_ACCESS_WRITE : 0u;

    D3D11_SUBRESOURCE_DATA init = {};
    init.pSysMem = data;

    ID3D11Buffer* buf = nullptr;
    if (FAILED(device->CreateBuffer(&desc, data ? &init : nullptr, &buf)))
        return nullptr;
    return buf;
}

// ---------------------------------------------------------------------------
// GDXDX11MeshUploader::Upload — lädt ALLE vorhandenen Streams hoch.
// Kein flagsVertex hier — der Uploader lädt alles was in SubmeshData steht.
// Welche Streams beim Draw gebunden werden, entscheidet der Shader.
// ---------------------------------------------------------------------------
bool GDXDX11MeshUploader::Upload(MeshAssetResource& mesh)
{
    for (uint32_t i = 0; i < mesh.SubmeshCount(); ++i)
    {
        if (!UploadSubmesh(mesh.submeshes[i], mesh.gpuBuffers[i]))
            return false;
    }
    mesh.gpuReleaseCallback = &GDXDX11MeshUploader::Release;
    mesh.gpuReady = true;
    return true;
}

bool GDXDX11MeshUploader::UploadSubmesh(SubmeshData& cpu, GpuMeshBuffer& gpu)
{
    const uint32_t vertCount = cpu.VertexCount();
    if (vertCount == 0) return false;

    gpu.vertexCount = vertCount;

    // POSITION (immer vorhanden)
    {
        auto* buf = CreateBuffer(m_device,
            cpu.positions.data(),
            static_cast<uint32_t>(cpu.positions.size() * sizeof(DirectX::XMFLOAT3)),
            D3D11_BIND_VERTEX_BUFFER);
        if (!buf) return false;
        gpu.positionBuffer  = buf;
        gpu.stridePosition  = sizeof(DirectX::XMFLOAT3);
    }

    // NORMAL
    if (cpu.HasNormals())
    {
        auto* buf = CreateBuffer(m_device,
            cpu.normals.data(),
            static_cast<uint32_t>(cpu.normals.size() * sizeof(DirectX::XMFLOAT3)),
            D3D11_BIND_VERTEX_BUFFER);
        if (buf) { gpu.normalBuffer = buf; gpu.strideNormal = sizeof(DirectX::XMFLOAT3); }
    }

    // COLOR
    if (!cpu.colors.empty() && cpu.colors.size() == cpu.positions.size())
    {
        auto* buf = CreateBuffer(m_device,
            cpu.colors.data(),
            static_cast<uint32_t>(cpu.colors.size() * sizeof(DirectX::XMFLOAT4)),
            D3D11_BIND_VERTEX_BUFFER);
        if (buf) { gpu.colorBuffer = buf; gpu.strideColor = sizeof(DirectX::XMFLOAT4); }
    }

    // TEXCOORD0 (uv0)
    if (cpu.HasUV0())
    {
        auto* buf = CreateBuffer(m_device,
            cpu.uv0.data(),
            static_cast<uint32_t>(cpu.uv0.size() * sizeof(DirectX::XMFLOAT2)),
            D3D11_BIND_VERTEX_BUFFER);
        if (buf) { gpu.uv1Buffer = buf; gpu.strideUV1 = sizeof(DirectX::XMFLOAT2); }
    }

    // TANGENT (float4: xyz + Handedness)
    if (cpu.HasTangents())
    {
        auto* buf = CreateBuffer(m_device,
            cpu.tangents.data(),
            static_cast<uint32_t>(cpu.tangents.size() * sizeof(DirectX::XMFLOAT4)),
            D3D11_BIND_VERTEX_BUFFER);
        if (buf) { gpu.tangentBuffer = buf; gpu.strideTangent = sizeof(DirectX::XMFLOAT4); }
    }

    // BONE INDICES + WEIGHTS
    if (cpu.HasSkinning())
    {
        auto* bi = CreateBuffer(m_device,
            cpu.boneIndices.data(),
            static_cast<uint32_t>(cpu.boneIndices.size() * sizeof(DirectX::XMUINT4)),
            D3D11_BIND_VERTEX_BUFFER);
        auto* bw = CreateBuffer(m_device,
            cpu.boneWeights.data(),
            static_cast<uint32_t>(cpu.boneWeights.size() * sizeof(DirectX::XMFLOAT4)),
            D3D11_BIND_VERTEX_BUFFER);
        if (bi) { gpu.boneIndexBuffer  = bi; gpu.strideBoneIndex  = sizeof(DirectX::XMUINT4);  }
        if (bw) { gpu.boneWeightBuffer = bw; gpu.strideBoneWeight = sizeof(DirectX::XMFLOAT4); }
    }

    // INDEX BUFFER
    if (!cpu.indices.empty())
    {
        auto* ib = CreateBuffer(m_device,
            cpu.indices.data(),
            static_cast<uint32_t>(cpu.indices.size() * sizeof(uint32_t)),
            D3D11_BIND_INDEX_BUFFER);
        if (!ib)
        {
            // VBs wieder freigeben
            if (gpu.positionBuffer)   { static_cast<ID3D11Buffer*>(gpu.positionBuffer)->Release();   gpu.positionBuffer   = nullptr; }
            if (gpu.normalBuffer)     { static_cast<ID3D11Buffer*>(gpu.normalBuffer)->Release();     gpu.normalBuffer     = nullptr; }
            if (gpu.colorBuffer)      { static_cast<ID3D11Buffer*>(gpu.colorBuffer)->Release();      gpu.colorBuffer      = nullptr; }
            if (gpu.uv1Buffer)        { static_cast<ID3D11Buffer*>(gpu.uv1Buffer)->Release();        gpu.uv1Buffer        = nullptr; }
            if (gpu.tangentBuffer)    { static_cast<ID3D11Buffer*>(gpu.tangentBuffer)->Release();    gpu.tangentBuffer    = nullptr; }
            if (gpu.boneIndexBuffer)  { static_cast<ID3D11Buffer*>(gpu.boneIndexBuffer)->Release();  gpu.boneIndexBuffer  = nullptr; }
            if (gpu.boneWeightBuffer) { static_cast<ID3D11Buffer*>(gpu.boneWeightBuffer)->Release(); gpu.boneWeightBuffer = nullptr; }
            return false;
        }
        gpu.indexBuffer = ib;
        gpu.indexCount  = static_cast<uint32_t>(cpu.indices.size());
    }

    gpu.ready = true;
    return true;
}

void GDXDX11MeshUploader::Release(MeshAssetResource& mesh)
{
    for (auto& gpu : mesh.gpuBuffers)
    {
        auto safeRelease = [](void*& ptr)
        {
            if (ptr) { static_cast<ID3D11Buffer*>(ptr)->Release(); ptr = nullptr; }
        };

        safeRelease(gpu.positionBuffer);
        safeRelease(gpu.normalBuffer);
        safeRelease(gpu.colorBuffer);
        safeRelease(gpu.uv1Buffer);
        safeRelease(gpu.uv2Buffer);
        safeRelease(gpu.tangentBuffer);
        safeRelease(gpu.boneIndexBuffer);
        safeRelease(gpu.boneWeightBuffer);
        safeRelease(gpu.indexBuffer);

        gpu.stridePosition = gpu.strideNormal = gpu.strideColor = 0u;
        gpu.strideUV1 = gpu.strideUV2 = gpu.strideTangent = 0u;
        gpu.strideBoneIndex = gpu.strideBoneWeight = 0u;
        gpu.indexCount = gpu.vertexCount = 0u;
        gpu.ready = false;
    }
}

// ---------------------------------------------------------------------------
// GDXDX11RenderExecutor::Init
// ---------------------------------------------------------------------------
bool GDXDX11RenderExecutor::Init(const InitParams& p)
{
    m_device  = p.device;
    m_context = p.context;

    if (!m_device || !m_context) return false;

    CreateConstantBuffers();
    return m_entityCB && m_frameCB;
}

void GDXDX11RenderExecutor::CreateConstantBuffers()
{
    m_entityCB = CreateBuffer(m_device, nullptr,
        sizeof(Dx11EntityConstants), D3D11_BIND_CONSTANT_BUFFER, true);

    m_frameCB = CreateBuffer(m_device, nullptr,
        sizeof(Dx11FrameConstants), D3D11_BIND_CONSTANT_BUFFER, true);
}

void GDXDX11RenderExecutor::Shutdown()
{
    if (m_entityCB) { m_entityCB->Release(); m_entityCB = nullptr; }
    if (m_frameCB)  { m_frameCB->Release();  m_frameCB  = nullptr; }
}

// ---------------------------------------------------------------------------
// UpdateFrameConstants
// ---------------------------------------------------------------------------
void GDXDX11RenderExecutor::UpdateFrameConstants(const FrameData& frame)
{
    if (!m_frameCB) return;

    Dx11FrameConstants fc = {};

    // row_major Shader → kein Transpose nötig.
    std::memcpy(fc.viewMatrix,     &frame.viewMatrix,           64);
    std::memcpy(fc.projMatrix,     &frame.projMatrix,           64);
    std::memcpy(fc.viewProjMatrix, &frame.viewProjMatrix,       64);
    std::memcpy(fc.shadowViewProj, &frame.shadowViewProjMatrix, 64);

    fc.cameraPos[0] = frame.cameraPos.x;
    fc.cameraPos[1] = frame.cameraPos.y;
    fc.cameraPos[2] = frame.cameraPos.z;
    fc.cameraPos[3] = 0.0f;

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (SUCCEEDED(m_context->Map(m_frameCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        std::memcpy(mapped.pData, &fc, sizeof(fc));
        m_context->Unmap(m_frameCB, 0);
    }

    m_context->VSSetConstantBuffers(1, 1, &m_frameCB);
    m_context->PSSetConstantBuffers(1, 1, &m_frameCB);
}

// ---------------------------------------------------------------------------
// BindVertexStreams — bindet Streams entsprechend vertexFlags.
// Exakt wie OYNAME SurfaceGpuBuffer::Draw() — Slot-Nummer = Reihenfolge der
// gesetzten Flags. Nur vorhandene Streams werden gebunden.
// ---------------------------------------------------------------------------
bool GDXDX11RenderExecutor::BindVertexStreams(const GpuMeshBuffer& gpu, uint32_t flags)
{
    ID3D11Buffer* buffers[8] = {};
    UINT          strides[8] = {};
    UINT          offsets[8] = {};
    UINT          slot = 0;

    // Makro: bind wenn Flag gesetzt, sonst Fehler wenn Pflicht aber fehlend.
    auto bind = [&](bool flagSet, void* buf, uint32_t stride, const char* /*name*/) -> bool
    {
        if (!flagSet) return true;
        if (!buf || stride == 0) return false;  // Shader braucht es, Buffer fehlt → skip draw
        buffers[slot] = static_cast<ID3D11Buffer*>(buf);
        strides[slot] = stride;
        offsets[slot] = 0u;
        ++slot;
        return true;
    };

    if (!bind(flags & GDX_VERTEX_POSITION,     gpu.positionBuffer,   gpu.stridePosition,   "POSITION"))     return false;
    if (!bind(flags & GDX_VERTEX_NORMAL,       gpu.normalBuffer,     gpu.strideNormal,     "NORMAL"))       return false;
    if (!bind(flags & GDX_VERTEX_COLOR,        gpu.colorBuffer,      gpu.strideColor,      "COLOR"))        return false;
    if (!bind(flags & GDX_VERTEX_TEX1,         gpu.uv1Buffer,        gpu.strideUV1,        "TEXCOORD0"))    return false;
    if (!bind(flags & GDX_VERTEX_TEX2,         gpu.uv2Buffer,        gpu.strideUV2,        "TEXCOORD1"))    return false;
    if (!bind(flags & GDX_VERTEX_TANGENT,      gpu.tangentBuffer,    gpu.strideTangent,    "TANGENT"))      return false;
    if (!bind(flags & GDX_VERTEX_BONE_INDICES, gpu.boneIndexBuffer,  gpu.strideBoneIndex,  "BLENDINDICES")) return false;
    if (!bind(flags & GDX_VERTEX_BONE_WEIGHTS, gpu.boneWeightBuffer, gpu.strideBoneWeight, "BLENDWEIGHT"))  return false;

    if (slot > 0)
        m_context->IASetVertexBuffers(0, slot, buffers, strides, offsets);

    return true;
}

// ---------------------------------------------------------------------------
// ExecuteQueue
// ---------------------------------------------------------------------------
void GDXDX11RenderExecutor::ExecuteQueue(
    const RenderQueue&                              queue,
    ResourceStore<MeshAssetResource, MeshTag>&     meshStore,
    ResourceStore<MaterialResource,  MaterialTag>& matStore,
    ResourceStore<GDXShaderResource, ShaderTag>&   shaderStore)
{
    m_drawCalls    = 0u;
    m_lastShader   = ShaderHandle::Invalid();
    m_lastMaterial = MaterialHandle::Invalid();

    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    for (const RenderCommand& cmd : queue.commands)
    {
        MeshAssetResource* mesh   = meshStore.Get(cmd.mesh);
        MaterialResource*  mat    = matStore.Get(cmd.material);
        GDXShaderResource* shader = shaderStore.Get(cmd.shader);

        if (!mesh || !mat || !shader || !shader->IsValid()) continue;
        if (cmd.submeshIndex >= mesh->gpuBuffers.size())    continue;

        const GpuMeshBuffer& gpu = mesh->gpuBuffers[cmd.submeshIndex];
        if (!gpu.ready || !gpu.positionBuffer)              continue;

        // --- Shader binden (State-Batching) ---------------------------------
        if (cmd.shader != m_lastShader)
        {
            m_context->VSSetShader(static_cast<ID3D11VertexShader*>(shader->vertexShader), nullptr, 0);
            m_context->PSSetShader(static_cast<ID3D11PixelShader*> (shader->pixelShader),  nullptr, 0);
            m_context->IASetInputLayout(static_cast<ID3D11InputLayout*>(shader->inputLayout));
            m_lastShader   = cmd.shader;
            m_lastMaterial = MaterialHandle::Invalid(); // neuer Shader → Material neu binden
        }

        // --- Material-cbuffer (b2) binden (State-Batching) ------------------
        if (cmd.material != m_lastMaterial)
        {
            if (mat->gpuConstantBuffer)
            {
                auto* matCB = static_cast<ID3D11Buffer*>(mat->gpuConstantBuffer);

                if (mat->cpuDirty)
                {
                    D3D11_MAPPED_SUBRESOURCE mapped = {};
                    if (SUCCEEDED(m_context->Map(matCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
                    {
                        std::memcpy(mapped.pData, &mat->data, sizeof(MaterialData));
                        m_context->Unmap(matCB, 0);
                    }
                    mat->cpuDirty = false;
                }

                m_context->PSSetConstantBuffers(2, 1, &matCB);
            }
            m_lastMaterial = cmd.material;
        }

        // --- Per-Entity cbuffer (b0) ----------------------------------------
        {
            Dx11EntityConstants ec = {};
            std::memcpy(ec.worldMatrix, &cmd.worldMatrix, 64);

            DirectX::XMMATRIX w   = DirectX::XMLoadFloat4x4(&cmd.worldMatrix);
            DirectX::XMMATRIX wIT = DirectX::XMMatrixTranspose(
                DirectX::XMMatrixInverse(nullptr, w));
            DirectX::XMFLOAT4X4 witF;
            DirectX::XMStoreFloat4x4(&witF, wIT);
            std::memcpy(ec.worldInverseTranspose, &witF, 64);

            D3D11_MAPPED_SUBRESOURCE mapped = {};
            if (SUCCEEDED(m_context->Map(m_entityCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
            {
                std::memcpy(mapped.pData, &ec, sizeof(ec));
                m_context->Unmap(m_entityCB, 0);
            }
            m_context->VSSetConstantBuffers(0, 1, &m_entityCB);
        }

        // --- Vertex-Streams binden (flag-gesteuert wie OYNAME) ---------------
        if (!BindVertexStreams(gpu, shader->vertexFlags))
            continue;

        // --- Index Buffer + Draw --------------------------------------------
        if (gpu.indexBuffer)
        {
            m_context->IASetIndexBuffer(
                static_cast<ID3D11Buffer*>(gpu.indexBuffer), DXGI_FORMAT_R32_UINT, 0u);
            m_context->DrawIndexed(gpu.indexCount, 0u, 0);
        }
        else
        {
            m_context->Draw(gpu.vertexCount, 0u);
        }

        ++m_drawCalls;
    }
}
