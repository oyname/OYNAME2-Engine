#pragma comment(lib, "d3dcompiler.lib")

#include "GDXTileLightCuller.h"
#include "Core/Debug.h"
#include "Core/GDXMath.h"
#include "Core/GDXMathHelpers.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>

#include <cstring>
#include <algorithm>
#include <array>
#include <filesystem>
#include <vector>

// ---------------------------------------------------------------------------
// CS cbuffer layout (must match TileCullCB in TileLightCullCS.hlsl)
// ---------------------------------------------------------------------------
struct alignas(16) TileCullCB
{
    float    view[16];
    float    proj[16];
    float    projInv[16];
    float    viewportW;
    float    viewportH;
    uint32_t lightCount;
    uint32_t tileCountX;
};

// CS-side LightData (matches PixelShader LightData struct)
struct alignas(16) CSLightData
{
    float position[4];
    float direction[4];
    float diffuse[4];
    float innerCosAngle;
    float outerCosAngle;
    float pad0;
    float pad1;
};
static_assert(sizeof(CSLightData) == 64);

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static ID3D11Buffer* CreateStructuredBuffer(
    ID3D11Device* device, uint32_t elementSize, uint32_t elementCount,
    bool uav, const void* initData = nullptr)
{
    D3D11_BUFFER_DESC d{};
    d.ByteWidth           = elementSize * elementCount;
    d.Usage               = D3D11_USAGE_DEFAULT;
    d.BindFlags           = D3D11_BIND_SHADER_RESOURCE | (uav ? D3D11_BIND_UNORDERED_ACCESS : 0u);
    d.MiscFlags           = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    d.StructureByteStride = elementSize;

    D3D11_SUBRESOURCE_DATA init{ initData, 0, 0 };
    ID3D11Buffer* buf = nullptr;
    if (FAILED(device->CreateBuffer(&d, initData ? &init : nullptr, &buf)))
        return nullptr;
    return buf;
}

static ID3D11ShaderResourceView* CreateStructuredSRV(
    ID3D11Device* device, ID3D11Buffer* buf, uint32_t count)
{
    D3D11_SHADER_RESOURCE_VIEW_DESC d{};
    d.Format              = DXGI_FORMAT_UNKNOWN;
    d.ViewDimension       = D3D11_SRV_DIMENSION_BUFFER;
    d.Buffer.FirstElement = 0;
    d.Buffer.NumElements  = count;
    ID3D11ShaderResourceView* srv = nullptr;
    device->CreateShaderResourceView(buf, &d, &srv);
    return srv;
}

static ID3D11UnorderedAccessView* CreateStructuredUAV(
    ID3D11Device* device, ID3D11Buffer* buf, uint32_t count, bool counter = false)
{
    D3D11_UNORDERED_ACCESS_VIEW_DESC d{};
    d.Format              = DXGI_FORMAT_UNKNOWN;
    d.ViewDimension       = D3D11_UAV_DIMENSION_BUFFER;
    d.Buffer.FirstElement = 0;
    d.Buffer.NumElements  = count;
    d.Buffer.Flags        = counter ? D3D11_BUFFER_UAV_FLAG_COUNTER : 0u;
    ID3D11UnorderedAccessView* uav = nullptr;
    device->CreateUnorderedAccessView(buf, &d, &uav);
    return uav;
}

static ID3D11Buffer* CreateDynamicCB(ID3D11Device* device, uint32_t bytes)
{
    D3D11_BUFFER_DESC d{};
    d.ByteWidth      = (bytes + 15u) & ~15u;
    d.Usage          = D3D11_USAGE_DYNAMIC;
    d.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ID3D11Buffer* buf = nullptr;
    device->CreateBuffer(&d, nullptr, &buf);
    return buf;
}

