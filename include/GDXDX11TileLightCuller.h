#pragma once

#include "FrameData.h"
#include <cstdint>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Buffer;
struct ID3D11ComputeShader;
struct ID3D11ShaderResourceView;
struct ID3D11UnorderedAccessView;

static constexpr uint32_t GDX_TILE_SIZE           = 16u;
static constexpr uint32_t GDX_MAX_LIGHTS          = 256u;
static constexpr uint32_t GDX_MAX_LIGHTS_PER_TILE = 128u;

class GDXDX11TileLightCuller
{
public:
    GDXDX11TileLightCuller()  = default;
    ~GDXDX11TileLightCuller() { Shutdown(); }

    bool Init(ID3D11Device* device, ID3D11ComputeShader* computeShader);
    void Shutdown();

    // Ensures enough capacity for the requested viewport.
    // Reallocates only when a larger tile grid is needed.
    bool EnsureSize(ID3D11Device* device, uint32_t viewportW, uint32_t viewportH);

    void UploadLights(ID3D11DeviceContext* ctx, const FrameData& frame);
    void Dispatch(ID3D11DeviceContext* ctx, const FrameData& frame,
                  uint32_t viewportW, uint32_t viewportH);
    void BindForPS(ID3D11DeviceContext* ctx);
    void UploadPSInfoCB(ID3D11DeviceContext* ctx, const FrameData& frame);
    void UnbindFromPS(ID3D11DeviceContext* ctx);

    bool IsReady() const { return m_cs != nullptr && m_lightDataSRV != nullptr; }

    // Active tile counts for the current viewport.
    uint32_t TileCountX() const { return m_activeTileCountX; }
    uint32_t TileCountY() const { return m_activeTileCountY; }

    // Allocated capacity in tiles.
    uint32_t TileCapacityX() const { return m_capacityTileCountX; }
    uint32_t TileCapacityY() const { return m_capacityTileCountY; }

private:
    bool CreateStaticBuffers(ID3D11Device* device);
    bool CreateTileBuffers(ID3D11Device* device, uint32_t tileCountX, uint32_t tileCountY);
    void ReleaseTileBuffers();

    ID3D11ComputeShader* m_cs = nullptr;
    ID3D11Buffer* m_csCB = nullptr;

    ID3D11Buffer*             m_lightDataBuf = nullptr;
    ID3D11ShaderResourceView* m_lightDataSRV = nullptr;

    ID3D11Buffer*              m_indexListBuf = nullptr;
    ID3D11UnorderedAccessView* m_indexListUAV = nullptr;
    ID3D11ShaderResourceView*  m_indexListSRV = nullptr;

    ID3D11Buffer*              m_gridBuf = nullptr;
    ID3D11UnorderedAccessView* m_gridUAV = nullptr;
    ID3D11ShaderResourceView*  m_gridSRV = nullptr;

    ID3D11Buffer*              m_counterBuf = nullptr;
    ID3D11UnorderedAccessView* m_counterUAV = nullptr;

    ID3D11Buffer* m_psInfoCB = nullptr;

    uint32_t m_capacityTileCountX = 0u;
    uint32_t m_capacityTileCountY = 0u;
    uint32_t m_activeTileCountX   = 0u;
    uint32_t m_activeTileCountY   = 0u;
    uint32_t m_viewportW          = 0u;
    uint32_t m_viewportH          = 0u;
};

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
