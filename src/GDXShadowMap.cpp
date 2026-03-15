// GDXShadowMap.cpp — adaptiert aus OYNAME Dx11ShadowMap.cpp.
// Bewährte Werte beibehalten: DepthBias, SlopeScale, PCF-Sampler.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>

#include "GDXShadowMap.h"

bool GDXShadowMap::Create(ID3D11Device* device, uint32_t size)
{
    if (!device || size == 0) return false;

    Release();
    m_size = size;

    // --- Shadow Texture (R32_TYPELESS — wie OYNAME) -----------------------
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width             = size;
    texDesc.Height            = size;
    texDesc.MipLevels         = 1;
    texDesc.ArraySize         = 1;
    texDesc.Format            = DXGI_FORMAT_R32_TYPELESS;
    texDesc.SampleDesc.Count  = 1;
    texDesc.Usage             = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags         = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

    if (FAILED(device->CreateTexture2D(&texDesc, nullptr, &m_shadowTex)))
    {
        Release(); return false;
    }

    // --- DSV (D32_FLOAT) --------------------------------------------------
    D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format             = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension      = D3D11_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Texture2D.MipSlice = 0;

    if (FAILED(device->CreateDepthStencilView(m_shadowTex, &dsvDesc, &m_shadowDSV)))
    {
        Release(); return false;
    }

    // --- SRV (R32_FLOAT) für PS t16 ---------------------------------------
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                    = DXGI_FORMAT_R32_FLOAT;
    srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels       = 1;

    if (FAILED(device->CreateShaderResourceView(m_shadowTex, &srvDesc, &m_shadowSRV)))
    {
        Release(); return false;
    }

    // --- Rasterizer State: DepthBias + SlopeScale (wie OYNAME, bewährt) --
    D3D11_RASTERIZER_DESC rsDesc = {};
    rsDesc.FillMode              = D3D11_FILL_SOLID;
    rsDesc.CullMode              = D3D11_CULL_BACK;
    rsDesc.FrontCounterClockwise = FALSE;
    rsDesc.DepthBias             = 2000;    // verhindert Shadow-Acne
    rsDesc.DepthBiasClamp        = 0.01f;
    rsDesc.SlopeScaledDepthBias  = 0.5f;   // reduziert vs OYNAME 1.5 → weniger Offset
    rsDesc.DepthClipEnable       = TRUE;

    if (FAILED(device->CreateRasterizerState(&rsDesc, &m_shadowRS)))
    {
        Release(); return false;
    }

    return true;
}

void GDXShadowMap::BeginPass(ID3D11DeviceContext* ctx)
{
    if (!ctx || !m_shadowDSV) return;

    // Wichtig: Shadow-SRV vor Rebind als DSV lösen, sonst bleibt der Depth-Pass in DX11 ungültig.
    ID3D11ShaderResourceView* nullSRV = nullptr;
    ctx->PSSetShaderResources(16, 1, &nullSRV);

    // Kein RTV beim Shadow Pass — nur Depth
    ID3D11RenderTargetView* nullRTV = nullptr;
    ctx->OMSetRenderTargets(1, &nullRTV, m_shadowDSV);
    ctx->ClearDepthStencilView(m_shadowDSV, D3D11_CLEAR_DEPTH, 1.0f, 0);

    // Viewport = Shadow-Map-Größe
    D3D11_VIEWPORT vp = {};
    vp.Width    = static_cast<float>(m_size);
    vp.Height   = static_cast<float>(m_size);
    vp.MaxDepth = 1.0f;
    ctx->RSSetViewports(1, &vp);

    // Shadow Rasterizer: DepthBias aktiv
    ctx->RSSetState(m_shadowRS);
}

void GDXShadowMap::EndPass(ID3D11DeviceContext* ctx)
{
    if (!ctx) return;
    // SRV unbinden bevor Main-Pass sie als RTV nutzt
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
    safeRelease(m_shadowDSV);
    safeRelease(m_shadowTex);
    m_size = 0u;
}
