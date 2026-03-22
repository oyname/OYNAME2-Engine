#pragma once

#include "Core/GDXMath.h"
#include "SubmeshData.h"
#include "MeshAssetResource.h"

#include <algorithm>
#include <cmath>
#include <limits>

struct MeshBounds
{
    Float3 min = { 0, 0, 0 };
    Float3 max = { 0, 0, 0 };
    Float3 center = { 0, 0, 0 };
    Float3 extent = { 0, 0, 0 };
    float radius = 0.0f;
    bool valid = false;
};

namespace MeshUtilities
{
    namespace _detail
    {
        inline bool NearlyZero(float v)
        {
            return std::fabs(v) <= 1e-8f;
        }

        inline Float3 SafeNormalize3(const Float3& v, const Float3& fallback)
        {
            return GIDX::Normalize3(v, fallback);
        }

        inline float AreaSq(const Float3& p0, const Float3& p1, const Float3& p2)
        {
            const Float3 e1 = GIDX::Subtract(p1, p0);
            const Float3 e2 = GIDX::Subtract(p2, p0);
            const Float3 c = GIDX::Cross(e1, e2);
            return GIDX::Dot3(c, c);
        }

        inline Float4x4 Inverse3x4(const Float4x4& m)
        {
            const float a00 = m._11, a01 = m._12, a02 = m._13;
            const float a10 = m._21, a11 = m._22, a12 = m._23;
            const float a20 = m._31, a21 = m._32, a22 = m._33;

            const float c00 =  (a11 * a22 - a12 * a21);
            const float c01 = -(a10 * a22 - a12 * a20);
            const float c02 =  (a10 * a21 - a11 * a20);
            const float c10 = -(a01 * a22 - a02 * a21);
            const float c11 =  (a00 * a22 - a02 * a20);
            const float c12 = -(a00 * a21 - a01 * a20);
            const float c20 =  (a01 * a12 - a02 * a11);
            const float c21 = -(a00 * a12 - a02 * a10);
            const float c22 =  (a00 * a11 - a01 * a10);

            const float det = a00 * c00 + a01 * c01 + a02 * c02;
            if (std::fabs(det) <= 1e-12f)
                return GIDX::Identity4x4();

            const float invDet = 1.0f / det;

            Float4x4 inv{};
            inv._11 = c00 * invDet; inv._12 = c10 * invDet; inv._13 = c20 * invDet; inv._14 = 0.0f;
            inv._21 = c01 * invDet; inv._22 = c11 * invDet; inv._23 = c21 * invDet; inv._24 = 0.0f;
            inv._31 = c02 * invDet; inv._32 = c12 * invDet; inv._33 = c22 * invDet; inv._34 = 0.0f;
            inv._44 = 1.0f;

            const Float3 t = { m._41, m._42, m._43 };
            const Float3 invT = GIDX::Scale3(GIDX::TransformVector(t, inv), -1.0f);
            inv._41 = invT.x;
            inv._42 = invT.y;
            inv._43 = invT.z;
            return inv;
        }
    }

    inline bool Validate(const SubmeshData& s) noexcept
    {
        return s.Validate();
    }

    inline MeshBounds RecalculateBounds(const SubmeshData& s)
    {
        MeshBounds b;
        if (s.positions.empty())
            return b;

        Float3 minV = s.positions[0];
        Float3 maxV = s.positions[0];

        for (const auto& p : s.positions)
        {
            minV.x = std::min(minV.x, p.x);
            minV.y = std::min(minV.y, p.y);
            minV.z = std::min(minV.z, p.z);
            maxV.x = std::max(maxV.x, p.x);
            maxV.y = std::max(maxV.y, p.y);
            maxV.z = std::max(maxV.z, p.z);
        }

        b.min = minV;
        b.max = maxV;
        b.center = { (minV.x + maxV.x) * 0.5f, (minV.y + maxV.y) * 0.5f, (minV.z + maxV.z) * 0.5f };
        b.extent = { (maxV.x - minV.x) * 0.5f, (maxV.y - minV.y) * 0.5f, (maxV.z - minV.z) * 0.5f };

        float maxRadiusSq = 0.0f;
        for (const auto& p : s.positions)
        {
            const float dx = p.x - b.center.x;
            const float dy = p.y - b.center.y;
            const float dz = p.z - b.center.z;
            maxRadiusSq = std::max(maxRadiusSq, dx * dx + dy * dy + dz * dz);
        }
        b.radius = std::sqrt(maxRadiusSq);
        b.valid = true;
        return b;
    }

