#pragma once

#include "MeshAssetResource.h"
#include "MeshUtilities.h"
#include "SubmeshBuilder.h"

#include <string>

struct BasicMeshBuildOptions
{
    bool generateNormals = true;
    bool generateTangents = false;
    bool copyUV0ToUV1 = false;
    float uv1ScaleU = 1.0f;
    float uv1ScaleV = 1.0f;
    bool removeDegenerates = true;
    bool validate = true;
};

namespace BasicMeshGenerator
{
    inline void Finalize(SubmeshData& s, const BasicMeshBuildOptions& opt)
    {
        if (opt.removeDegenerates)
            MeshUtilities::RemoveDegenerateTriangles(s);
        if (opt.generateNormals && !s.HasNormals())
            MeshUtilities::ComputeNormals(s);
        if (opt.generateTangents && s.HasUV0() && s.HasNormals() && !s.HasTangents())
            MeshUtilities::ComputeTangents(s);
        if (opt.copyUV0ToUV1 && s.HasUV0() && !s.HasUV1())
        {
            s.uv1.resize(s.uv0.size());
            for (size_t i = 0; i < s.uv0.size(); ++i)
                s.uv1[i] = { s.uv0[i].x * opt.uv1ScaleU, s.uv0[i].y * opt.uv1ScaleV };
        }
        if (opt.validate)
            (void)s.Validate();
    }

    inline SubmeshData QuadXY(float width = 1.0f, float height = 1.0f, const BasicMeshBuildOptions& opt = {})
    {
        SubmeshBuilder b;
        b.Reserve(4, 6);
        const float hx = width * 0.5f;
        const float hy = height * 0.5f;

        const auto v0 = b.AddVertex(-hx, -hy, 0.0f); b.SetUV0(v0, {0, 1});
        const auto v1 = b.AddVertex(-hx,  hy, 0.0f); b.SetUV0(v1, {0, 0});
        const auto v2 = b.AddVertex( hx, -hy, 0.0f); b.SetUV0(v2, {1, 1});
        const auto v3 = b.AddVertex( hx,  hy, 0.0f); b.SetUV0(v3, {1, 0});
        b.AddTriangle(v0, v1, v2);
        b.AddTriangle(v2, v1, v3);

        SubmeshData s = b.MoveBuild();
        Finalize(s, opt);
        return s;
    }

    inline SubmeshData GridXZ(uint32_t cellsX, uint32_t cellsZ, float sizeX = 1.0f, float sizeZ = 1.0f, const BasicMeshBuildOptions& opt = {})
    {
        if (cellsX == 0u) cellsX = 1u;
        if (cellsZ == 0u) cellsZ = 1u;

        const uint32_t vx = cellsX + 1u;
        const uint32_t vz = cellsZ + 1u;
        const float halfX = sizeX * 0.5f;
        const float halfZ = sizeZ * 0.5f;

        SubmeshBuilder b;
        b.Reserve(vx * vz, cellsX * cellsZ * 6u);

        for (uint32_t z = 0; z < vz; ++z)
        {
            const float tz = static_cast<float>(z) / static_cast<float>(cellsZ);
            const float pz = -halfZ + tz * sizeZ;
            for (uint32_t x = 0; x < vx; ++x)
            {
                const float tx = static_cast<float>(x) / static_cast<float>(cellsX);
                const float px = -halfX + tx * sizeX;
                const uint32_t v = b.AddVertex(px, 0.0f, pz);
                b.SetUV0(v, { tx, 1.0f - tz });
            }
        }

        for (uint32_t z = 0; z < cellsZ; ++z)
        {
            for (uint32_t x = 0; x < cellsX; ++x)
            {
                const uint32_t i0 = z * vx + x;
                const uint32_t i1 = i0 + 1u;
                const uint32_t i2 = i0 + vx;
                const uint32_t i3 = i2 + 1u;
                b.AddTriangle(i0, i2, i1);
                b.AddTriangle(i1, i2, i3);
            }
        }

        SubmeshData s = b.MoveBuild();
        Finalize(s, opt);
        return s;
    }

    inline MeshAssetResource MakeMeshAsset(SubmeshData submesh, const std::string& debugName = {})
    {
        MeshAssetResource mesh;
        mesh.debugName = debugName;
        mesh.AddSubmesh(std::move(submesh));
        return mesh;
    }
}
