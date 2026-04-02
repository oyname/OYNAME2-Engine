// ============================================================
//  GDXDX11ParticleRenderer.cpp  --  DX11 GPU-instanced particle renderer
// ============================================================
#include "Particles/GDXDX11ParticleRenderer.h"
#include "GDXDX11ShaderCompiler.h"
#include "GDXMathHelpers.h"
#include "Core/Debug.h"
#include <d3d11.h>
#include <d3dcompiler.h>

// ---- cbuffer layout (must match ParticleVS.hlsl b0) --------
struct ParticleCBuffer
{
    float viewProj[16];  // row_major float4x4
    float camRight[3];
    float _pad0;
    float camUp[3];
    float _pad1;
};

// ---- static quad corners (local space, -1..+1) -------------
struct QuadVert { float x, y; };
static const QuadVert kQuadVerts[4] =
{
    { -1.0f,  1.0f },  // top-left
    {  1.0f,  1.0f },  // top-right
    { -1.0f, -1.0f },  // bottom-left
    {  1.0f, -1.0f },  // bottom-right
};
static const uint16_t kQuadIndices[6] = { 0, 1, 2,  1, 3, 2 };

// ============================================================
//  Init
// ============================================================
bool GDXDX11ParticleRenderer::Init(ID3D11Device*            device,
                                    ID3D11DeviceContext*      context,
                                    ID3D11ShaderResourceView* particleTexture,
                                    int                       maxParticles)
{
    m_device       = device;
    m_ctx          = context;
    m_texture      = particleTexture;
    m_maxInstances = maxParticles;

    if (!CreateBuffers(device, maxParticles)) return false;
    if (!CreateShaders(device))               return false;
    if (!CreateStates (device))               return false;
    return true;
}

