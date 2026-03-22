#pragma once

#include <vector>
#include <cstdint>
#include <cmath>
#include "GDXMath.h"
#include "GDXVertexFlags.h"

using namespace GIDX;

// ---------------------------------------------------------------------------
// SubmeshData — CPU-Geometrie für einen Sub-Mesh-Slot.
//
// Reines Daten-Struct ohne GPU-Member.
// GPU-Seite lebt separat in GpuMeshBuffer / MeshAssetResource.
//
// Konvention:
//   - positions ist immer gefüllt.
//   - normals, uv0, uv1, tangents, colors sind optional.
//   - indices leer  -> non-indexed Mesh
//   - indices gefüllt -> alle Indices müssen < positions.size() sein.
//   - boneIndices / boneWeights: beide leer oder beide gleich groß wie positions.
// ---------------------------------------------------------------------------
struct SubmeshData
{
    std::vector<GIDX::Float3> positions;
    std::vector<GIDX::Float3> normals;
    std::vector<GIDX::Float2> uv0;
    std::vector<GIDX::Float2> uv1;
    std::vector<GIDX::Float4> tangents;
    std::vector<GIDX::Float4> colors;
    std::vector<uint32_t>          indices;

    std::vector<GIDX::UInt4>  boneIndices;
    std::vector<GIDX::Float4> boneWeights;

    uint32_t VertexCount() const noexcept
    {
        return static_cast<uint32_t>(positions.size());
    }

    uint32_t IndexCount() const noexcept
    {
        return static_cast<uint32_t>(indices.size());
    }

    bool HasNormals() const noexcept
    {
        return !normals.empty() && normals.size() == positions.size();
    }

    bool HasUV0() const noexcept
    {
        return !uv0.empty() && uv0.size() == positions.size();
    }

    bool HasUV1() const noexcept
    {
        return !uv1.empty() && uv1.size() == positions.size();
    }

    bool HasTangents() const noexcept
    {
        return !tangents.empty() && tangents.size() == positions.size();
    }

    bool HasSkinning() const noexcept
    {
        return !boneIndices.empty()
            && boneIndices.size() == positions.size()
            && boneWeights.size() == positions.size();
    }

    bool IsEmpty() const noexcept
    {
        return positions.empty();
    }

    uint32_t ComputeVertexFlags() const noexcept
    {
        uint32_t f = GDX_VERTEX_POSITION;
        if (HasNormals())   f |= GDX_VERTEX_NORMAL;
        if (HasUV0())       f |= GDX_VERTEX_TEX1;
        if (HasUV1())       f |= GDX_VERTEX_TEX2;
        if (!colors.empty() && colors.size() == positions.size())
            f |= GDX_VERTEX_COLOR;
        if (HasTangents())  f |= GDX_VERTEX_TANGENT;
        if (HasSkinning())
            f |= GDX_VERTEX_BONE_INDICES | GDX_VERTEX_BONE_WEIGHTS;
        return f;
    }

    bool Validate() const noexcept
    {
        if (positions.empty()) return false;
        if (!normals.empty() && normals.size() != positions.size()) return false;
        if (!uv0.empty() && uv0.size() != positions.size()) return false;
        if (!uv1.empty() && uv1.size() != positions.size()) return false;
        if (!tangents.empty() && tangents.size() != positions.size()) return false;
        if (!colors.empty() && colors.size() != positions.size()) return false;

        const bool hasBI = !boneIndices.empty();
        const bool hasBW = !boneWeights.empty();
        if (hasBI != hasBW) return false;

        if (hasBI)
        {
            if (boneIndices.size() != positions.size()) return false;
            if (boneWeights.size() != positions.size()) return false;
        }

        for (uint32_t idx : indices)
        {
            if (idx >= positions.size()) return false;
        }

        return true;
    }
};