    inline void FlipWinding(SubmeshData& s)
    {
        if (!s.indices.empty())
        {
            for (size_t i = 0; i + 2 < s.indices.size(); i += 3)
                std::swap(s.indices[i + 1], s.indices[i + 2]);
        }
        else
        {
            for (size_t i = 0; i + 2 < s.positions.size(); i += 3)
            {
                std::swap(s.positions[i + 1], s.positions[i + 2]);
                if (s.HasNormals())  std::swap(s.normals[i + 1], s.normals[i + 2]);
                if (s.HasUV0())      std::swap(s.uv0[i + 1], s.uv0[i + 2]);
                if (s.HasUV1())      std::swap(s.uv1[i + 1], s.uv1[i + 2]);
                if (s.HasTangents()) std::swap(s.tangents[i + 1], s.tangents[i + 2]);
                if (!s.colors.empty() && s.colors.size() == s.positions.size())
                    std::swap(s.colors[i + 1], s.colors[i + 2]);
                if (s.HasSkinning())
                {
                    std::swap(s.boneIndices[i + 1], s.boneIndices[i + 2]);
                    std::swap(s.boneWeights[i + 1], s.boneWeights[i + 2]);
                }
            }
        }
    }

    inline void InvertNormals(SubmeshData& s)
    {
        if (!s.HasNormals())
            return;

        for (auto& n : s.normals)
        {
            n.x = -n.x;
            n.y = -n.y;
            n.z = -n.z;
        }

        if (s.HasTangents())
        {
            for (auto& t : s.tangents)
            {
                t.x = -t.x;
                t.y = -t.y;
                t.z = -t.z;
                t.w = -t.w;
            }
        }
    }

    inline void ApplyTransform(SubmeshData& s, const Float4x4& transform)
    {
        const Float4x4 inverse = _detail::Inverse3x4(transform);
        const Float4x4 normalMatrix = {
            inverse._11, inverse._21, inverse._31, 0.0f,
            inverse._12, inverse._22, inverse._32, 0.0f,
            inverse._13, inverse._23, inverse._33, 0.0f,
            0.0f,        0.0f,        0.0f,        1.0f
        };

        for (auto& p : s.positions)
            p = GIDX::TransformPoint(p, transform);

        if (s.HasNormals())
        {
            for (auto& n : s.normals)
                n = _detail::SafeNormalize3(
                    GIDX::TransformVector(n, normalMatrix),
                    { 0.0f, 1.0f, 0.0f });
        }

        if (s.HasTangents())
        {
            for (auto& t : s.tangents)
            {
                const Float3 tv = _detail::SafeNormalize3(
                    GIDX::TransformVector({ t.x, t.y, t.z }, normalMatrix),
                    { 1.0f, 0.0f, 0.0f });
                t.x = tv.x;
                t.y = tv.y;
                t.z = tv.z;
            }
        }
    }

    inline void RemoveDegenerateTriangles(SubmeshData& s, float epsilon = 1e-8f)
    {
        auto areaSq = [&](uint32_t i0, uint32_t i1, uint32_t i2) -> float
        {
            return _detail::AreaSq(s.positions[i0], s.positions[i1], s.positions[i2]);
        };

        if (!s.indices.empty())
        {
            std::vector<uint32_t> cleaned;
            cleaned.reserve(s.indices.size());
            for (size_t i = 0; i + 2 < s.indices.size(); i += 3)
            {
                const uint32_t i0 = s.indices[i + 0];
                const uint32_t i1 = s.indices[i + 1];
                const uint32_t i2 = s.indices[i + 2];
                if (i0 == i1 || i1 == i2 || i0 == i2) continue;
                if (areaSq(i0, i1, i2) <= epsilon) continue;
                cleaned.push_back(i0);
                cleaned.push_back(i1);
                cleaned.push_back(i2);
            }
            s.indices.swap(cleaned);
        }
    }

    inline void ComputeNormals(SubmeshData& s)
    {
        if (s.positions.empty())
            return;

        s.normals.assign(s.positions.size(), Float3{ 0, 0, 0 });

        auto accumulate = [&](uint32_t i0, uint32_t i1, uint32_t i2)
        {
            const Float3 e1 = GIDX::Subtract(s.positions[i1], s.positions[i0]);
            const Float3 e2 = GIDX::Subtract(s.positions[i2], s.positions[i0]);
            const Float3 n = GIDX::Cross(e1, e2);

            auto addTo = [&](uint32_t idx)
            {
                s.normals[idx] = GIDX::Add(s.normals[idx], n);
            };

            addTo(i0);
            addTo(i1);
            addTo(i2);
        };

        if (!s.indices.empty())
        {
            for (size_t i = 0; i + 2 < s.indices.size(); i += 3)
                accumulate(s.indices[i + 0], s.indices[i + 1], s.indices[i + 2]);
        }
        else
        {
            for (size_t i = 0; i + 2 < s.positions.size(); i += 3)
                accumulate(static_cast<uint32_t>(i + 0), static_cast<uint32_t>(i + 1), static_cast<uint32_t>(i + 2));
        }

        for (auto& n : s.normals)
            n = _detail::SafeNormalize3(n, { 0.0f, 1.0f, 0.0f });
    }

