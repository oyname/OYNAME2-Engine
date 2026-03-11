#include "GDXDX11RenderExecutor.h"
#include "GDXVertexFlags.h"
#include "SubmeshData.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <DirectXMath.h>
#include <cstring>

// ---------------------------------------------------------------------------
// Helper
// ---------------------------------------------------------------------------
static ID3D11Buffer* CreateBuffer(ID3D11Device* device,
    const void* data, uint32_t bytes, D3D11_BIND_FLAG bind, bool dynamic = false)
{
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth      = bytes;
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

// ===========================================================================
// GDXDX11MeshUploader
// ===========================================================================
bool GDXDX11MeshUploader::Upload(MeshAssetResource& mesh)
{
    for (uint32_t i = 0; i < mesh.SubmeshCount(); ++i)
        if (!UploadSubmesh(mesh.submeshes[i], mesh.gpuBuffers[i])) return false;
    mesh.gpuReleaseCallback = &GDXDX11MeshUploader::Release;
    mesh.gpuReady = true;
    return true;
}

bool GDXDX11MeshUploader::UploadSubmesh(SubmeshData& cpu, GpuMeshBuffer& gpu)
{
    if (cpu.VertexCount() == 0) return false;
    gpu.vertexCount = cpu.VertexCount();

    auto upload = [&](const void* data, uint32_t stride, uint32_t count,
                      void*& outBuf, uint32_t& outStride) -> bool
    {
        auto* buf = CreateBuffer(m_device, data, stride * count, D3D11_BIND_VERTEX_BUFFER);
        if (!buf) return false;
        outBuf    = buf;
        outStride = stride;
        return true;
    };

    if (!upload(cpu.positions.data(), sizeof(DirectX::XMFLOAT3),
                cpu.VertexCount(), gpu.positionBuffer, gpu.stridePosition))
        return false;

    if (cpu.HasNormals())
        upload(cpu.normals.data(), sizeof(DirectX::XMFLOAT3),
               cpu.VertexCount(), gpu.normalBuffer, gpu.strideNormal);

    if (!cpu.colors.empty() && cpu.colors.size() == cpu.positions.size())
        upload(cpu.colors.data(), sizeof(DirectX::XMFLOAT4),
               cpu.VertexCount(), gpu.colorBuffer, gpu.strideColor);

    if (cpu.HasUV0())
        upload(cpu.uv0.data(), sizeof(DirectX::XMFLOAT2),
               cpu.VertexCount(), gpu.uv1Buffer, gpu.strideUV1);

    if (cpu.HasTangents())
        upload(cpu.tangents.data(), sizeof(DirectX::XMFLOAT4),
               cpu.VertexCount(), gpu.tangentBuffer, gpu.strideTangent);

    if (cpu.HasSkinning())
    {
        upload(cpu.boneIndices.data(), sizeof(DirectX::XMUINT4),
               cpu.VertexCount(), gpu.boneIndexBuffer, gpu.strideBoneIndex);
        upload(cpu.boneWeights.data(), sizeof(DirectX::XMFLOAT4),
               cpu.VertexCount(), gpu.boneWeightBuffer, gpu.strideBoneWeight);
    }

    if (!cpu.indices.empty())
    {
        auto* ib = CreateBuffer(m_device, cpu.indices.data(),
            static_cast<uint32_t>(cpu.indices.size() * sizeof(uint32_t)),
            D3D11_BIND_INDEX_BUFFER);
        if (!ib) return false;
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
        auto sr = [](void*& p){ if(p){ static_cast<ID3D11Buffer*>(p)->Release(); p=nullptr; } };
        sr(gpu.positionBuffer);  sr(gpu.normalBuffer);    sr(gpu.colorBuffer);
        sr(gpu.uv1Buffer);       sr(gpu.uv2Buffer);       sr(gpu.tangentBuffer);
        sr(gpu.boneIndexBuffer); sr(gpu.boneWeightBuffer); sr(gpu.indexBuffer);
        gpu = GpuMeshBuffer{};
    }
}

// ===========================================================================
// GDXDX11RenderExecutor
// ===========================================================================
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
    m_frameCB  = CreateBuffer(m_device, nullptr,
        sizeof(Dx11FrameConstants),  D3D11_BIND_CONSTANT_BUFFER, true);
}

void GDXDX11RenderExecutor::Shutdown()
{
    if (m_entityCB) { m_entityCB->Release(); m_entityCB = nullptr; }
    if (m_frameCB)  { m_frameCB->Release();  m_frameCB  = nullptr; }
}