// ---------------------------------------------------------------------------
// BuiltinMeshes
//
// Winding-Konvention:
//   CCW von außen gesehen, LH-Koordinatensystem.
// ---------------------------------------------------------------------------
namespace BuiltinMeshes
{
    inline SubmeshData Triangle()
    {
        SubmeshData s;
        s.positions = {
            { -0.5f, -0.5f, 0.0f },
            {  0.0f,  0.5f, 0.0f },
            {  0.5f, -0.5f, 0.0f },
        };
        s.normals = {
            { 0.0f, 0.0f, -1.0f },
            { 0.0f, 0.0f, -1.0f },
            { 0.0f, 0.0f, -1.0f },
        };
        s.uv0 = {
            { 0.0f, 1.0f },
            { 0.5f, 0.0f },
            { 1.0f, 1.0f },
        };
        s.indices = { 0, 1, 2 };
        return s;
    }

    inline SubmeshData Cube()
    {
        SubmeshData s;
        s.positions.reserve(24);
        s.normals.reserve(24);
        s.uv0.reserve(24);
        s.indices.reserve(36);

        constexpr float h = 0.5f;
        uint32_t b = 0;

        // Hinten (-Z)
        b = static_cast<uint32_t>(s.positions.size());
        s.positions.insert(s.positions.end(), {
            {-h,-h,-h}, {-h, h,-h}, { h,-h,-h}, { h, h,-h}
            });
        for (int i = 0; i < 4; ++i) s.normals.push_back({ 0,0,-1 });
        s.uv0.insert(s.uv0.end(), {
            {0,1}, {0,0}, {1,1}, {1,0}
            });
        s.indices.insert(s.indices.end(), {
            b + 0, b + 1, b + 2,
            b + 3, b + 2, b + 1
            });

        // Vorne (+Z)
        b = static_cast<uint32_t>(s.positions.size());
        s.positions.insert(s.positions.end(), {
            {-h,-h, h}, {-h, h, h}, { h,-h, h}, { h, h, h}
            });
        for (int i = 0; i < 4; ++i) s.normals.push_back({ 0,0,1 });
        s.uv0.insert(s.uv0.end(), {
            {1,1}, {1,0}, {0,1}, {0,0}
            });
        s.indices.insert(s.indices.end(), {
            b + 2, b + 1, b + 0,
            b + 2, b + 3, b + 1
            });

        // Links (-X)
        b = static_cast<uint32_t>(s.positions.size());
        s.positions.insert(s.positions.end(), {
            {-h,-h,-h}, {-h,-h, h}, {-h, h,-h}, {-h, h, h}
            });
        for (int i = 0; i < 4; ++i) s.normals.push_back({ -1,0,0 });
        s.uv0.insert(s.uv0.end(), {
            {1,1}, {0,1}, {1,0}, {0,0}
            });
        s.indices.insert(s.indices.end(), {
            b + 0, b + 1, b + 2,
            b + 2, b + 1, b + 3
            });

        // Rechts (+X)
        b = static_cast<uint32_t>(s.positions.size());
        s.positions.insert(s.positions.end(), {
            { h,-h,-h}, { h,-h, h}, { h, h,-h}, { h, h, h}
            });
        for (int i = 0; i < 4; ++i) s.normals.push_back({ 1,0,0 });
        s.uv0.insert(s.uv0.end(), {
            {0,1}, {1,1}, {0,0}, {1,0}
            });
        s.indices.insert(s.indices.end(), {
            b + 2, b + 1, b + 0,
            b + 2, b + 3, b + 1
            });

        // Unten (-Y)
        b = static_cast<uint32_t>(s.positions.size());
        s.positions.insert(s.positions.end(), {
            {-h,-h,-h}, { h,-h,-h}, {-h,-h, h}, { h,-h, h}
            });
        for (int i = 0; i < 4; ++i) s.normals.push_back({ 0,-1,0 });
        s.uv0.insert(s.uv0.end(), {
            {0,0}, {1,0}, {0,1}, {1,1}
            });
        s.indices.insert(s.indices.end(), {
            b + 0, b + 1, b + 2,
            b + 2, b + 1, b + 3
            });

        // Oben (+Y)
        b = static_cast<uint32_t>(s.positions.size());
        s.positions.insert(s.positions.end(), {
            {-h, h,-h}, { h, h,-h}, {-h, h, h}, { h, h, h}
            });
        for (int i = 0; i < 4; ++i) s.normals.push_back({ 0,1,0 });
        s.uv0.insert(s.uv0.end(), {
            {1,0}, {0,0}, {1,1}, {0,1}
            });
        s.indices.insert(s.indices.end(), {
            b + 1, b + 2, b + 3,
            b + 2, b + 1, b + 0
            });

        return s;
    }

