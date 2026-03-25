#pragma once
#include "GDXVertexFlags.h"
#include "ShaderVariant.h"
#include "GDXShaderLayout.h"
#include <cstdint>
#include <string>

// ---------------------------------------------------------------------------
// GDXShaderResource — CPU-Metadaten eines Shader-Paars im ResourceStore.
//
// Kein GPU-Objekt. Das zugehörige DX11ShaderGpu liegt in
// GDXDX11GpuRegistry und ist nur dem Backend zugänglich.
// ---------------------------------------------------------------------------
struct GDXShaderResource
{
    uint32_t         vertexFlags       = GDX_VERTEX_DEFAULT;
    GDXShaderLayout  layout;
    ShaderPassType   passType          = ShaderPassType::Main;
    uint32_t         variantFeatures   = SVF_NONE;
    bool             supportsSkinning  = false;
    bool             usesVertexColor   = false;
    bool             ready             = false;
    std::wstring     debugName;

    bool IsValid() const noexcept { return ready; }

    GDXShaderResource() = default;
    GDXShaderResource(const GDXShaderResource&)            = delete;
    GDXShaderResource& operator=(const GDXShaderResource&) = delete;
    GDXShaderResource(GDXShaderResource&&)                 = default;
    GDXShaderResource& operator=(GDXShaderResource&&)      = default;
};