void GDXDX11RenderExecutor::UpdateFrameConstants(const FrameData& frame)
{
    if (!m_frameCB) return;

    Dx11FrameConstants fc = {};
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
// BindVertexStreams — flag-gesteuert (wie OYNAME SurfaceGpuBuffer::Draw)
// ---------------------------------------------------------------------------
bool GDXDX11RenderExecutor::BindVertexStreams(const GpuMeshBuffer& gpu, uint32_t flags)
{
    ID3D11Buffer* buffers[8] = {};
    UINT          strides[8] = {};
    UINT          offsets[8] = {};
    UINT          slot = 0u;

    auto bind = [&](bool needed, void* buf, uint32_t stride) -> bool
    {
        if (!needed) return true;
        if (!buf || stride == 0) return false;
        buffers[slot] = static_cast<ID3D11Buffer*>(buf);
        strides[slot] = stride;
        offsets[slot] = 0u;
        ++slot;
        return true;
    };

    if (!bind(flags & GDX_VERTEX_POSITION,     gpu.positionBuffer,   gpu.stridePosition))   return false;
    if (!bind(flags & GDX_VERTEX_NORMAL,       gpu.normalBuffer,     gpu.strideNormal))     return false;
    if (!bind(flags & GDX_VERTEX_COLOR,        gpu.colorBuffer,      gpu.strideColor))      return false;
    if (!bind(flags & GDX_VERTEX_TEX1,         gpu.uv1Buffer,        gpu.strideUV1))        return false;
    if (!bind(flags & GDX_VERTEX_TEX2,         gpu.uv2Buffer,        gpu.strideUV2))        return false;
    if (!bind(flags & GDX_VERTEX_TANGENT,      gpu.tangentBuffer,    gpu.strideTangent))    return false;
    if (!bind(flags & GDX_VERTEX_BONE_INDICES, gpu.boneIndexBuffer,  gpu.strideBoneIndex))  return false;
    if (!bind(flags & GDX_VERTEX_BONE_WEIGHTS, gpu.boneWeightBuffer, gpu.strideBoneWeight)) return false;

    if (slot > 0u)
        m_context->IASetVertexBuffers(0, slot, buffers, strides, offsets);

    return true;
}

// ---------------------------------------------------------------------------
// BindMaterialTextures — t0=Albedo, t1=Normal, t2=ORM, t3=Emissive
// Fallback auf Default-Texturen wenn kein Handle gesetzt.
// ---------------------------------------------------------------------------
void GDXDX11RenderExecutor::BindMaterialTextures(
    const MaterialResource&                    mat,
    ResourceStore<GDXTextureResource, TextureTag>& texStore,
    TextureHandle defaultWhite,
    TextureHandle defaultNormal,
    TextureHandle defaultORM,
    TextureHandle defaultBlack)
{
    auto getSRV = [&](TextureHandle h, TextureHandle fallback) -> ID3D11ShaderResourceView*
    {
        const TextureHandle eff = h.IsValid() ? h : fallback;
        const GDXTextureResource* tex = texStore.Get(eff);
        if (!tex || !tex->srv) return nullptr;
        return static_cast<ID3D11ShaderResourceView*>(tex->srv);
    };

    ID3D11ShaderResourceView* srvs[4] =
    {
        getSRV(mat.albedoTex,   defaultWhite),   // t0
        getSRV(mat.normalTex,   defaultNormal),  // t1
        getSRV(mat.ormTex,      defaultORM),     // t2
        getSRV(mat.emissiveTex, defaultBlack),   // t3
    };

    m_context->PSSetShaderResources(0, 4, srvs);
}

// ---------------------------------------------------------------------------
// ExecuteShadowQueue — Depth-Only Shadow Pass.
// Kein Material-Check, kein Textur-Bind, nur World-Matrix + Position-Stream.
// Löst den Kernfehler: ExecuteQueue() übersprang alle Shadow-Draws weil
// cmd.material == Invalid() → mat == nullptr → continue.
// ---------------------------------------------------------------------------
void GDXDX11RenderExecutor::ExecuteShadowQueue(
    const RenderQueue&                          queue,
    ResourceStore<MeshAssetResource, MeshTag>&  meshStore,
    ResourceStore<GDXShaderResource, ShaderTag>& shaderStore)
{
    m_drawCalls  = 0u;
    m_lastShader = ShaderHandle::Invalid();

    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    for (const RenderCommand& cmd : queue.commands)
    {
        MeshAssetResource* mesh   = meshStore.Get(cmd.mesh);
        GDXShaderResource* shader = shaderStore.Get(cmd.shader);

        if (!mesh || !shader || !shader->IsValid())            continue;
        if (cmd.submeshIndex >= mesh->gpuBuffers.size())       continue;

        const GpuMeshBuffer& gpu = mesh->gpuBuffers[cmd.submeshIndex];
        if (!gpu.ready || !gpu.positionBuffer)                 continue;

        // Shader setzen (nur wenn gewechselt)
        if (cmd.shader != m_lastShader)
        {
            m_context->VSSetShader(static_cast<ID3D11VertexShader*>(shader->vertexShader), nullptr, 0);
            m_context->PSSetShader(static_cast<ID3D11PixelShader*> (shader->pixelShader),  nullptr, 0);
            m_context->IASetInputLayout(static_cast<ID3D11InputLayout*>(shader->inputLayout));
            m_lastShader = cmd.shader;
        }

        // Entity cbuffer b0 (World-Matrix für Shadow-VS)
        {
            Dx11EntityConstants ec = {};
            std::memcpy(ec.worldMatrix, &cmd.worldMatrix, 64);

            D3D11_MAPPED_SUBRESOURCE mapped = {};
            if (SUCCEEDED(m_context->Map(m_entityCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
            {
                std::memcpy(mapped.pData, &ec, sizeof(ec));
                m_context->Unmap(m_entityCB, 0);
            }
            m_context->VSSetConstantBuffers(0, 1, &m_entityCB);
        }

        // Nur POSITION-Stream (Shadow-Shader braucht nichts anderes)
        if (!BindVertexStreams(gpu, GDX_VERTEX_POSITION)) continue;

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

// ---------------------------------------------------------------------------
// ExecuteQueue
// ---------------------------------------------------------------------------
void GDXDX11RenderExecutor::ExecuteQueue(
    const RenderQueue&                              queue,
    ResourceStore<MeshAssetResource, MeshTag>&     meshStore,
    ResourceStore<MaterialResource,  MaterialTag>& matStore,
    ResourceStore<GDXShaderResource, ShaderTag>&   shaderStore,
    ResourceStore<GDXTextureResource,TextureTag>&  texStore,
    void* shadowSRV)
{
    m_drawCalls    = 0u;
    m_lastShader   = ShaderHandle::Invalid();
    m_lastMaterial = MaterialHandle::Invalid();

    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Shadow SRV an t16 binden (oder clearen wenn kein Shadow Pass)
    {
        auto* srv = static_cast<ID3D11ShaderResourceView*>(shadowSRV);
        m_context->PSSetShaderResources(16, 1, &srv);
    }

    for (const RenderCommand& cmd : queue.commands)
    {
        MeshAssetResource* mesh   = meshStore.Get(cmd.mesh);
        MaterialResource*  mat    = matStore.Get(cmd.material);
        GDXShaderResource* shader = shaderStore.Get(cmd.shader);

        if (!mesh || !mat || !shader || !shader->IsValid()) continue;
        if (cmd.submeshIndex >= mesh->gpuBuffers.size())    continue;

        const GpuMeshBuffer& gpu = mesh->gpuBuffers[cmd.submeshIndex];
        if (!gpu.ready || !gpu.positionBuffer)              continue;

        // --- Shader (State-Batching) ----------------------------------------
        if (cmd.shader != m_lastShader)
        {
            m_context->VSSetShader(static_cast<ID3D11VertexShader*>(shader->vertexShader), nullptr, 0);
            m_context->PSSetShader(static_cast<ID3D11PixelShader*> (shader->pixelShader),  nullptr, 0);
            m_context->IASetInputLayout(static_cast<ID3D11InputLayout*>(shader->inputLayout));
            m_lastShader   = cmd.shader;
            m_lastMaterial = MaterialHandle::Invalid();
        }

        // --- Material + Texturen (State-Batching) ---------------------------
        if (cmd.material != m_lastMaterial)
        {
            // cbuffer b2
            if (mat->gpuConstantBuffer)
            {
                auto* cb = static_cast<ID3D11Buffer*>(mat->gpuConstantBuffer);
                if (mat->cpuDirty)
                {
                    D3D11_MAPPED_SUBRESOURCE m = {};
                    if (SUCCEEDED(m_context->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
                    {
                        std::memcpy(m.pData, &mat->data, sizeof(MaterialData));
                        m_context->Unmap(cb, 0);
                    }
                    mat->cpuDirty = false;
                }
                m_context->PSSetConstantBuffers(2, 1, &cb);
            }

            // Texturen t0-t3
            BindMaterialTextures(*mat, texStore,
                defaultWhiteTex, defaultNormalTex,
                defaultORMTex,   defaultBlackTex);

            m_lastMaterial = cmd.material;
        }

        // --- Entity cbuffer b0 ----------------------------------------------
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

        // --- Vertex Streams --------------------------------------------------
        if (!BindVertexStreams(gpu, shader->vertexFlags)) continue;

        // --- Draw -----------------------------------------------------------
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
