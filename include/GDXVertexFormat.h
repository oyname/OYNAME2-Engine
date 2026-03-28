#pragma once

#include "GDXVertexFlags.h"
#include "SubmeshData.h"

#include <array>
#include <cstdint>

// ---------------------------------------------------------------------------
// GDXVertexFormat — explizite Beschreibung des CPU-/GPU-Vertex-Layouts.
//
// Bisher steuerte KROM fast alles nur über Bitflags. Das bleibt erhalten,
// aber diese Schicht macht das Layout greifbar:
//   - BasicMeshGenerator kann ein Zielformat anpeilen
//   - ShaderLayout kann explizit sagen welche Streams gebraucht werden
//   - spätere Backends (OpenGL/Vulkan/DX12) bekommen dieselbe Metadatenbasis
// ---------------------------------------------------------------------------

enum class GDXVertexSemantic : uint8_t
{
    Position,
    Normal,
    Color,
    TexCoord0,
    TexCoord1,
    Tangent,
    BoneIndices,
    BoneWeights,
};

struct GDXVertexElementDesc
{
    GDXVertexSemantic semantic = GDXVertexSemantic::Position;
    uint8_t semanticIndex = 0u;
    uint8_t streamIndex = 0u;
    uint8_t componentCount = 0u;
    uint8_t componentSizeBytes = 0u;
    uint32_t requiredFlags = 0u;
};

struct GDXVertexFormatDesc
{
    uint32_t flags = GDX_VERTEX_NONE;
    std::array<GDXVertexElementDesc, 8> elements{};
    uint32_t elementCount = 0u;

    bool Has(GDXVertexSemantic semantic, uint8_t semanticIndex = 0u) const noexcept
    {
        for (uint32_t i = 0; i < elementCount; ++i)
        {
            if (elements[i].semantic == semantic && elements[i].semanticIndex == semanticIndex)
                return true;
        }
        return false;
    }
};

namespace GDXVertexFormat
{
    inline GDXVertexFormatDesc FromFlags(uint32_t flags) noexcept
    {
        GDXVertexFormatDesc desc{};
        desc.flags = flags;

        auto add = [&](GDXVertexSemantic semantic, uint8_t semanticIndex,
                       uint8_t componentCount, uint8_t componentSizeBytes,
                       uint32_t requiredFlags)
        {
            auto& e = desc.elements[desc.elementCount++];
            e.semantic = semantic;
            e.semanticIndex = semanticIndex;
            e.streamIndex = static_cast<uint8_t>(desc.elementCount - 1u);
            e.componentCount = componentCount;
            e.componentSizeBytes = componentSizeBytes;
            e.requiredFlags = requiredFlags;
        };

        if (flags & GDX_VERTEX_POSITION)     add(GDXVertexSemantic::Position, 0, 3, 4, GDX_VERTEX_POSITION);
        if (flags & GDX_VERTEX_NORMAL)       add(GDXVertexSemantic::Normal, 0, 3, 4, GDX_VERTEX_NORMAL);
        if (flags & GDX_VERTEX_COLOR)        add(GDXVertexSemantic::Color, 0, 4, 4, GDX_VERTEX_COLOR);
        if (flags & GDX_VERTEX_TEX1)         add(GDXVertexSemantic::TexCoord0, 0, 2, 4, GDX_VERTEX_TEX1);
        if (flags & GDX_VERTEX_TEX2)         add(GDXVertexSemantic::TexCoord1, 1, 2, 4, GDX_VERTEX_TEX2);
        if (flags & GDX_VERTEX_TANGENT)      add(GDXVertexSemantic::Tangent, 0, 4, 4, GDX_VERTEX_TANGENT);
        if (flags & GDX_VERTEX_BONE_INDICES) add(GDXVertexSemantic::BoneIndices, 0, 4, 4, GDX_VERTEX_BONE_INDICES);
        if (flags & GDX_VERTEX_BONE_WEIGHTS) add(GDXVertexSemantic::BoneWeights, 0, 4, 4, GDX_VERTEX_BONE_WEIGHTS);
        return desc;
    }

    inline GDXVertexFormatDesc FromSubmesh(const SubmeshData& submesh) noexcept
    {
        return FromFlags(submesh.ComputeVertexFlags());
    }
}
