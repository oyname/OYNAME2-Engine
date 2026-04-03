#pragma once

#include "GDXVertexFlags.h"
#include "SubmeshData.h"

#include <array>
#include <cstdint>

// ---------------------------------------------------------------------------
// GDXVertexFormat — explizite Beschreibung des CPU-/GPU-Vertex-Layouts.
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
    InstanceWorld0,
    InstanceWorld1,
    InstanceWorld2,
    InstanceWorld3,
    InstanceWorldIT0,
    InstanceWorldIT1,
    InstanceWorldIT2,
    InstanceWorldIT3,
};

enum class GDXVertexElementFormat : uint8_t
{
    Float2,
    Float3,
    Float4,
    UInt4,
};

struct GDXVertexElementDesc
{
    GDXVertexSemantic semantic = GDXVertexSemantic::Position;
    uint8_t semanticIndex = 0u;
    uint8_t streamIndex = 0u;
    uint8_t componentCount = 0u;
    uint8_t componentSizeBytes = 0u;
    uint32_t requiredFlags = 0u;
    GDXVertexElementFormat format = GDXVertexElementFormat::Float3;
};

struct GDXVertexFormatDesc
{
    uint32_t flags = GDX_VERTEX_NONE;
    std::array<GDXVertexElementDesc, 16> elements{};
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
    inline void AddElement(GDXVertexFormatDesc& desc,
                           GDXVertexSemantic semantic,
                           uint8_t semanticIndex,
                           uint8_t componentCount,
                           uint8_t componentSizeBytes,
                           uint32_t requiredFlags,
                           GDXVertexElementFormat format,
                           uint8_t streamIndex = 0xFFu) noexcept
    {
        if (desc.elementCount >= desc.elements.size())
            return;

        auto& e = desc.elements[desc.elementCount];
        e.semantic = semantic;
        e.semanticIndex = semanticIndex;
        e.streamIndex = (streamIndex == 0xFFu) ? static_cast<uint8_t>(desc.elementCount) : streamIndex;
        e.componentCount = componentCount;
        e.componentSizeBytes = componentSizeBytes;
        e.requiredFlags = requiredFlags;
        e.format = format;
        ++desc.elementCount;
    }

    inline GDXVertexFormatDesc FromFlags(uint32_t flags) noexcept
    {
        GDXVertexFormatDesc desc{};
        desc.flags = flags;

        if (flags & GDX_VERTEX_POSITION)     AddElement(desc, GDXVertexSemantic::Position, 0, 3, 4, GDX_VERTEX_POSITION, GDXVertexElementFormat::Float3);
        if (flags & GDX_VERTEX_NORMAL)       AddElement(desc, GDXVertexSemantic::Normal, 0, 3, 4, GDX_VERTEX_NORMAL, GDXVertexElementFormat::Float3);
        if (flags & GDX_VERTEX_COLOR)        AddElement(desc, GDXVertexSemantic::Color, 0, 4, 4, GDX_VERTEX_COLOR, GDXVertexElementFormat::Float4);
        if (flags & GDX_VERTEX_TEX1)         AddElement(desc, GDXVertexSemantic::TexCoord0, 0, 2, 4, GDX_VERTEX_TEX1, GDXVertexElementFormat::Float2);
        if (flags & GDX_VERTEX_TEX2)         AddElement(desc, GDXVertexSemantic::TexCoord1, 1, 2, 4, GDX_VERTEX_TEX2, GDXVertexElementFormat::Float2);
        if (flags & GDX_VERTEX_TANGENT)      AddElement(desc, GDXVertexSemantic::Tangent, 0, 4, 4, GDX_VERTEX_TANGENT, GDXVertexElementFormat::Float4);
        if (flags & GDX_VERTEX_BONE_INDICES) AddElement(desc, GDXVertexSemantic::BoneIndices, 0, 4, 4, GDX_VERTEX_BONE_INDICES, GDXVertexElementFormat::UInt4);
        if (flags & GDX_VERTEX_BONE_WEIGHTS) AddElement(desc, GDXVertexSemantic::BoneWeights, 0, 4, 4, GDX_VERTEX_BONE_WEIGHTS, GDXVertexElementFormat::Float4);

        constexpr uint8_t kInstanceStream = 8u;
        AddElement(desc, GDXVertexSemantic::InstanceWorld0,   8, 4, 4, 0u, GDXVertexElementFormat::Float4, kInstanceStream);
        AddElement(desc, GDXVertexSemantic::InstanceWorld1,   9, 4, 4, 0u, GDXVertexElementFormat::Float4, kInstanceStream);
        AddElement(desc, GDXVertexSemantic::InstanceWorld2,  10, 4, 4, 0u, GDXVertexElementFormat::Float4, kInstanceStream);
        AddElement(desc, GDXVertexSemantic::InstanceWorld3,  11, 4, 4, 0u, GDXVertexElementFormat::Float4, kInstanceStream);
        AddElement(desc, GDXVertexSemantic::InstanceWorldIT0, 12, 4, 4, 0u, GDXVertexElementFormat::Float4, kInstanceStream);
        AddElement(desc, GDXVertexSemantic::InstanceWorldIT1, 13, 4, 4, 0u, GDXVertexElementFormat::Float4, kInstanceStream);
        AddElement(desc, GDXVertexSemantic::InstanceWorldIT2, 14, 4, 4, 0u, GDXVertexElementFormat::Float4, kInstanceStream);
        AddElement(desc, GDXVertexSemantic::InstanceWorldIT3, 15, 4, 4, 0u, GDXVertexElementFormat::Float4, kInstanceStream);
        return desc;
    }
}
