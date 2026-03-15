#pragma once
#include "Handle.h"
#include "ECSTypes.h"
#include "GDXResourceBinding.h"

#include <cstdint>
#include "GDXMath.h"

enum class RenderPass : uint8_t
{
    Shadow      = 0,
    Opaque      = 1,
    Transparent = 2,
};

struct RenderCommand
{
    MeshHandle     mesh;
    MaterialHandle material;
    ShaderHandle   shader;
    uint32_t       submeshIndex = 0u;
    EntityID       ownerEntity = NULL_ENTITY;
    RenderPass     pass = RenderPass::Opaque;

    GIDX::Float4x4 worldMatrix = {};
    ResourceBindingSet  resourceBindings{};

    uint64_t sortKey = 0ull;
    bool           receiveShadows = true;

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
