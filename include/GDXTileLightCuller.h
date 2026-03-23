#pragma once

#include "FrameData.h"
#include <cstdint>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Buffer;
struct ID3D11ComputeShader;
struct ID3D11ShaderResourceView;
struct ID3D11UnorderedAccessView;

// ---------------------------------------------------------------------------
// GDXTileLightCuller — Forward+ Tile-based Light Culling (DX11 Compute).
//
// Usage per frame:
//   1. culler.UploadLights(ctx, frame)     -- fill light StructuredBuffer
//   2. culler.Dispatch(ctx, frame, w, h)   -- run compute shader
//   3. culler.BindForPS(ctx)               -- bind SRVs to PS t20/t21/t22
//   4. [draw calls]
//   5. culler.UnbindFromPS(ctx)            -- cleanup
//
// Pixel shader reads:
//   t20 = StructuredBuffer<LightData>   gTileLights
//   t21 = StructuredBuffer<uint>        gLightIndexList
//   t22 = StructuredBuffer<uint2>       gLightGrid  (offset, count per tile)
//   b3  = TileCullInfo cbuffer          (tileCountX, lightCount, sceneAmbient)
// ---------------------------------------------------------------------------

static constexpr uint32_t GDX_TILE_SIZE           = 16u;
static constexpr uint32_t GDX_MAX_LIGHTS          = 256u;
static constexpr uint32_t GDX_MAX_LIGHTS_PER_TILE = 128u;

class GDXTileLightCuller
{
public:
    GDXTileLightCuller()  = default;
    ~GDXTileLightCuller() { Shutdown(); }

    bool Init(ID3D11Device* device);
    void Shutdown();

    // Resize internal buffers when the viewport changes.
    // Safe to call every frame — only reallocates when size actually changed.
    bool EnsureSize(ID3D11Device* device, uint32_t viewportW, uint32_t viewportH);

    // Upload CPU light data to the GPU StructuredBuffer.
    void UploadLights(ID3D11DeviceContext* ctx, const FrameData& frame);

    // Dispatch the tile culling compute shader.
    // Must be called after UploadLights and before BindForPS.
    void Dispatch(ID3D11DeviceContext* ctx, const FrameData& frame,
                  uint32_t viewportW, uint32_t viewportH);

    // Bind light SRVs + info cbuffer to the pixel shader.
    void BindForPS(ID3D11DeviceContext* ctx);
    void UploadPSInfoCB(ID3D11DeviceContext* ctx, const FrameData& frame);

    // Remove all PS bindings (call after draw calls are done).
    void UnbindFromPS(ID3D11DeviceContext* ctx);

    bool IsReady() const { return m_cs != nullptr && m_lightDataSRV != nullptr; }

    uint32_t TileCountX() const { return m_tileCountX; }
    uint32_t TileCountY() const { return m_tileCountY; }

private:
    bool CreateComputeShader(ID3D11Device* device);
    bool CreateStaticBuffers(ID3D11Device* device);
    bool CreateTileBuffers(ID3D11Device* device, uint32_t tileCountX, uint32_t tileCountY);
    void ReleaseTileBuffers();

    // --- Compute shader ---
    ID3D11ComputeShader* m_cs = nullptr;

    // --- CS constants (b0) ---
    ID3D11Buffer* m_csCB = nullptr;

    // --- Light data StructuredBuffer (written CPU, read CS t0 + PS t20) ---
    ID3D11Buffer*             m_lightDataBuf = nullptr;
    ID3D11ShaderResourceView* m_lightDataSRV = nullptr;

    // --- Light index list (CS u0 write, PS t21 read) ---
    ID3D11Buffer*              m_indexListBuf = nullptr;
    ID3D11UnorderedAccessView* m_indexListUAV = nullptr;
    ID3D11ShaderResourceView*  m_indexListSRV = nullptr;

    // --- Light grid: (offset, count) per tile (CS u1 write, PS t22 read) ---
    ID3D11Buffer*              m_gridBuf = nullptr;
    ID3D11UnorderedAccessView* m_gridUAV = nullptr;
    ID3D11ShaderResourceView*  m_gridSRV = nullptr;

    // --- Atomic counter (CS u2) ---
    ID3D11Buffer*              m_counterBuf = nullptr;
    ID3D11UnorderedAccessView* m_counterUAV = nullptr;

    // --- Tile info cbuffer for PS (b3, replaces old light cbuffer) ---
    ID3D11Buffer* m_psInfoCB = nullptr;

    uint32_t m_tileCountX  = 0u;
    uint32_t m_tileCountY  = 0u;
    uint32_t m_viewportW   = 0u;
    uint32_t m_viewportH   = 0u;
};

// ---------------------------------------------------------------------------
// PS cbuffer b3 layout (replaces old GDXDX11LightCBuffer).
// Must match TileInfo cbuffer in PixelShader.hlsl.
// ---------------------------------------------------------------------------
struct alignas(16) GDXTileInfoCBuffer
{
    float    sceneAmbient[3];
    uint32_t lightCount;
    uint32_t tileCountX;
    uint32_t tileCountY;
    float    pad0;
    float    pad1;
};
static_assert(sizeof(GDXTileInfoCBuffer) == 32);
