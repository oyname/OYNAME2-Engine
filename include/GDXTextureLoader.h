#pragma once

// ---------------------------------------------------------------------------
// GDXTextureLoader.h — DX11-internes Textur-Lade-Interface.
//
// Nur vom DX11-Backend (GDXTextureLoader.cpp, GDXDX11RenderBackend.cpp) includiert.
// Kein Frontend-Code sieht diese Datei.
// ---------------------------------------------------------------------------

#include "GDXTextureResource.h"   // GDXTextureFormat, GDXTextureSemantic
#include "GDXDX11GpuResources.h"  // DX11TextureGpu
#include "ImageBuffer.h"

#include <cstdint>

struct ID3D11Device;
struct ID3D11DeviceContext;

// Lädt Textur aus Datei, erstellt DX11TextureGpu + befüllt Metadaten.
bool GDXTextureLoader_LoadFromFile(
    ID3D11Device*        device,
    ID3D11DeviceContext* ctx,
    const wchar_t*       filename,
    DX11TextureGpu&      outGpu,
    GDXTextureResource&  outMeta,
    bool                 isSRGB);

// Erstellt 1×1 Fallback-Textur.
bool GDXTextureLoader_Create1x1(
    ID3D11Device* device,
    uint8_t r, uint8_t g, uint8_t b, uint8_t a,
    DX11TextureGpu&     outGpu,
    GDXTextureResource& outMeta);

// Erstellt Textur aus CPU-ImageBuffer.
bool GDXTextureLoader_CreateFromImage(
    ID3D11Device*        device,
    ID3D11DeviceContext* ctx,
    const ImageBuffer&   image,
    DX11TextureGpu&      outGpu,
    GDXTextureResource&  outMeta,
    bool                 isSRGB,
    const wchar_t*       debugName);