    // Oktaeder mit harten Kanten:
    //  - 8 Flächen
    //  - 24 Vertices (kein Vertex-Sharing)
    //  - pro Face eine feste Normale
    //  - Winding: CCW von außen gesehen
    inline SubmeshData Octahedron(float radius = 0.5f)
    {
        SubmeshData s;
        s.positions.reserve(24);
        s.normals.reserve(24);
        s.uv0.reserve(24);
        s.indices.reserve(24);

        const float r = radius;
        const float k = 1.0f / std::sqrt(3.0f);

        auto addFace = [&](const Float3& a,
            const Float3& b,
            const Float3& c,
            const Float3& n)
            {
                const uint32_t base = static_cast<uint32_t>(s.positions.size());

                s.positions.push_back(a);
                s.positions.push_back(b);
                s.positions.push_back(c);

                s.normals.push_back(n);
                s.normals.push_back(n);
                s.normals.push_back(n);

                s.uv0.push_back({ 0.5f, 0.0f });
                s.uv0.push_back({ 0.0f, 1.0f });
                s.uv0.push_back({ 1.0f, 1.0f });

                s.indices.push_back(base + 0);
                s.indices.push_back(base + 1);
                s.indices.push_back(base + 2);
            };

        const Float3 T = { 0.0f,  r,    0.0f };
        const Float3 D = { 0.0f, -r,    0.0f };
        const Float3 R = { r,    0.0f,  0.0f };
        const Float3 L = { -r,   0.0f,  0.0f };
        const Float3 F = { 0.0f, 0.0f,  r };
        const Float3 Ba = { 0.0f, 0.0f, -r };

        // Oben
        addFace(T, F, R, { k,  k,  k });
        addFace(T, L, F, { -k,  k,  k });
        addFace(T, Ba, L, { -k,  k, -k });
        addFace(T, R, Ba, { k,  k, -k });

        // Unten
        addFace(D, R, F, { k, -k,  k });
        addFace(D, F, L, { -k, -k,  k });
        addFace(D, L, Ba, { -k, -k, -k });
        addFace(D, Ba, R, { k, -k, -k });

        return s;
    }