// ============================================================
//  Render
// ============================================================
void GDXDX11ParticleRenderer::Render(const ParticleRenderSubmission& submission)
{
    if (!m_ctx || submission.Empty())
        return;

    const ParticleRenderContext& ctx = submission.context;

    // --- Update cbuffer ---
    {
        ParticleCBuffer cb = {};
        memcpy(cb.viewProj, &ctx.viewProj, sizeof(float) * 16);
        cb.camRight[0] = ctx.camRight.x;
        cb.camRight[1] = ctx.camRight.y;
        cb.camRight[2] = ctx.camRight.z;
        cb.camUp[0]    = ctx.camUp.x;
        cb.camUp[1]    = ctx.camUp.y;
        cb.camUp[2]    = ctx.camUp.z;

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (SUCCEEDED(m_ctx->Map(m_cbuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            memcpy(mapped.pData, &cb, sizeof(cb));
            m_ctx->Unmap(m_cbuffer, 0);
        }
    }

    m_ctx->VSSetShader(m_vs, nullptr, 0);
    m_ctx->PSSetShader(m_ps, nullptr, 0);
    m_ctx->IASetInputLayout(m_layout);
    m_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_ctx->VSSetConstantBuffers(0, 1, &m_cbuffer);
    m_ctx->PSSetShaderResources(0, 1, &m_texture);
    m_ctx->PSSetSamplers(0, 1, &m_sampler);
    m_ctx->OMSetDepthStencilState(m_depthState, 0);
    m_ctx->RSSetState(m_rsCullNone);

    UINT quadStride = sizeof(QuadVert), quadOffset = 0;
    m_ctx->IASetVertexBuffers(0, 1, &m_quadVB, &quadStride, &quadOffset);
    m_ctx->IASetIndexBuffer(m_quadIB, DXGI_FORMAT_R16_UINT, 0);

    float bf[4] = {};

    if (!submission.alphaInstances.empty())
    {
        m_ctx->OMSetBlendState(m_blendAlpha, bf, 0xFFFFFFFF);
        UploadAndDrawInstanced(0, submission.alphaInstances);
    }

    if (!submission.additiveInstances.empty())
    {
        m_ctx->OMSetBlendState(m_blendAdd, bf, 0xFFFFFFFF);
        UploadAndDrawInstanced(1, submission.additiveInstances);
    }

    ID3D11ShaderResourceView* nullSRV = nullptr;
    m_ctx->PSSetShaderResources(0, 1, &nullSRV);
}

bool GDXDX11ParticleRenderer::UploadAndDrawInstanced(int blendMode,
                                                       const std::vector<ParticleInstance>& instances)
{
    const int n = (int)instances.size();
    if (n <= 0)
    {
        m_lastUploadedCounts[blendMode] = 0;
        return true;
    }

    if (!EnsureInstanceCapacity(n))
        return false;

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(m_ctx->Map(m_instanceBuf[blendMode], 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        return false;
    memcpy(mapped.pData, instances.data(), static_cast<size_t>(n) * sizeof(ParticleInstance));
    m_ctx->Unmap(m_instanceBuf[blendMode], 0);
    m_lastUploadedCounts[blendMode] = n;

    // Bind instance buffer to slot 1
    UINT instStride = sizeof(ParticleInstance), instOffset = 0;
    m_ctx->IASetVertexBuffers(1, 1, &m_instanceBuf[blendMode], &instStride, &instOffset);

    m_ctx->DrawIndexedInstanced(6, (UINT)n, 0, 0, 0);
    return true;
}

bool GDXDX11ParticleRenderer::EnsureInstanceCapacity(int requiredInstances)
{
    if (requiredInstances <= m_maxInstances)
        return true;

    if (!m_device)
        return false;

    int newCapacity = (m_maxInstances > 0) ? m_maxInstances : 1;
    while (newCapacity < requiredInstances)
        newCapacity *= 2;

    ID3D11Buffer* newBuffers[2] = {};
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth      = (UINT)(newCapacity * sizeof(ParticleInstance));
    bd.Usage          = D3D11_USAGE_DYNAMIC;
    bd.BindFlags      = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    for (int i = 0; i < 2; ++i)
    {
        if (FAILED(m_device->CreateBuffer(&bd, nullptr, &newBuffers[i])))
        {
            for (int j = 0; j < 2; ++j)
                if (newBuffers[j]) newBuffers[j]->Release();
            DBWARN(GDX_SRC_LOC, "Particle instance buffer grow failed.");
            return false;
        }
    }

    for (int i = 0; i < 2; ++i)
    {
        if (m_instanceBuf[i]) m_instanceBuf[i]->Release();
        m_instanceBuf[i] = newBuffers[i];
    }

    Debug::Log(GDX_SRC_LOC, "Particle instance buffer grown to ", newCapacity, " instances.");
    m_maxInstances = newCapacity;
    return true;
}

// ============================================================
//  CreateBuffers
// ============================================================
bool GDXDX11ParticleRenderer::CreateBuffers(ID3D11Device* dev, int maxParticles)
{
    // Static unit-quad VB
    {
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth  = sizeof(kQuadVerts);
        bd.Usage      = D3D11_USAGE_IMMUTABLE;
        bd.BindFlags  = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA sr = { kQuadVerts };
        if (FAILED(dev->CreateBuffer(&bd, &sr, &m_quadVB))) return false;
    }

    // Static quad IB (uint16)
    {
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth  = sizeof(kQuadIndices);
        bd.Usage      = D3D11_USAGE_IMMUTABLE;
        bd.BindFlags  = D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA sr = { kQuadIndices };
        if (FAILED(dev->CreateBuffer(&bd, &sr, &m_quadIB))) return false;
    }

    // Dynamic instance buffers (one per blend mode)
    {
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth      = (UINT)(maxParticles * sizeof(ParticleInstance));
        bd.Usage          = D3D11_USAGE_DYNAMIC;
        bd.BindFlags      = D3D11_BIND_VERTEX_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        for (int i = 0; i < 2; ++i)
            if (FAILED(dev->CreateBuffer(&bd, nullptr, &m_instanceBuf[i]))) return false;
    }

    // Cbuffer (viewProj + camRight + camUp)
    {
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth      = sizeof(ParticleCBuffer);
        bd.Usage          = D3D11_USAGE_DYNAMIC;
        bd.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(dev->CreateBuffer(&bd, nullptr, &m_cbuffer))) return false;
    }

    return true;
}

// ============================================================
//  CreateShaders
// ============================================================
bool GDXDX11ParticleRenderer::CreateShaders(ID3D11Device* dev)
{
    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* psBlob = nullptr;

    if (!GDXDX11CompileShaderFromFile(L"ParticleVS.hlsl", "VSMain", "vs_5_0", &vsBlob))
        return false;

    if (!GDXDX11CompileShaderFromFile(L"ParticlePS.hlsl", "PSMain", "ps_5_0", &psBlob))
    {
        vsBlob->Release();
        return false;
    }

    HRESULT hr = S_OK;
    hr = dev->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_vs);
    if (FAILED(hr)) { vsBlob->Release(); psBlob->Release(); return false; }

    hr = dev->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_ps);
    if (FAILED(hr)) { vsBlob->Release(); psBlob->Release(); return false; }

    // Input layout:
    //   Slot 0 (per-vertex, step 1):  QuadVert  — corner offset
    //   Slot 1 (per-instance, step 1): ParticleInstance
    const D3D11_INPUT_ELEMENT_DESC ied[] =
    {
        // --- per-vertex (slot 0) ---
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0,  0, D3D11_INPUT_PER_VERTEX_DATA,   0 },

        // --- per-instance (slot 1) ---
        { "POSITION", 1, DXGI_FORMAT_R32G32B32_FLOAT,    1,  0, D3D11_INPUT_PER_INSTANCE_DATA, 1 },  // pos
        { "TEXCOORD", 1, DXGI_FORMAT_R32_FLOAT,          1, 12, D3D11_INPUT_PER_INSTANCE_DATA, 1 },  // size
        { "TEXCOORD", 2, DXGI_FORMAT_R32G32B32_FLOAT,    1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1 },  // velDir
        { "TEXCOORD", 3, DXGI_FORMAT_R32_FLOAT,          1, 28, D3D11_INPUT_PER_INSTANCE_DATA, 1 },  // rot
        { "TEXCOORD", 4, DXGI_FORMAT_R32G32_FLOAT,       1, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1 },  // u, v
        { "TEXCOORD", 5, DXGI_FORMAT_R32_FLOAT,          1, 40, D3D11_INPUT_PER_INSTANCE_DATA, 1 },  // uvUnit
        { "TEXCOORD", 6, DXGI_FORMAT_R32_UINT,           1, 44, D3D11_INPUT_PER_INSTANCE_DATA, 1 },  // flags
        { "COLOR",    1, DXGI_FORMAT_R8G8B8A8_UNORM,    1, 48, D3D11_INPUT_PER_INSTANCE_DATA, 1 },  // rgba
        { "TEXCOORD", 7, DXGI_FORMAT_R32_FLOAT,          1, 52, D3D11_INPUT_PER_INSTANCE_DATA, 1 },  // pivotOffset
    };

    hr = dev->CreateInputLayout(ied, (UINT)std::size(ied),
                                vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &m_layout);
    vsBlob->Release();
    psBlob->Release();
    return SUCCEEDED(hr);
}

