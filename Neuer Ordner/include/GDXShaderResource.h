#pragma once
#include "GDXVertexFlags.h"
#include "ShaderVariant.h"
#include "GDXShaderLayout.h"
#include <cstdint>
#include <string>

// ---------------------------------------------------------------------------
// GDXShaderResource — geladenes Shader-Paar (VS + PS) im ResourceStore.
//
// Kein <d3d11.h> im Header — GPU-Objekte hinter void*.
//
// vertexFlags steuert:
//   1. InputLayout-Bau   (beim Laden via CreateShader)
//   2. Mesh-Upload       (GDXDX11MeshUploader liest Flags → separate Streams)
//   3. Draw-Binding      (GDXDX11RenderExecutor bindet nur aktive Slots)
//
// Anwender-Workflow (wie OYNAME CreateShader):
//   ShaderHandle h = renderer.CreateShader(
//       L"MyVS.hlsl", L"MyPS.hlsl",
//       GDX_VERTEX_POSITION | GDX_VERTEX_NORMAL | GDX_VERTEX_TEX1);
//   // InputLayout automatisch gebaut, fertig.
// ---------------------------------------------------------------------------
struct GDXShaderResource
{
    // Vertex-Format-Flags (steuern InputLayout + Upload + Binding)
    uint32_t vertexFlags = GDX_VERTEX_DEFAULT;
    GDXShaderLayout layout;

    // GPU-Handles (backend-agnostisch, hinter void*)
    void* vertexShader = nullptr;   // ID3D11VertexShader*
    void* pixelShader  = nullptr;   // ID3D11PixelShader*
    void* inputLayout  = nullptr;   // ID3D11InputLayout*

    // Debug-Name (RenderDoc, PIX)
    std::wstring debugName;

    // Varianten-Metadaten (backend-neutral)
    ShaderPassType passType = ShaderPassType::Main;
    uint32_t variantFeatures = SVF_NONE;
    bool supportsSkinning = false;
    bool usesVertexColor = false;

    bool IsValid() const noexcept
    {
        return vertexShader != nullptr
            && pixelShader  != nullptr
            && inputLayout  != nullptr;
    }

    // GPU-Cleanup erfolgt in GDXECSRenderer::Shutdown via ShaderStore.ForEach.
    GDXShaderResource() = default;
    GDXShaderResource(const GDXShaderResource&) = delete;
    GDXShaderResource& operator=(const GDXShaderResource&) = delete;
    GDXShaderResource(GDXShaderResource&&) = default;
    GDXShaderResource& operator=(GDXShaderResource&&) = default;
};