    // Pyramide mit quadratischer Basis und harten Kanten.
    // Höhe = size, Basisbreite = size.
    // Mittelpunkt liegt im Ursprung.
    inline SubmeshData Pyramid(float size = 1.0f)
    {
        SubmeshData s;
        s.positions.reserve(18);
        s.normals.reserve(18);
        s.uv0.reserve(18);
        s.indices.reserve(18);

        const float h = size * 0.5f;
        const float hy = size * 0.5f;

        const Float3 top = { 0.0f,  hy, 0.0f };
        const Float3 bl = { -h, -hy, -h };
        const Float3 br = { h, -hy, -h };
        const Float3 fr = { h, -hy,  h };
        const Float3 fl = { -h, -hy,  h };

        auto normalize = [](float x, float y, float z) -> Float3
            {
                const float len = std::sqrt(x * x + y * y + z * z);
                if (len <= 0.000001f) return { 0.0f, 1.0f, 0.0f };
                return { x / len, y / len, z / len };
            };

        auto addFace = [&](const Float3& a,
            const Float3& b,
            const Float3& c,
            const Float3& n,
            const Float2& uvA,
            const Float2& uvB,
            const Float2& uvC)
            {
                const uint32_t base = static_cast<uint32_t>(s.positions.size());

                s.positions.push_back(a);
                s.positions.push_back(b);
                s.positions.push_back(c);

                s.normals.push_back(n);
                s.normals.push_back(n);
                s.normals.push_back(n);

                s.uv0.push_back(uvA);
                s.uv0.push_back(uvB);
                s.uv0.push_back(uvC);

                s.indices.push_back(base + 0);
                s.indices.push_back(base + 1);
                s.indices.push_back(base + 2);
            };

        // Seitenflächen
        {
            const auto n = normalize(0.0f, h, -hy);
            addFace(top, br, bl, n, { 0.5f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f });
        }
        {
            const auto n = normalize(hy, h, 0.0f);
            addFace(top, fr, br, n, { 0.5f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f });
        }
        {
            const auto n = normalize(0.0f, h, hy);
            addFace(top, fl, fr, n, { 0.5f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f });
        }
        {
            const auto n = normalize(-hy, h, 0.0f);
            addFace(top, bl, fl, n, { 0.5f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f });
        }

        // Basis (-Y)
        addFace(bl, br, fl, { 0.0f, -1.0f, 0.0f },
            { 0.0f, 1.0f }, { 1.0f, 1.0f }, { 0.0f, 0.0f });
        addFace(fl, br, fr, { 0.0f, -1.0f, 0.0f },
            { 0.0f, 0.0f }, { 1.0f, 1.0f }, { 1.0f, 0.0f });

        return s;
    }

    // UV-Sphere mit glatten Normalen und shared vertices.
    // slices >= 3, stacks >= 2
    inline SubmeshData Sphere(float radius = 0.5f, uint32_t slices = 24, uint32_t stacks = 16)
    {
        SubmeshData s;

        if (slices < 3) slices = 3;
        if (stacks < 2) stacks = 2;

        const uint32_t ringVertexCount = slices + 1;
        const uint32_t vertexCount = (stacks + 1) * ringVertexCount;
        const uint32_t indexCount = stacks * slices * 6;

        s.positions.reserve(vertexCount);
        s.normals.reserve(vertexCount);
        s.uv0.reserve(vertexCount);
        s.indices.reserve(indexCount);

        constexpr float PI = 3.14159265358979323846f;

        for (uint32_t stack = 0; stack <= stacks; ++stack)
        {
            const float v = static_cast<float>(stack) / static_cast<float>(stacks);
            const float phi = v * PI; // 0 .. PI

            const float y = std::cos(phi);
            const float r = std::sin(phi);

            for (uint32_t slice = 0; slice <= slices; ++slice)
            {
                const float u = static_cast<float>(slice) / static_cast<float>(slices);
                const float theta = u * (2.0f * PI); // 0 .. 2PI

                const float x = r * std::cos(theta);
                const float z = r * std::sin(theta);

                s.positions.push_back({ x * radius, y * radius, z * radius });
                s.normals.push_back({ x, y, z });
                s.uv0.push_back({ u, v });
            }
        }

        for (uint32_t stack = 0; stack < stacks; ++stack)
        {
            for (uint32_t slice = 0; slice < slices; ++slice)
            {
                const uint32_t i0 = stack * ringVertexCount + slice;
                const uint32_t i1 = i0 + 1;
                const uint32_t i2 = (stack + 1) * ringVertexCount + slice;
                const uint32_t i3 = i2 + 1;

                s.indices.push_back(i0);
                s.indices.push_back(i1);
                s.indices.push_back(i2);

                s.indices.push_back(i1);
                s.indices.push_back(i3);
                s.indices.push_back(i2);
            }
        }

        return s;
    }
}