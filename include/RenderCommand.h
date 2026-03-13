#pragma once
#include "Handle.h"
#include "ECSTypes.h"

#include <cstdint>
#include <DirectXMath.h>

enum class RenderPass : uint8_t
{
    Shadow      = 0,
    Opaque      = 1,
    Transparent = 2,
};

// ---------------------------------------------------------------------------
// RenderCommand — vollständige Beschreibung eines Draw Calls.
//
// Neu gegenüber vorheriger Version: ShaderHandle shader.
// Der Executor liest den Shader aus dem ShaderStore → vertexFlags →
// bindet genau die Streams die der Shader erwartet.
// ---------------------------------------------------------------------------
struct RenderCommand
{
    MeshHandle     mesh;
    MaterialHandle material;
    ShaderHandle   shader;
    uint32_t       submeshIndex = 0u;
    EntityID       ownerEntity = NULL_ENTITY;
    RenderPass     pass = RenderPass::Opaque;

    DirectX::XMFLOAT4X4 worldMatrix = {};

    uint64_t sortKey = 0ull;

    static uint64_t MakeSortKey(RenderPass pass,
                                 uint32_t shaderSortID,
                                 uint32_t materialSortID,
                                 float    depth)
    {
        const uint32_t depthBits = *reinterpret_cast<const uint32_t*>(&depth);
        return  (static_cast<uint64_t>(static_cast<uint8_t>(pass)) << 62)
              | (static_cast<uint64_t>(shaderSortID   & 0x3FFFu)  << 48)
              | (static_cast<uint64_t>(materialSortID & 0xFFFFu)  << 32)
              | static_cast<uint64_t>(depthBits);
    }

    void SetSortKey(RenderPass pass,
                    uint32_t shaderSortID,
                    uint32_t materialSortID,
                    float depth = 0.0f)
    {
        sortKey = MakeSortKey(pass, shaderSortID, materialSortID, depth);
    }
};
