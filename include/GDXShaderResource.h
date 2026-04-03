#pragma once
#include "GDXVertexFlags.h"
#include "ShaderVariant.h"
#include "GDXShaderContracts.h"
#include <array>
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
    GDXPipelineLayoutDesc pipelineLayout;
    GDXVertexFormatDesc expectedVertexFormat;
    GDXShaderSourceAssetDesc sourceAsset;
    std::array<GDXShaderArtifactDesc, 2> artifacts{};
    uint32_t         artifactCount      = 0u;
    ShaderPassType   passType          = ShaderPassType::Main;
    uint32_t         variantFeatures   = SVF_NONE;
    bool             supportsSkinning  = false;
    bool             supportsInstancing = false;
    bool             usesVertexColor   = false;
    bool             ready             = false;
    std::wstring     debugName;

    bool IsValid() const noexcept { return ready; }

    void SetInterfaceContract(const GDXShaderInterfaceContract& contract) noexcept
    {
        layout = contract.shaderLayout;
        pipelineLayout = contract.pipelineLayout;
        expectedVertexFormat = contract.vertexFormat;
    }

    void AddArtifact(const GDXShaderArtifactDesc& artifact) noexcept
    {
        if (artifactCount >= artifacts.size())
            return;
        artifacts[artifactCount++] = artifact;
    }

    GDXShaderResource() = default;
    GDXShaderResource(const GDXShaderResource&)            = delete;
    GDXShaderResource& operator=(const GDXShaderResource&) = delete;
    GDXShaderResource(GDXShaderResource&&)                 = default;
    GDXShaderResource& operator=(GDXShaderResource&&)      = default;
};
