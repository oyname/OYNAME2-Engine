#pragma once

#include <cstdint>
#include <cmath>
#include <cstring>
#include <DirectXMath.h>

namespace GIDX
{
    struct Float2
    {
        float x = 0.0f;
        float y = 0.0f;
    };
    static_assert(sizeof(Float2) == 8, "Float2 size mismatch");

    struct Float3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };
    static_assert(sizeof(Float3) == 12, "Float3 size mismatch");

    struct Float4
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float w = 0.0f;
    };
    static_assert(sizeof(Float4) == 16, "Float4 size mismatch");

    struct UInt4
    {
        uint32_t x = 0u;
        uint32_t y = 0u;
        uint32_t z = 0u;
        uint32_t w = 0u;
    };
    static_assert(sizeof(UInt4) == 16, "UInt4 size mismatch");

    struct Float4x4
    {
        float _11 = 1.0f, _12 = 0.0f, _13 = 0.0f, _14 = 0.0f;
        float _21 = 0.0f, _22 = 1.0f, _23 = 0.0f, _24 = 0.0f;
        float _31 = 0.0f, _32 = 0.0f, _33 = 1.0f, _34 = 0.0f;
        float _41 = 0.0f, _42 = 0.0f, _43 = 0.0f, _44 = 1.0f;
    };
    static_assert(sizeof(Float4x4) == 64, "Float4x4 size mismatch");


    constexpr float Pi = 3.14159265358979323846f;

    inline constexpr float ToRadians(float degrees) noexcept
    {
        return degrees * (Pi / 180.0f);
    }

    inline constexpr Float4x4 Identity4x4() noexcept
    {
        return {};
    }

    inline Float3 Add(const Float3& a, const Float3& b) noexcept
    {
        return { a.x + b.x, a.y + b.y, a.z + b.z };
    }

    inline Float3 Subtract(const Float3& a, const Float3& b) noexcept
    {
        return { a.x - b.x, a.y - b.y, a.z - b.z };
    }

    inline Float3 Scale3(const Float3& v, float s) noexcept
    {
        return { v.x * s, v.y * s, v.z * s };
    }

    inline float Dot3(const Float3& a, const Float3& b) noexcept
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    inline float Length3(const Float3& v) noexcept
    {
        return std::sqrt(Dot3(v, v));
    }

    inline Float3 Normalize3(const Float3& v, const Float3& fallback = { 0.0f, 0.0f, 1.0f }) noexcept
    {
        const float len = Length3(v);
        if (len <= 1e-8f)
            return fallback;
        const float inv = 1.0f / len;
        return { v.x * inv, v.y * inv, v.z * inv };
    }

    inline Float3 Cross(const Float3& a, const Float3& b) noexcept
    {
        return {
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x
        };
    }

    inline Float4 NormalizeQuat(const Float4& q, const Float4& fallback = { 0.0f, 0.0f, 0.0f, 1.0f }) noexcept
    {
        const float lenSq = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
        if (lenSq <= 1e-12f)
            return fallback;
        const float invLen = 1.0f / std::sqrt(lenSq);
        return { q.x * invLen, q.y * invLen, q.z * invLen, q.w * invLen };
    }

    inline Float4 QuaternionMultiply(const Float4& a, const Float4& b) noexcept
    {
        return NormalizeQuat({
            a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
        });
    }

    inline Float4 QuaternionFromEulerDeg(float pitchDeg, float yawDeg, float rollDeg) noexcept
    {
        const float pitch = ToRadians(pitchDeg) * 0.5f;
        const float yaw = ToRadians(yawDeg) * 0.5f;
        const float roll = ToRadians(rollDeg) * 0.5f;

        const float sp = std::sin(pitch);
        const float cp = std::cos(pitch);
        const float sy = std::sin(yaw);
        const float cy = std::cos(yaw);
        const float sr = std::sin(roll);
        const float cr = std::cos(roll);

        return NormalizeQuat({
            sp * cy * cr + cp * sy * sr,
            cp * sy * cr - sp * cy * sr,
            cp * cy * sr + sp * sy * cr,
            cp * cy * cr - sp * sy * sr
        });
    }

    inline Float4 QuaternionFromBasis(const Float3& rightIn, const Float3& upIn, const Float3& forwardIn) noexcept
    {
        const Float3 right = Normalize3(rightIn, { 1.0f, 0.0f, 0.0f });
        const Float3 up = Normalize3(upIn, { 0.0f, 1.0f, 0.0f });
        const Float3 forward = Normalize3(forwardIn, { 0.0f, 0.0f, 1.0f });

        const float m00 = right.x;
        const float m01 = right.y;
        const float m02 = right.z;
        const float m10 = up.x;
        const float m11 = up.y;
        const float m12 = up.z;
        const float m20 = forward.x;
        const float m21 = forward.y;
        const float m22 = forward.z;

        const float trace = m00 + m11 + m22;
        Float4 q{};

        if (trace > 0.0f)
        {
            const float s = std::sqrt(trace + 1.0f) * 2.0f;
            q.w = 0.25f * s;
            q.x = (m12 - m21) / s;
            q.y = (m20 - m02) / s;
            q.z = (m01 - m10) / s;
        }
        else if (m00 > m11 && m00 > m22)
        {
            const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
            q.w = (m12 - m21) / s;
            q.x = 0.25f * s;
            q.y = (m10 + m01) / s;
            q.z = (m20 + m02) / s;
        }
        else if (m11 > m22)
        {
            const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
            q.w = (m20 - m02) / s;
            q.x = (m10 + m01) / s;
            q.y = 0.25f * s;
            q.z = (m21 + m12) / s;
        }
        else
        {
            const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
            q.w = (m01 - m10) / s;
            q.x = (m20 + m02) / s;
            q.y = (m21 + m12) / s;
            q.z = 0.25f * s;
        }

        return NormalizeQuat(q);
    }

    inline Float3 TransformPoint(const Float3& p, const Float4x4& m) noexcept
    {
        return {
            p.x * m._11 + p.y * m._21 + p.z * m._31 + m._41,
            p.x * m._12 + p.y * m._22 + p.z * m._32 + m._42,
            p.x * m._13 + p.y * m._23 + p.z * m._33 + m._43
        };
    }

    inline Float3 TransformVector(const Float3& v, const Float4x4& m) noexcept
    {
        return {
            v.x * m._11 + v.y * m._21 + v.z * m._31,
            v.x * m._12 + v.y * m._22 + v.z * m._32,
            v.x * m._13 + v.y * m._23 + v.z * m._33
        };
    }
}