template<typename T>
static void UploadDynamicCB(ID3D11DeviceContext* ctx, ID3D11Buffer* buf, const T& data)
{
    D3D11_MAPPED_SUBRESOURCE m{};
    if (SUCCEEDED(ctx->Map(buf, 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
    {
        std::memcpy(m.pData, &data, sizeof(T));
        ctx->Unmap(buf, 0);
    }
}

// Simple 4x4 matrix inverse (for projection matrices — no general case needed)
static void InverseProjection(const float src[16], float dst[16])
{
    // For a standard row-major perspective projection matrix:
    // [m00  0    0    0 ]
    // [0    m11  0    0 ]
    // [0    0    m22  m23]
    // [0    0    m32  0 ]
    // Inverse is analytical:
    const float m00 = src[0],  m11 = src[5];
    const float m22 = src[10], m23 = src[11];
    const float m32 = src[14];

    std::memset(dst, 0, 64);
    if (std::abs(m00) < 1e-9f || std::abs(m11) < 1e-9f) return;

    dst[0]  = 1.0f / m00;
    dst[5]  = 1.0f / m11;
    dst[11] = 1.0f / m32;
    dst[14] = 1.0f / m23;
    dst[15] = -m22 / (m23 * m32);
}


static std::filesystem::path GetExeDir()
{
    std::array<wchar_t, 4096> buf{};
    const DWORD len = GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
    if (len == 0 || len >= buf.size()) return std::filesystem::current_path();
    return std::filesystem::path(buf.data()).parent_path();
}

static std::filesystem::path FindShaderPath(const std::wstring& file)
{
    const auto exe = GetExeDir();
    const std::vector<std::filesystem::path> candidates =
    {
        exe / L"shader" / file,
        exe / L".." / L"shader" / file,
        exe / L".." / L".." / L"shader" / file,
        exe / L".." / L".." / L".." / L"shader" / file,
        std::filesystem::current_path() / L"shader" / file,
        std::filesystem::current_path() / L".." / L"shader" / file,
    };

    for (const auto& c : candidates)
    {
        std::error_code ec;
        if (std::filesystem::exists(c, ec))
            return std::filesystem::weakly_canonical(c, ec);
    }
    return {};
}

// ---------------------------------------------------------------------------
// Init / Shutdown
// ---------------------------------------------------------------------------
bool GDXTileLightCuller::Init(ID3D11Device* device)
{
    if (!device) return false;
    if (!CreateComputeShader(device)) return false;
    if (!CreateStaticBuffers(device)) return false;
    return true;
}

void GDXTileLightCuller::Shutdown()
{
    ReleaseTileBuffers();

    auto sr = [](auto*& p) { if (p) { p->Release(); p = nullptr; } };
    sr(m_lightDataSRV);
    sr(m_lightDataBuf);
    sr(m_csCB);
    sr(m_psInfoCB);
    sr(m_cs);
}

// ---------------------------------------------------------------------------
// Compile compute shader from file at runtime
// ---------------------------------------------------------------------------
bool GDXTileLightCuller::CreateComputeShader(ID3D11Device* device)
{
    ID3DBlob* blob  = nullptr;
    ID3DBlob* error = nullptr;

    const auto shaderPath = FindShaderPath(L"TileLightCullCS.hlsl");
    if (shaderPath.empty())
    {
        Debug::LogError(GDX_SRC_LOC, L"TileLightCullCS.hlsl not found in shader search paths");
        return false;
    }

    const UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
    HRESULT hr = D3DCompileFromFile(
        shaderPath.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "main", "cs_5_0", flags, 0, &blob, &error);

    if (FAILED(hr))
    {
        if (error)
        {
            Debug::LogError(GDX_SRC_LOC,
                L"TileLightCullCS compile error in ", shaderPath.c_str(), L": ",
                static_cast<const char*>(error->GetBufferPointer()));
            error->Release();
        }
        else
        {
            Debug::LogError(GDX_SRC_LOC, L"TileLightCullCS.hlsl not found");
        }
        return false;
    }

    hr = device->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(),
                                     nullptr, &m_cs);
    blob->Release();
    if (error) error->Release();

    return SUCCEEDED(hr);
}

// ---------------------------------------------------------------------------
// Static buffers (size-independent)
// ---------------------------------------------------------------------------
bool GDXTileLightCuller::CreateStaticBuffers(ID3D11Device* device)
{
    // Light data StructuredBuffer — MAX_LIGHTS entries
    m_lightDataBuf = CreateStructuredBuffer(device, sizeof(CSLightData), GDX_MAX_LIGHTS, false);
    if (!m_lightDataBuf) return false;

    m_lightDataSRV = CreateStructuredSRV(device, m_lightDataBuf, GDX_MAX_LIGHTS);
    if (!m_lightDataSRV) return false;

    m_csCB    = CreateDynamicCB(device, sizeof(TileCullCB));
    m_psInfoCB = CreateDynamicCB(device, sizeof(GDXTileInfoCBuffer));

    return m_csCB && m_psInfoCB;
}

// ---------------------------------------------------------------------------
// Tile-size-dependent buffers
// ---------------------------------------------------------------------------
bool GDXTileLightCuller::EnsureSize(ID3D11Device* device, uint32_t w, uint32_t h)
{
    if (!device) return false;

    const uint32_t tcx = (w  + GDX_TILE_SIZE - 1u) / GDX_TILE_SIZE;
    const uint32_t tcy = (h  + GDX_TILE_SIZE - 1u) / GDX_TILE_SIZE;

    if (tcx == m_tileCountX && tcy == m_tileCountY)
        return true;  // no change

    ReleaseTileBuffers();
    return CreateTileBuffers(device, tcx, tcy);
}

bool GDXTileLightCuller::CreateTileBuffers(
    ID3D11Device* device, uint32_t tcx, uint32_t tcy)
{
    const uint32_t tileCount     = tcx * tcy;
    const uint32_t indexListSize = tileCount * GDX_MAX_LIGHTS_PER_TILE;

    // Light index list
    m_indexListBuf = CreateStructuredBuffer(device, sizeof(uint32_t), indexListSize, true);
    if (!m_indexListBuf) return false;
    m_indexListUAV = CreateStructuredUAV(device, m_indexListBuf, indexListSize);
    m_indexListSRV = CreateStructuredSRV(device, m_indexListBuf, indexListSize);

    // Light grid: (offset, count) per tile — 2x uint32 = 8 bytes per tile
    m_gridBuf = CreateStructuredBuffer(device, sizeof(uint32_t) * 2u, tileCount, true);
    if (!m_gridBuf) return false;
    m_gridUAV = CreateStructuredUAV(device, m_gridBuf, tileCount);
    m_gridSRV = CreateStructuredSRV(device, m_gridBuf, tileCount);

    // Atomic counter (single uint)
    m_counterBuf = CreateStructuredBuffer(device, sizeof(uint32_t), 1u, true);
    if (!m_counterBuf) return false;
    m_counterUAV = CreateStructuredUAV(device, m_counterBuf, 1u);

    if (!m_indexListUAV || !m_indexListSRV ||
        !m_gridUAV      || !m_gridSRV      || !m_counterUAV)
        return false;

    m_tileCountX = tcx;
    m_tileCountY = tcy;
    return true;
}

void GDXTileLightCuller::ReleaseTileBuffers()
{
    auto sr = [](auto*& p) { if (p) { p->Release(); p = nullptr; } };
    sr(m_counterUAV);  sr(m_counterBuf);
    sr(m_gridSRV);     sr(m_gridUAV);    sr(m_gridBuf);
    sr(m_indexListSRV); sr(m_indexListUAV); sr(m_indexListBuf);
    m_tileCountX = m_tileCountY = 0u;
}

// ---------------------------------------------------------------------------
// UploadLights — CPU → GPU StructuredBuffer
// ---------------------------------------------------------------------------
void GDXTileLightCuller::UploadLights(ID3D11DeviceContext* ctx, const FrameData& frame)
{
    if (!ctx || !m_lightDataBuf) return;

    const uint32_t count = (std::min)(frame.lightCount, GDX_MAX_LIGHTS);
    if (count == 0u) return;

    CSLightData upload[GDX_MAX_LIGHTS] = {};
    for (uint32_t i = 0u; i < count; ++i)
    {
        const LightEntry& src = frame.lights[i];
        CSLightData&       dst = upload[i];
        dst.position[0]  = src.position.x;
        dst.position[1]  = src.position.y;
        dst.position[2]  = src.position.z;
        dst.position[3]  = src.position.w;
        dst.direction[0] = src.direction.x;
        dst.direction[1] = src.direction.y;
        dst.direction[2] = src.direction.z;
        dst.direction[3] = src.direction.w;
        dst.diffuse[0]   = src.diffuse.x;
        dst.diffuse[1]   = src.diffuse.y;
        dst.diffuse[2]   = src.diffuse.z;
        dst.diffuse[3]   = src.diffuse.w;
        dst.innerCosAngle = src.innerCosAngle;
        dst.outerCosAngle = src.outerCosAngle;
    }

    ctx->UpdateSubresource(m_lightDataBuf, 0, nullptr, upload,
        sizeof(CSLightData) * count, 0);
}

// ---------------------------------------------------------------------------
// Dispatch
// ---------------------------------------------------------------------------
void GDXTileLightCuller::Dispatch(
    ID3D11DeviceContext* ctx,
    const FrameData& frame,
    uint32_t viewportW, uint32_t viewportH)
{
    if (!ctx || !m_cs || !m_tileCountX) return;

    // --- Reset counter ---
    const uint32_t zero = 0u;
    ctx->UpdateSubresource(m_counterBuf, 0, nullptr, &zero, sizeof(zero), 0);

    // --- Clear light grid ---
    const uint32_t zeros[2] = { 0u, 0u };
    // (brief approach: zero via UAV clear)
    const UINT clearVal[4] = { 0, 0, 0, 0 };
    ctx->ClearUnorderedAccessViewUint(m_gridUAV, clearVal);
    ctx->ClearUnorderedAccessViewUint(m_counterUAV, clearVal);

    // --- Upload CS cbuffer ---
    TileCullCB cb{};
    std::memcpy(cb.view, &frame.viewMatrix, 64);
    std::memcpy(cb.proj, &frame.projMatrix, 64);
    InverseProjection(cb.proj, cb.projInv);
    cb.viewportW   = static_cast<float>(viewportW);
    cb.viewportH   = static_cast<float>(viewportH);
    cb.lightCount  = (std::min)(frame.lightCount, GDX_MAX_LIGHTS);
    cb.tileCountX  = m_tileCountX;
    UploadDynamicCB(ctx, m_csCB, cb);

    // --- Bind resources ---
    ctx->CSSetShader(m_cs, nullptr, 0);

    ID3D11Buffer* cbs[1] = { m_csCB };
    ctx->CSSetConstantBuffers(0, 1, cbs);

    ID3D11ShaderResourceView* srvs[1] = { m_lightDataSRV };
    ctx->CSSetShaderResources(0, 1, srvs);

    ID3D11UnorderedAccessView* uavs[3] = {
        m_indexListUAV, m_gridUAV, m_counterUAV };
    const UINT initCounts[3] = { 0, 0, 0 };
    ctx->CSSetUnorderedAccessViews(0, 3, uavs, initCounts);

    // --- Dispatch one group per tile ---
    ctx->Dispatch(m_tileCountX, m_tileCountY, 1u);

    // --- Unbind CS resources ---
    ctx->CSSetShader(nullptr, nullptr, 0);
    ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
    ctx->CSSetShaderResources(0, 1, nullSRV);
    ID3D11UnorderedAccessView* nullUAV[3] = { nullptr, nullptr, nullptr };
    ctx->CSSetUnorderedAccessViews(0, 3, nullUAV, nullptr);
}

// ---------------------------------------------------------------------------
// BindForPS / UnbindFromPS
// ---------------------------------------------------------------------------
void GDXTileLightCuller::BindForPS(ID3D11DeviceContext* ctx)
{
    if (!ctx) return;

    // t20 = light data, t21 = index list, t22 = grid
    ID3D11ShaderResourceView* srvs[3] = {
        m_lightDataSRV, m_indexListSRV, m_gridSRV };
    ctx->PSSetShaderResources(20u, 3u, srvs);

    // b3 = tile info (replaces old light cbuffer slot)
    ID3D11Buffer* cbs[1] = { m_psInfoCB };
    ctx->PSSetConstantBuffers(3u, 1u, cbs);
}

void GDXTileLightCuller::UnbindFromPS(ID3D11DeviceContext* ctx)
{
    if (!ctx) return;
    ID3D11ShaderResourceView* null3[3] = { nullptr, nullptr, nullptr };
    ctx->PSSetShaderResources(20u, 3u, null3);
}

void GDXTileLightCuller::UploadPSInfoCB(
    ID3D11DeviceContext* ctx, const FrameData& frame)
{
    GDXTileInfoCBuffer info{};
    info.sceneAmbient[0] = frame.sceneAmbient.x;
    info.sceneAmbient[1] = frame.sceneAmbient.y;
    info.sceneAmbient[2] = frame.sceneAmbient.z;
    info.lightCount  = (std::min)(frame.lightCount, GDX_MAX_LIGHTS);
    info.tileCountX  = m_tileCountX;
    info.tileCountY  = m_tileCountY;
    UploadDynamicCB(ctx, m_psInfoCB, info);
}
