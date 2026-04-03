#pragma once
// ---------------------------------------------------------------------------
// Math/Geometry/FrustumUtils.h  --  Frustum-Build + Cull-Tests (inline)
//
// Keine Render-State-Abhängigkeit. Nutzt nur FrustumPlane/FrustumData
// und primitive Math-Typen.
// ---------------------------------------------------------------------------
#include "Math/Geometry/Frustum.h"
#include "Core/GDXMath.h"

#include <cmath>
#include <algorithm>

namespace FrustumUtils
{

inline float BasisLength(float x, float y, float z)
{
    return std::sqrt(x * x + y * y + z * z);
}

inline float ComputeMaxWorldScale(const Matrix4& m)
{
    // Die Engine verwendet die Matrix mit Achsen in den Spalten:
    // TransformPoint(): x = p.x*m._11 + p.y*m._21 + p.z*m._31 + m._41
    // Entsprechend muessen fuer den konservativen Sphere-Radius die
    // Spaltenlaengen der 3x3-Basis gemessen werden, nicht die Zeilen.
    const float sx = BasisLength(m._11, m._21, m._31);
    const float sy = BasisLength(m._12, m._22, m._32);
    const float sz = BasisLength(m._13, m._23, m._33);
    return (std::max)({ sx, sy, sz, 1e-6f });
}

inline FrustumPlane NormalizePlane(const FrustumPlane& p)
{
    const float len = std::sqrt(p.normal.x * p.normal.x +
                                p.normal.y * p.normal.y +
                                p.normal.z * p.normal.z);
    if (len <= 1e-6f) return p;
    const float inv = 1.0f / len;
    FrustumPlane out = p;
    out.normal.x *= inv; out.normal.y *= inv; out.normal.z *= inv;
    out.d        *= inv;
    return out;
}

inline FrustumData BuildFrustumFromViewProj(const Matrix4& m)
{
    FrustumData f{};
    f.planes[0] = NormalizePlane({ { m._14 + m._11, m._24 + m._21, m._34 + m._31 }, m._44 + m._41 });
    f.planes[1] = NormalizePlane({ { m._14 - m._11, m._24 - m._21, m._34 - m._31 }, m._44 - m._41 });
    f.planes[2] = NormalizePlane({ { m._14 + m._12, m._24 + m._22, m._34 + m._32 }, m._44 + m._42 });
    f.planes[3] = NormalizePlane({ { m._14 - m._12, m._24 - m._22, m._34 - m._32 }, m._44 - m._42 });
    f.planes[4] = NormalizePlane({ { m._13,         m._23,         m._33         }, m._43          });
    f.planes[5] = NormalizePlane({ { m._14 - m._13, m._24 - m._23, m._34 - m._33 }, m._44 - m._43 });
    f.valid = true;
    return f;
}

inline bool SphereInsideFrustum(const FrustumData& f, const Float3& center, float radius)
{
    if (!f.valid) return true;
    for (const FrustumPlane& p : f.planes)
    {
        if (p.normal.x * center.x + p.normal.y * center.y +
            p.normal.z * center.z + p.d < -radius)
            return false;
    }
    return true;
}

inline bool AABBInsideFrustum(const FrustumData& f, const Float3& wMin, const Float3& wMax)
{
    if (!f.valid) return true;
    for (const FrustumPlane& p : f.planes)
    {
        const float px = (p.normal.x >= 0.0f) ? wMax.x : wMin.x;
        const float py = (p.normal.y >= 0.0f) ? wMax.y : wMin.y;
        const float pz = (p.normal.z >= 0.0f) ? wMax.z : wMin.z;
        if (p.normal.x * px + p.normal.y * py + p.normal.z * pz + p.d < 0.0f)
            return false;
    }
    return true;
}

inline void TransformAABB(const Float3& lMin, const Float3& lMax,
                           const Matrix4& m,
                           Float3& outMin, Float3& outMax)
{
    const Float3 localCenter = {
        (lMin.x + lMax.x) * 0.5f,
        (lMin.y + lMax.y) * 0.5f,
        (lMin.z + lMax.z) * 0.5f,
    };
    const Float3 localExtents = {
        (lMax.x - lMin.x) * 0.5f,
        (lMax.y - lMin.y) * 0.5f,
        (lMax.z - lMin.z) * 0.5f,
    };

    const Float3 worldCenter = {
        localCenter.x * m._11 + localCenter.y * m._21 + localCenter.z * m._31 + m._41,
        localCenter.x * m._12 + localCenter.y * m._22 + localCenter.z * m._32 + m._42,
        localCenter.x * m._13 + localCenter.y * m._23 + localCenter.z * m._33 + m._43,
    };

    Float3 worldExtents = {
        std::abs(m._11) * localExtents.x + std::abs(m._21) * localExtents.y + std::abs(m._31) * localExtents.z,
        std::abs(m._12) * localExtents.x + std::abs(m._22) * localExtents.y + std::abs(m._32) * localExtents.z,
        std::abs(m._13) * localExtents.x + std::abs(m._23) * localExtents.y + std::abs(m._33) * localExtents.z,
    };

    constexpr float kCullAabbEpsilon = 1.0e-3f;
    worldExtents.x = (std::max)(worldExtents.x, kCullAabbEpsilon);
    worldExtents.y = (std::max)(worldExtents.y, kCullAabbEpsilon);
    worldExtents.z = (std::max)(worldExtents.z, kCullAabbEpsilon);

    outMin = {
        worldCenter.x - worldExtents.x,
        worldCenter.y - worldExtents.y,
        worldCenter.z - worldExtents.z,
    };
    outMax = {
        worldCenter.x + worldExtents.x,
        worldCenter.y + worldExtents.y,
        worldCenter.z + worldExtents.z,
    };
}

} // namespace FrustumUtils
