// GDXSamplerCache.cpp — 4 geteilte Sampler einmalig erstellen.
// Verbesserung: OYNAME erstellte pro Textur einen eigenen SamplerState.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>

#include "GDXSamplerCache.h"

bool GDXSamplerCache::Init(ID3D11Device* device)
{
    if (!device) return false;

    // --- s0: Linear Wrap (Standard für Albedo, Normal, ORM, Emissive) ----
    {
        D3D11_SAMPLER_DESC desc = {};
        desc.Filter         = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        desc.AddressU       = D3D11_TEXTURE_ADDRESS_WRAP;
        desc.AddressV       = D3D11_TEXTURE_ADDRESS_WRAP;
        desc.AddressW       = D3D11_TEXTURE_ADDRESS_WRAP;
        desc.MaxLOD         = D3D11_FLOAT32_MAX;
        desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
        if (FAILED(device->CreateSamplerState(&desc, &m_linearWrap)))
            return false;
    }

    // --- s1: Linear Clamp (UI, Decals, Render-Texturen) ------------------
    {
        D3D11_SAMPLER_DESC desc = {};
        desc.Filter         = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        desc.AddressU       = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.AddressV       = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.AddressW       = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.MaxLOD         = D3D11_FLOAT32_MAX;
        desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
        if (FAILED(device->CreateSamplerState(&desc, &m_linearClamp)))
            return false;
    }

    // --- s2: Anisotropic 8x (hochwertige Oberflächen) --------------------
    {
        D3D11_SAMPLER_DESC desc = {};
        desc.Filter            = D3D11_FILTER_ANISOTROPIC;
        desc.AddressU          = D3D11_TEXTURE_ADDRESS_WRAP;
        desc.AddressV          = D3D11_TEXTURE_ADDRESS_WRAP;
        desc.AddressW          = D3D11_TEXTURE_ADDRESS_WRAP;
        desc.MaxAnisotropy     = 16;
        desc.MaxLOD            = D3D11_FLOAT32_MAX;
        desc.ComparisonFunc    = D3D11_COMPARISON_NEVER;
        if (FAILED(device->CreateSamplerState(&desc, &m_anisotropic)))
            return false;
    }

    // --- s7: PCF Comparison (Shadow Map — OYNAME-kompatibel) -------------
    {
        D3D11_SAMPLER_DESC desc = {};
        desc.Filter             = D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
        desc.AddressU           = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.AddressV           = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.AddressW           = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.ComparisonFunc     = D3D11_COMPARISON_LESS;
        desc.BorderColor[0]     = 1.0f;
        desc.BorderColor[1]     = 1.0f;
        desc.BorderColor[2]     = 1.0f;
        desc.BorderColor[3]     = 1.0f;
        desc.MaxLOD             = D3D11_FLOAT32_MAX;
        if (FAILED(device->CreateSamplerState(&desc, &m_pcfComparison)))
            return false;
    }

    return true;
}

void GDXSamplerCache::BindAll(ID3D11DeviceContext* ctx) const
{
    if (!ctx) return;

    // Slots 0-2: Linear Wrap, Linear Clamp, Anisotropic
    ID3D11SamplerState* samplers03[3] = { m_linearWrap, m_linearClamp, m_anisotropic };
    ctx->PSSetSamplers(0, 3, samplers03);

    // Slot 7: PCF Comparison (Shadow — wie OYNAME s7)
    ctx->PSSetSamplers(7, 1, &m_pcfComparison);
}

void GDXSamplerCache::Shutdown()
{
    auto safeRelease = [](ID3D11SamplerState*& s)
    {
        if (s) { s->Release(); s = nullptr; }
    };

    safeRelease(m_linearWrap);
    safeRelease(m_linearClamp);
    safeRelease(m_anisotropic);
    safeRelease(m_pcfComparison);
}