namespace GDXMathDX
{
    inline DirectX::XMFLOAT2 ToDX(const GIDX::Float2& v) noexcept { return { v.x, v.y }; }
    inline DirectX::XMFLOAT3 ToDX(const GIDX::Float3& v) noexcept { return { v.x, v.y, v.z }; }
    inline DirectX::XMFLOAT4 ToDX(const GIDX::Float4& v) noexcept { return { v.x, v.y, v.z, v.w }; }
    inline DirectX::XMUINT4  ToDX(const GIDX::UInt4& v)  noexcept { return { v.x, v.y, v.z, v.w }; }

    inline GIDX::Float2 FromDX(const DirectX::XMFLOAT2& v) noexcept { return { v.x, v.y }; }
    inline GIDX::Float3 FromDX(const DirectX::XMFLOAT3& v) noexcept { return { v.x, v.y, v.z }; }
    inline GIDX::Float4 FromDX(const DirectX::XMFLOAT4& v) noexcept { return { v.x, v.y, v.z, v.w }; }
    inline GIDX::UInt4  FromDX(const DirectX::XMUINT4& v)  noexcept { return { v.x, v.y, v.z, v.w }; }

    inline DirectX::XMFLOAT4X4 ToDX(const GIDX::Float4x4& m) noexcept
    {
        DirectX::XMFLOAT4X4 out{};
        std::memcpy(&out, &m, sizeof(out));
        return out;
    }

    inline GIDX::Float4x4 FromDX(const DirectX::XMFLOAT4X4& m) noexcept
    {
        GIDX::Float4x4 out{};
        std::memcpy(&out, &m, sizeof(out));
        return out;
    }
}
