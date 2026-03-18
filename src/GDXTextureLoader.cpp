// GDXTextureLoader.cpp — DX11 Textur-Lader mit sRGB + Mip-Generierung.
//
// Verbesserung gegenüber erster Version:
//   - sRGB-Unterscheidung: Albedo/Emissive → SRGB, Normal/ORM → LINEAR
//   - Automatische Mip-Generierung via GenerateMips()
//     → kein Flimmern bei Distanz, korrekte Anisotropic-Filterung
//   - USAGE_DEFAULT + BindFlags RENDER_TARGET für GenerateMips()-Kompatibilität

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>

#define STB_IMAGE_IMPLEMENTATION
#include "..//third_party/stb_image.h"

#include "GDXTextureLoader.h"

// ---------------------------------------------------------------------------
// Interne Hilfsfunktion: Texture2D + SRV mit Mips erstellen
// ---------------------------------------------------------------------------
static bool CreateSRVWithMips(
    ID3D11Device*             device,
    ID3D11DeviceContext*      ctx,
    const uint8_t*            data,
    int                       w, int h,
    DXGI_FORMAT               fmt,          // z.B. SRGB oder LINEAR
    ID3D11ShaderResourceView** outSRV)
{
    if (!device || !data || !outSRV) return false;

    // Mip-Anzahl berechnen (log2 der größten Dimension + 1)
    UINT mipLevels = 1u;
    {
        UINT s = static_cast<UINT>(w > h ? w : h);
        while (s > 1u) { s >>= 1u; ++mipLevels; }
    }

    // Textur: USAGE_DEFAULT + RENDER_TARGET für GenerateMips()
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width            = static_cast<UINT>(w);
    desc.Height           = static_cast<UINT>(h);
    desc.MipLevels        = mipLevels;
    desc.ArraySize        = 1;
    desc.Format           = fmt;
    desc.SampleDesc.Count = 1;
    desc.Usage            = D3D11_USAGE_DEFAULT;
    desc.BindFlags        = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags        = D3D11_RESOURCE_MISC_GENERATE_MIPS;

    ID3D11Texture2D* tex = nullptr;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, &tex)) || !tex)
        return false;

    // Mip 0 mit Pixeldaten füllen
    ctx->UpdateSubresource(tex, 0, nullptr, data, static_cast<UINT>(w * 4), 0);

    // SRV erstellen
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                    = fmt;
    srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels       = static_cast<UINT>(-1); // alle Mips

    HRESULT hr = device->CreateShaderResourceView(tex, &srvDesc, outSRV);
    if (SUCCEEDED(hr) && *outSRV)
        ctx->GenerateMips(*outSRV);  // Mips automatisch generieren

    tex->Release();
    return SUCCEEDED(hr) && *outSRV;
}

// ---------------------------------------------------------------------------
// Öffentliche API: Textur aus Datei laden
//
// isSRGB=true  → Albedo, Emissive  (DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
// isSRGB=false → Normal, ORM       (DXGI_FORMAT_R8G8B8A8_UNORM)
// ---------------------------------------------------------------------------
bool GDXTextureLoader_LoadFromFile(
    ID3D11Device*        device,
    ID3D11DeviceContext* ctx,
    const wchar_t*       filename,
    GDXTextureResource&  outResource,
    bool                 isSRGB)
{
    if (!device || !ctx || !filename) return false;

    char narrow[4096] = {};
    size_t converted = 0;
    wcstombs_s(&converted, narrow, sizeof(narrow), filename, _TRUNCATE);

    int w = 0, h = 0, ch = 0;
    uint8_t* pixels = stbi_load(narrow, &w, &h, &ch, 4);
    if (!pixels) return false;

    const DXGI_FORMAT fmt = isSRGB
        ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
        : DXGI_FORMAT_R8G8B8A8_UNORM;

    ID3D11ShaderResourceView* srv = nullptr;
    const bool ok = CreateSRVWithMips(device, ctx, pixels, w, h, fmt, &srv);
    stbi_image_free(pixels);

    if (!ok) return false;

    outResource.srv       = srv;
    outResource.width     = static_cast<uint32_t>(w);
    outResource.height    = static_cast<uint32_t>(h);
    outResource.ready     = true;
    outResource.isSRGB    = isSRGB;
    outResource.format    = isSRGB ? GDXTextureFormat::RGBA8_UNORM_SRGB : GDXTextureFormat::RGBA8_UNORM;
    outResource.semantic  = GDXTextureSemantic::Unknown;
    outResource.debugName = filename;
    return true;
}

// ---------------------------------------------------------------------------
// Öffentliche API: 1×1 Fallback-Textur erstellen (kein stb_image, kein Mip)
// ---------------------------------------------------------------------------
bool GDXTextureLoader_Create1x1(
    ID3D11Device* device,
    uint8_t r, uint8_t g, uint8_t b, uint8_t a,
    GDXTextureResource& outResource)
{
    if (!device) return false;

    const uint8_t pixel[4] = { r, g, b, a };

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width            = 1;
    desc.Height           = 1;
    desc.MipLevels        = 1;
    desc.ArraySize        = 1;
    desc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage            = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags        = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA init = {};
    init.pSysMem     = pixel;
    init.SysMemPitch = 4;

    ID3D11Texture2D* tex = nullptr;
    if (FAILED(device->CreateTexture2D(&desc, &init, &tex)) || !tex) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                    = desc.Format;
    srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels       = 1;

    ID3D11ShaderResourceView* srv = nullptr;
    HRESULT hr = device->CreateShaderResourceView(tex, &srvDesc, &srv);
    tex->Release();

    if (FAILED(hr) || !srv) return false;

    outResource.srv    = srv;
    outResource.width  = 1u;
    outResource.height = 1u;
    outResource.ready  = true;
    outResource.isSRGB = false;
    outResource.format = GDXTextureFormat::RGBA8_UNORM;
    outResource.semantic = GDXTextureSemantic::Unknown;
    return true;
}


// ---------------------------------------------------------------------------
// Öffentliche API: Textur aus ImageBuffer erstellen
// ---------------------------------------------------------------------------
bool GDXTextureLoader_CreateFromImage(
    ID3D11Device*        device,
    ID3D11DeviceContext* ctx,
    const ImageBuffer&   image,
    GDXTextureResource&  outResource,
    bool                 isSRGB,
    const wchar_t*       debugName)
{
    if (!device || !ctx || !image.IsValid()) return false;

    const DXGI_FORMAT fmt = isSRGB
        ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
        : DXGI_FORMAT_R8G8B8A8_UNORM;

    ID3D11ShaderResourceView* srv = nullptr;
    const bool ok = CreateSRVWithMips(device, ctx, image.Data(), static_cast<int>(image.Width()), static_cast<int>(image.Height()), fmt, &srv);
    if (!ok) return false;

    outResource.srv = srv;
    outResource.width = image.Width();
    outResource.height = image.Height();
    outResource.ready = true;
    outResource.isSRGB = isSRGB;
    outResource.format = isSRGB ? GDXTextureFormat::RGBA8_UNORM_SRGB : GDXTextureFormat::RGBA8_UNORM;
    outResource.semantic = GDXTextureSemantic::Procedural;
    outResource.debugName = debugName ? debugName : L"ImageBufferTexture";
    return true;
}
