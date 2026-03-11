#pragma once

#include <vector>
#include <cstdint>
#include <cmath>
#include <DirectXMath.h>

// ---------------------------------------------------------------------------
// SubmeshData — CPU-Geometrie für einen Sub-Mesh-Slot.
//
// Reines Daten-Struct ohne GPU-Member.
// GPU-Seite lebt separat in GpuMeshBuffer / MeshAssetResource.
//
// Konvention:
//   - positions ist immer gefüllt.
//   - normals, uv0, tangents, colors sind optional.
//   - indices leer  -> non-indexed Mesh
//   - indices gefüllt -> alle Indices müssen < positions.size() sein.
//   - boneIndices / boneWeights: beide leer oder beide gleich groß wie positions.
// ---------------------------------------------------------------------------
struct SubmeshData
{
    std::vector<DirectX::XMFLOAT3> positions;
    std::vector<DirectX::XMFLOAT3> normals;
    std::vector<DirectX::XMFLOAT2> uv0;
    std::vector<DirectX::XMFLOAT4> tangents;
    std::vector<DirectX::XMFLOAT4> colors;
    std::vector<uint32_t>          indices;

    std::vector<DirectX::XMUINT4>  boneIndices;
    std::vector<DirectX::XMFLOAT4> boneWeights;

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

    bool Validate() const noexcept
    {
        if (positions.empty()) return false;
        if (!normals.empty() && normals.size() != positions.size()) return false;
        if (!uv0.empty() && uv0.size() != positions.size()) return false;
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

        auto addFace = [&](const DirectX::XMFLOAT3& a,
            const DirectX::XMFLOAT3& b,
            const DirectX::XMFLOAT3& c,
            const DirectX::XMFLOAT3& n)
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

        const DirectX::XMFLOAT3 T = { 0.0f,  r,    0.0f };
        const DirectX::XMFLOAT3 D = { 0.0f, -r,    0.0f };
        const DirectX::XMFLOAT3 R = { r,    0.0f,  0.0f };
        const DirectX::XMFLOAT3 L = { -r,   0.0f,  0.0f };
        const DirectX::XMFLOAT3 F = { 0.0f, 0.0f,  r };
        const DirectX::XMFLOAT3 Ba = { 0.0f, 0.0f, -r };

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
}