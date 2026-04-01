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
    const float sx = BasisLength(m._11, m._12, m._13);
    const float sy = BasisLength(m._21, m._22, m._23);
    const float sz = BasisLength(m._31, m._32, m._33);
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
    outMin = outMax = { m._41, m._42, m._43 };
    for (int i = 0; i < 3; ++i)
    {
        const float* row = (i == 0) ? &m._11 : (i == 1) ? &m._21 : &m._31;
        float* oMn = &outMin.x + i;
        float* oMx = &outMax.x + i;
        for (int j = 0; j < 3; ++j)
        {
            const float a = row[j] * (&lMin.x)[j];
            const float b = row[j] * (&lMax.x)[j];
            if (a < b) { *oMn += a; *oMx += b; }
            else        { *oMn += b; *oMx += a; }
        }
    }
}

} // namespace FrustumUtils