    inline void ComputeTangents(SubmeshData& s)
    {
        if (s.positions.empty() || !s.HasUV0())
            return;

        if (!s.HasNormals())
            ComputeNormals(s);

        std::vector<Float3> tan1(s.positions.size(), Float3{ 0, 0, 0 });
        std::vector<Float3> tan2(s.positions.size(), Float3{ 0, 0, 0 });

        auto accumulate = [&](uint32_t i0, uint32_t i1, uint32_t i2)
        {
            const auto& p0f = s.positions[i0];
            const auto& p1f = s.positions[i1];
            const auto& p2f = s.positions[i2];
            const auto& w0 = s.uv0[i0];
            const auto& w1 = s.uv0[i1];
            const auto& w2 = s.uv0[i2];

            const float x1 = p1f.x - p0f.x;
            const float x2 = p2f.x - p0f.x;
            const float y1 = p1f.y - p0f.y;
            const float y2 = p2f.y - p0f.y;
            const float z1 = p1f.z - p0f.z;
            const float z2 = p2f.z - p0f.z;

            const float s1 = w1.x - w0.x;
            const float s2 = w2.x - w0.x;
            const float t1 = w1.y - w0.y;
            const float t2 = w2.y - w0.y;

            const float det = s1 * t2 - s2 * t1;
            if (_detail::NearlyZero(det))
                return;

            const float r = 1.0f / det;
            const Float3 sdir = {
                (t2 * x1 - t1 * x2) * r,
                (t2 * y1 - t1 * y2) * r,
                (t2 * z1 - t1 * z2) * r
            };
            const Float3 tdir = {
                (s1 * x2 - s2 * x1) * r,
                (s1 * y2 - s2 * y1) * r,
                (s1 * z2 - s2 * z1) * r
            };

            tan1[i0] = GIDX::Add(tan1[i0], sdir);
            tan1[i1] = GIDX::Add(tan1[i1], sdir);
            tan1[i2] = GIDX::Add(tan1[i2], sdir);
            tan2[i0] = GIDX::Add(tan2[i0], tdir);
            tan2[i1] = GIDX::Add(tan2[i1], tdir);
            tan2[i2] = GIDX::Add(tan2[i2], tdir);
        };

        if (!s.indices.empty())
        {
            for (size_t i = 0; i + 2 < s.indices.size(); i += 3)
                accumulate(s.indices[i + 0], s.indices[i + 1], s.indices[i + 2]);
        }
        else
        {
            for (size_t i = 0; i + 2 < s.positions.size(); i += 3)
                accumulate(static_cast<uint32_t>(i + 0), static_cast<uint32_t>(i + 1), static_cast<uint32_t>(i + 2));
        }

        s.tangents.assign(s.positions.size(), Float4{ 1, 0, 0, 1 });

        for (size_t i = 0; i < s.positions.size(); ++i)
        {
            const Float3 n = s.normals[i];
            const Float3 t = tan1[i];

            Float3 tangent = GIDX::Subtract(t, GIDX::Scale3(n, GIDX::Dot3(n, t)));
            tangent = _detail::SafeNormalize3(tangent, { 1.0f, 0.0f, 0.0f });

            const Float3 bitangent = GIDX::Cross(n, tangent);
            const float handedness = (GIDX::Dot3(bitangent, tan2[i]) < 0.0f) ? -1.0f : 1.0f;

            s.tangents[i] = {
                tangent.x,
                tangent.y,
                tangent.z,
                handedness
            };
        }
    }
}

// ---------------------------------------------------------------------------
// MeshBuildSettings + MeshProcessor
// Ehemals MeshProcessor.h — hier zusammengefasst, da MeshProcessor nur ein
// Convenience-Wrapper um MeshUtilities ist.
// ---------------------------------------------------------------------------

struct MeshBuildSettings
{
    bool validateInput                = true;
    bool removeDegenerateTriangles    = true;
    bool computeNormalsIfMissing      = true;
    bool recomputeNormals             = false;
    bool computeTangentsIfPossible    = false;
    bool recomputeTangents            = false;
};

namespace MeshProcessor
{
    inline bool Process(SubmeshData& s, const MeshBuildSettings& settings = {})
    {
        if (settings.validateInput && !MeshUtilities::Validate(s))
            return false;

        if (settings.removeDegenerateTriangles)
            MeshUtilities::RemoveDegenerateTriangles(s);

        if (settings.recomputeNormals || (settings.computeNormalsIfMissing && !s.HasNormals()))
            MeshUtilities::ComputeNormals(s);

        const bool shouldTangents = settings.recomputeTangents
            || (settings.computeTangentsIfPossible && !s.HasTangents());

        if (shouldTangents && s.HasUV0())
            MeshUtilities::ComputeTangents(s);

        return !settings.validateInput || MeshUtilities::Validate(s);
    }

    inline bool Process(MeshAssetResource& asset, const MeshBuildSettings& settings = {})
    {
        for (auto& s : asset.submeshes)
        {
            if (!Process(s, settings))
                return false;
        }
        return true;
    }
}
