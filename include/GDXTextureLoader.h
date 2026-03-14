#pragma once

#include "GDXTextureResource.h"
#include "ImageBuffer.h"

#include <cstdint>

struct ID3D11Device;
struct ID3D11DeviceContext;

bool GDXTextureLoader_LoadFromFile(
    ID3D11Device*        device,
    ID3D11DeviceContext* ctx,
    const wchar_t*       filename,
    GDXTextureResource&  outResource,
    bool                 isSRGB);

bool GDXTextureLoader_Create1x1(
    ID3D11Device* device,
    uint8_t r, uint8_t g, uint8_t b, uint8_t a,
    GDXTextureResource& outResource);

bool GDXTextureLoader_CreateFromImage(
    ID3D11Device* device,
    ID3D11DeviceContext* ctx,
    const ImageBuffer& image,
    GDXTextureResource& outResource,
    bool isSRGB,
    const wchar_t* debugName);
