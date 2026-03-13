#pragma once

#include "SubmeshData.h"

#include <algorithm>
#include <cmath>
#include <limits>

struct MeshBounds
{
    DirectX::XMFLOAT3 min = { 0, 0, 0 };
    DirectX::XMFLOAT3 max = { 0, 0, 0 };
    DirectX::XMFLOAT3 center = { 0, 0, 0 };
    DirectX::XMFLOAT3 extent = { 0, 0, 0 };
    float radius = 0.0f;
    bool valid = false;
};

namespace MeshUtilities
{
    namespace _detail
    {
        inline DirectX::XMVECTOR Load3(const DirectX::XMFLOAT3& v)
        {
            return DirectX::XMLoadFloat3(&v);
        }

        inline DirectX::XMVECTOR SafeNormalize3(DirectX::XMVECTOR v, DirectX::XMVECTOR fallback)
        {
            const float lenSq = DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(v));
            if (lenSq <= 1e-12f)
                return fallback;
            return DirectX::XMVector3Normalize(v);
        }

        inline bool NearlyZero(float v)
        {
            return std::fabs(v) <= 1e-8f;
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

        DirectX::XMFLOAT3 minV = s.positions[0];
        DirectX::XMFLOAT3 maxV = s.positions[0];

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

    inline void ApplyTransform(SubmeshData& s, const DirectX::XMMATRIX& transform)
    {
        const DirectX::XMMATRIX normalMatrix = DirectX::XMMatrixTranspose(DirectX::XMMatrixInverse(nullptr, transform));

        for (auto& p : s.positions)
        {
            const DirectX::XMVECTOR v = DirectX::XMVector3TransformCoord(_detail::Load3(p), transform);
            DirectX::XMStoreFloat3(&p, v);
        }

        if (s.HasNormals())
        {
            const DirectX::XMVECTOR fallback = DirectX::XMVectorSet(0, 1, 0, 0);
            for (auto& n : s.normals)
            {
                DirectX::XMVECTOR v = DirectX::XMVector3TransformNormal(_detail::Load3(n), normalMatrix);
                v = _detail::SafeNormalize3(v, fallback);
                DirectX::XMStoreFloat3(&n, v);
            }
        }

        if (s.HasTangents())
        {
            const DirectX::XMVECTOR fallback = DirectX::XMVectorSet(1, 0, 0, 0);
            for (auto& t : s.tangents)
            {
                DirectX::XMVECTOR tv = DirectX::XMVectorSet(t.x, t.y, t.z, 0.0f);
                tv = DirectX::XMVector3TransformNormal(tv, normalMatrix);
                tv = _detail::SafeNormalize3(tv, fallback);
                t.x = DirectX::XMVectorGetX(tv);
                t.y = DirectX::XMVectorGetY(tv);
                t.z = DirectX::XMVectorGetZ(tv);
            }
        }
    }

    inline void RemoveDegenerateTriangles(SubmeshData& s, float epsilon = 1e-8f)
    {
        auto areaSq = [&](uint32_t i0, uint32_t i1, uint32_t i2) -> float
        {
            const DirectX::XMVECTOR p0 = _detail::Load3(s.positions[i0]);
            const DirectX::XMVECTOR p1 = _detail::Load3(s.positions[i1]);
            const DirectX::XMVECTOR p2 = _detail::Load3(s.positions[i2]);
            const DirectX::XMVECTOR e1 = DirectX::XMVectorSubtract(p1, p0);
            const DirectX::XMVECTOR e2 = DirectX::XMVectorSubtract(p2, p0);
            return DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(DirectX::XMVector3Cross(e1, e2)));
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

        s.normals.assign(s.positions.size(), DirectX::XMFLOAT3{ 0, 0, 0 });

        auto accumulate = [&](uint32_t i0, uint32_t i1, uint32_t i2)
        {
            const DirectX::XMVECTOR p0 = _detail::Load3(s.positions[i0]);
            const DirectX::XMVECTOR p1 = _detail::Load3(s.positions[i1]);
            const DirectX::XMVECTOR p2 = _detail::Load3(s.positions[i2]);
            const DirectX::XMVECTOR e1 = DirectX::XMVectorSubtract(p1, p0);
            const DirectX::XMVECTOR e2 = DirectX::XMVectorSubtract(p2, p0);
            const DirectX::XMVECTOR n = DirectX::XMVector3Cross(e1, e2);

            auto addTo = [&](uint32_t idx)
            {
                DirectX::XMVECTOR a = _detail::Load3(s.normals[idx]);
                a = DirectX::XMVectorAdd(a, n);
                DirectX::XMStoreFloat3(&s.normals[idx], a);
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

        const DirectX::XMVECTOR fallback = DirectX::XMVectorSet(0, 1, 0, 0);
        for (auto& n : s.normals)
        {
            DirectX::XMVECTOR v = _detail::SafeNormalize3(_detail::Load3(n), fallback);
            DirectX::XMStoreFloat3(&n, v);
        }
    }

    inline void ComputeTangents(SubmeshData& s)
    {
        if (s.positions.empty() || !s.HasUV0())
            return;

        if (!s.HasNormals())
            ComputeNormals(s);

        std::vector<DirectX::XMVECTOR> tan1(s.positions.size(), DirectX::XMVectorZero());
        std::vector<DirectX::XMVECTOR> tan2(s.positions.size(), DirectX::XMVectorZero());

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
            const DirectX::XMVECTOR sdir = DirectX::XMVectorSet(
                (t2 * x1 - t1 * x2) * r,
                (t2 * y1 - t1 * y2) * r,
                (t2 * z1 - t1 * z2) * r,
                0.0f);
            const DirectX::XMVECTOR tdir = DirectX::XMVectorSet(
                (s1 * x2 - s2 * x1) * r,
                (s1 * y2 - s2 * y1) * r,
                (s1 * z2 - s2 * z1) * r,
                0.0f);

            tan1[i0] = DirectX::XMVectorAdd(tan1[i0], sdir);
            tan1[i1] = DirectX::XMVectorAdd(tan1[i1], sdir);
            tan1[i2] = DirectX::XMVectorAdd(tan1[i2], sdir);
            tan2[i0] = DirectX::XMVectorAdd(tan2[i0], tdir);
            tan2[i1] = DirectX::XMVectorAdd(tan2[i1], tdir);
            tan2[i2] = DirectX::XMVectorAdd(tan2[i2], tdir);
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

        s.tangents.assign(s.positions.size(), DirectX::XMFLOAT4{ 1, 0, 0, 1 });
        const DirectX::XMVECTOR fallbackT = DirectX::XMVectorSet(1, 0, 0, 0);

        for (size_t i = 0; i < s.positions.size(); ++i)
        {
            const DirectX::XMVECTOR n = _detail::Load3(s.normals[i]);
            const DirectX::XMVECTOR t = tan1[i];

            DirectX::XMVECTOR tangent = DirectX::XMVectorSubtract(t, DirectX::XMVectorScale(n, DirectX::XMVectorGetX(DirectX::XMVector3Dot(n, t))));
            tangent = _detail::SafeNormalize3(tangent, fallbackT);

            const DirectX::XMVECTOR bitangent = DirectX::XMVector3Cross(n, tangent);
            const float handedness = (DirectX::XMVectorGetX(DirectX::XMVector3Dot(bitangent, tan2[i])) < 0.0f) ? -1.0f : 1.0f;

            s.tangents[i] = {
                DirectX::XMVectorGetX(tangent),
                DirectX::XMVectorGetY(tangent),
                DirectX::XMVectorGetZ(tangent),
                handedness
            };
        }
    }
}
