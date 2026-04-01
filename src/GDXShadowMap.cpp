// GDXShadowMap.cpp — CSM Shadow Map (Texture2DArray).
// Bewährte Werte beibehalten: DepthBias, SlopeScale, PCF-Sampler.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>

#include "GDXShadowMap.h"

bool GDXShadowMap::Create(ID3D11Device* device, uint32_t size, uint32_t cascadeCount)
{
    if (!device || size == 0 || cascadeCount == 0 || cascadeCount > kMaxCascades)
        return false;

    Release();
    m_size         = size;
    m_cascadeCount = cascadeCount;

    // --- Texture2DArray (R32_TYPELESS, ArraySize = cascadeCount) ----------
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width             = size;
    texDesc.Height            = size;
    texDesc.MipLevels         = 1;
    texDesc.ArraySize         = cascadeCount;
    texDesc.Format            = DXGI_FORMAT_R32_TYPELESS;
    texDesc.SampleDesc.Count  = 1;
    texDesc.Usage             = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags         = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

    if (FAILED(device->CreateTexture2D(&texDesc, nullptr, &m_shadowTex)))
    {
        Release(); return false;
    }

    // --- DSV pro Kaskade (D32_FLOAT, ein Slice je DSV) -------------------
    for (uint32_t i = 0; i < cascadeCount; ++i)
    {
        D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format                         = DXGI_FORMAT_D32_FLOAT;
        dsvDesc.ViewDimension                  = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
        dsvDesc.Texture2DArray.MipSlice        = 0;
        dsvDesc.Texture2DArray.FirstArraySlice = i;
        dsvDesc.Texture2DArray.ArraySize       = 1;

        if (FAILED(device->CreateDepthStencilView(m_shadowTex, &dsvDesc, &m_shadowDSV[i])))
        {
            Release(); return false;
        }
    }

    // --- SRV: ganzes Texture2DArray für PS t16 (R32_FLOAT) ---------------
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                              = DXGI_FORMAT_R32_FLOAT;
    srvDesc.ViewDimension                       = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    srvDesc.Texture2DArray.MostDetailedMip      = 0;
    srvDesc.Texture2DArray.MipLevels            = 1;
    srvDesc.Texture2DArray.FirstArraySlice      = 0;
    srvDesc.Texture2DArray.ArraySize            = cascadeCount;

    if (FAILED(device->CreateShaderResourceView(m_shadowTex, &srvDesc, &m_shadowSRV)))
    {
        Release(); return false;
    }

    // --- Rasterizer: DepthBias (wie OYNAME, bewährt) ----------------------
    D3D11_RASTERIZER_DESC rsDesc = {};
    rsDesc.FillMode              = D3D11_FILL_SOLID;
    rsDesc.CullMode              = D3D11_CULL_BACK;
    rsDesc.FrontCounterClockwise = FALSE;
    rsDesc.DepthBias             = 16;     // reduziert: weniger Peter-Panning / Schattenabloesung
    rsDesc.DepthBiasClamp        = 0.0f;
    rsDesc.SlopeScaledDepthBias  = 0.75f;   // konservativer Slope-Bias
    rsDesc.DepthClipEnable       = TRUE;

    if (FAILED(device->CreateRasterizerState(&rsDesc, &m_shadowRS)))
    {
        Release(); return false;
    }

    return true;
}

void GDXShadowMap::BeginPass(ID3D11DeviceContext* ctx, uint32_t cascade)
{
    if (!ctx || cascade >= m_cascadeCount || !m_shadowDSV[cascade])
        return;

    // SRV lösen bevor die Textur als DSV gebunden wird.
    ID3D11ShaderResourceView* nullSRV = nullptr;
    ctx->PSSetShaderResources(16, 1, &nullSRV);

    ID3D11RenderTargetView* nullRTV = nullptr;
    ctx->OMSetRenderTargets(1, &nullRTV, m_shadowDSV[cascade]);
    ctx->ClearDepthStencilView(m_shadowDSV[cascade], D3D11_CLEAR_DEPTH, 1.0f, 0);

    D3D11_VIEWPORT vp = {};
    vp.Width    = static_cast<float>(m_size);
    vp.Height   = static_cast<float>(m_size);
    vp.MaxDepth = 1.0f;
    ctx->RSSetViewports(1, &vp);

    ctx->RSSetState(m_shadowRS);
}

void GDXShadowMap::EndPass(ID3D11DeviceContext* ctx)
{
    if (!ctx) return;
    ID3D11ShaderResourceView* nullSRV = nullptr;
    ctx->PSSetShaderResources(16, 1, &nullSRV);
}

void GDXShadowMap::Release()
{
    auto safeRelease = [](auto*& ptr)
    {
        if (ptr) { ptr->Release(); ptr = nullptr; }
    };

    safeRelease(m_shadowRS);
    safeRelease(m_shadowSRV);
    for (auto& dsv : m_shadowDSV)
        safeRelease(dsv);
    safeRelease(m_shadowTex);
    m_size = 0u;
    m_cascadeCount = 0u;
}