// ============================================================
//  CreateStates
// ============================================================
bool GDXDX11ParticleRenderer::CreateStates(ID3D11Device* dev)
{
    // Alpha blend
    {
        D3D11_BLEND_DESC bd = {};
        auto& rt = bd.RenderTarget[0];
        rt.BlendEnable           = TRUE;
        rt.SrcBlend              = D3D11_BLEND_SRC_ALPHA;
        rt.DestBlend             = D3D11_BLEND_INV_SRC_ALPHA;
        rt.BlendOp               = D3D11_BLEND_OP_ADD;
        rt.SrcBlendAlpha         = D3D11_BLEND_ONE;
        rt.DestBlendAlpha        = D3D11_BLEND_ZERO;
        rt.BlendOpAlpha          = D3D11_BLEND_OP_ADD;
        rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (FAILED(dev->CreateBlendState(&bd, &m_blendAlpha))) return false;
    }

    // Additive blend
    {
        D3D11_BLEND_DESC bd = {};
        auto& rt = bd.RenderTarget[0];
        rt.BlendEnable           = TRUE;
        rt.SrcBlend              = D3D11_BLEND_SRC_ALPHA;
        rt.DestBlend             = D3D11_BLEND_ONE;
        rt.BlendOp               = D3D11_BLEND_OP_ADD;
        rt.SrcBlendAlpha         = D3D11_BLEND_ONE;
        rt.DestBlendAlpha        = D3D11_BLEND_ZERO;
        rt.BlendOpAlpha          = D3D11_BLEND_OP_ADD;
        rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (FAILED(dev->CreateBlendState(&bd, &m_blendAdd))) return false;
    }

    // Depth: read-only
    {
        D3D11_DEPTH_STENCIL_DESC dsd = {};
        dsd.DepthEnable    = TRUE;
        dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        dsd.DepthFunc      = D3D11_COMPARISON_LESS;
        if (FAILED(dev->CreateDepthStencilState(&dsd, &m_depthState))) return false;
    }

    // Bilinear wrap sampler
    {
        D3D11_SAMPLER_DESC sd = {};
        sd.Filter         = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        sd.MaxAnisotropy  = 1;
        sd.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
        sd.MaxLOD         = D3D11_FLOAT32_MAX;
        if (FAILED(dev->CreateSamplerState(&sd, &m_sampler))) return false;
    }

    // CullNone rasterizer (billboards are double-sided)
    {
        D3D11_RASTERIZER_DESC rd = {};
        rd.FillMode        = D3D11_FILL_SOLID;
        rd.CullMode        = D3D11_CULL_NONE;
        rd.DepthClipEnable = TRUE;
        if (FAILED(dev->CreateRasterizerState(&rd, &m_rsCullNone))) return false;
    }

    return true;
}

// ============================================================
//  Shutdown / Destructor
// ============================================================
GDXDX11ParticleRenderer::~GDXDX11ParticleRenderer() { Shutdown(); }

void GDXDX11ParticleRenderer::Shutdown()
{
    auto R = [](auto*& p){ if (p) { p->Release(); p = nullptr; } };
    R(m_quadVB); R(m_quadIB);
    for (int i = 0; i < 2; ++i) R(m_instanceBuf[i]);
    R(m_cbuffer);
    R(m_blendAlpha); R(m_blendAdd);
    R(m_depthState); R(m_rsCullNone);
    R(m_layout); R(m_vs); R(m_ps);
    R(m_sampler);
}
