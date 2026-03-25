#include "Core/GDXMathOps.h"

#include <cmath>

namespace GDX
{
    float ToRadians(float deg)
    {
        return deg * (PI / 180.0f);
    }

    Float3 Add(const Float3& a, const Float3& b)
    {
        return { a.x + b.x, a.y + b.y, a.z + b.z };
    }

    Float3 Subtract(const Float3& a, const Float3& b)
    {
        return { a.x - b.x, a.y - b.y, a.z - b.z };
    }

    Float3 Multiply(const Float3& v, float s)
    {
        return { v.x * s, v.y * s, v.z * s };
    }

    Float3 Scale3(const Float3& v, float s)
    {
        return Multiply(v, s);
    }

    float Dot3(const Float3& a, const Float3& b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    Float3 Cross3(const Float3& a, const Float3& b)
    {
        return {
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x
        };
    }

    Float3 Cross(const Float3& a, const Float3& b)
    {
        return Cross3(a, b);
    }

    float Length3(const Float3& v)
    {
        return std::sqrt(Dot3(v, v));
    }

    Float3 Normalize3(const Float3& v, const Float3& fallback)
    {
        const float len = Length3(v);
        if (len <= 1.0e-6f)
        {
            return fallback;
        }

        const float inv = 1.0f / len;
        return { v.x * inv, v.y * inv, v.z * inv };
    }

    Float4 QuaternionIdentity()
    {
        return { 0.0f, 0.0f, 0.0f, 1.0f };
    }

    Float4 QuaternionNormalize(const Float4& q)
    {
        const float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
        if (len <= 1.0e-6f)
        {
            return QuaternionIdentity();
        }

        const float inv = 1.0f / len;
        return { q.x * inv, q.y * inv, q.z * inv, q.w * inv };
    }

    Float4 QuaternionMultiply(const Float4& a, const Float4& b)
    {
        return QuaternionNormalize({
            a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
            });
    }

    Float4 QuaternionFromEulerDeg(float pitchDeg, float yawDeg, float rollDeg)
    {
        // Konvention: pitch=X, yaw=Y, roll=Z — identisch mit
        // DirectXMath XMQuaternionRotationRollPitchYaw(pitch, yaw, roll).
        const float pitch = ToRadians(pitchDeg) * 0.5f;
        const float yaw = ToRadians(yawDeg) * 0.5f;
        const float roll = ToRadians(rollDeg) * 0.5f;

        const float sp = std::sin(pitch);
        const float cp = std::cos(pitch);
        const float sy = std::sin(yaw);
        const float cy = std::cos(yaw);
        const float sr = std::sin(roll);
        const float cr = std::cos(roll);

        Float4 q{};
        q.x = cr * sp * cy + sr * cp * sy;  // X-Achse (Pitch)
        q.y = cr * cp * sy - sr * sp * cy;  // Y-Achse (Yaw)
        q.z = sr * cp * cy - cr * sp * sy;  // Z-Achse (Roll)
        q.w = cr * cp * cy + sr * sp * sy;
        return QuaternionNormalize(q);
    }

    Float4 QuaternionFromBasis(const Float3& right, const Float3& up, const Float3& forward)
    {
        const float trace = right.x + up.y + forward.z;
        Float4 q{};
        if (trace > 0.0f)
        {
            const float s = std::sqrt(trace + 1.0f) * 2.0f;
            q.w = 0.25f * s;
            q.x = (up.z - forward.y) / s;
            q.y = (forward.x - right.z) / s;
            q.z = (right.y - up.x) / s;
        }
        else if (right.x > up.y && right.x > forward.z)
        {
            const float s = std::sqrt(1.0f + right.x - up.y - forward.z) * 2.0f;
            q.w = (up.z - forward.y) / s;
            q.x = 0.25f * s;
            q.y = (up.x + right.y) / s;
            q.z = (forward.x + right.z) / s;
        }
        else if (up.y > forward.z)
        {
            const float s = std::sqrt(1.0f + up.y - right.x - forward.z) * 2.0f;
            q.w = (forward.x - right.z) / s;
            q.x = (up.x + right.y) / s;
            q.y = 0.25f * s;
            q.z = (forward.y + up.z) / s;
        }
        else
        {
            const float s = std::sqrt(1.0f + forward.z - right.x - up.y) * 2.0f;
            q.w = (right.y - up.x) / s;
            q.x = (forward.x + right.z) / s;
            q.y = (forward.y + up.z) / s;
            q.z = 0.25f * s;
        }
        return QuaternionNormalize(q);
    }

    Matrix4 MatrixIdentity() { return Matrix4::Identity(); }
    Matrix4 Identity4x4() { return Matrix4::Identity(); }

    Matrix4 MatrixMultiply(const Matrix4& a, const Matrix4& b)
    {
        Matrix4 out{};
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                out.m[r][c] = a.m[r][0] * b.m[0][c] + a.m[r][1] * b.m[1][c] + a.m[r][2] * b.m[2][c] + a.m[r][3] * b.m[3][c];
        return out;
    }
    Matrix4 Multiply(const Matrix4& a, const Matrix4& b) { return MatrixMultiply(a, b); }

    float Determinant(const Matrix4& m)
    {
        return m._11 * Determinant3x3(m._22, m._23, m._24, m._32, m._33, m._34, m._42, m._43, m._44)
            - m._12 * Determinant3x3(m._21, m._23, m._24, m._31, m._33, m._34, m._41, m._43, m._44)
            + m._13 * Determinant3x3(m._21, m._22, m._24, m._31, m._32, m._34, m._41, m._42, m._44)
            - m._14 * Determinant3x3(m._21, m._22, m._23, m._31, m._32, m._33, m._41, m._42, m._43);
    }

    float Determinant3x3(float a00, float a01, float a02,
        float a10, float a11, float a12,
        float a20, float a21, float a22)
    {
        return a00 * (a11 * a22 - a12 * a21) - a01 * (a10 * a22 - a12 * a20) + a02 * (a10 * a21 - a11 * a20);
    }

    Matrix4 MatrixInverse(const Matrix4& m)
    {
        const float c00 = Determinant3x3(m._22, m._23, m._24, m._32, m._33, m._34, m._42, m._43, m._44);
        const float c01 = -Determinant3x3(m._21, m._23, m._24, m._31, m._33, m._34, m._41, m._43, m._44);
        const float c02 = Determinant3x3(m._21, m._22, m._24, m._31, m._32, m._34, m._41, m._42, m._44);
        const float c03 = -Determinant3x3(m._21, m._22, m._23, m._31, m._32, m._33, m._41, m._42, m._43);
        const float det = m._11 * c00 + m._12 * c01 + m._13 * c02 + m._14 * c03;
        if (std::abs(det) <= 1.0e-8f)
            return Matrix4::Identity();
        Matrix4 out{};
        out._11 = c00 / det;
        out._12 = -Determinant3x3(m._12, m._13, m._14, m._32, m._33, m._34, m._42, m._43, m._44) / det;
        out._13 = Determinant3x3(m._12, m._13, m._14, m._22, m._23, m._24, m._42, m._43, m._44) / det;
        out._14 = -Determinant3x3(m._12, m._13, m._14, m._22, m._23, m._24, m._32, m._33, m._34) / det;
        out._21 = c01 / det;
        out._22 = Determinant3x3(m._11, m._13, m._14, m._31, m._33, m._34, m._41, m._43, m._44) / det;
        out._23 = -Determinant3x3(m._11, m._13, m._14, m._21, m._23, m._24, m._41, m._43, m._44) / det;
        out._24 = Determinant3x3(m._11, m._13, m._14, m._21, m._23, m._24, m._31, m._33, m._34) / det;
        out._31 = c02 / det;
        out._32 = -Determinant3x3(m._11, m._12, m._14, m._31, m._32, m._34, m._41, m._42, m._44) / det;
        out._33 = Determinant3x3(m._11, m._12, m._14, m._21, m._22, m._24, m._41, m._42, m._44) / det;
        out._34 = -Determinant3x3(m._11, m._12, m._14, m._21, m._22, m._24, m._31, m._32, m._34) / det;
        out._41 = c03 / det;
        out._42 = Determinant3x3(m._11, m._12, m._13, m._31, m._32, m._33, m._41, m._42, m._43) / det;
        out._43 = -Determinant3x3(m._11, m._12, m._13, m._21, m._22, m._23, m._41, m._42, m._43) / det;
        out._44 = Determinant3x3(m._11, m._12, m._13, m._21, m._22, m._23, m._31, m._32, m._33) / det;
        return out;
    }
    Matrix4 Inverse(const Matrix4& m) { return MatrixInverse(m); }

    Matrix4 MatrixTranslation(const Float3& t)
    {
        Matrix4 out = Matrix4::Identity();
        out._41 = t.x; out._42 = t.y; out._43 = t.z;
        return out;
    }
    Matrix4 Translation(float x, float y, float z) { return MatrixTranslation({ x,y,z }); }

    Matrix4 MatrixScaling(const Float3& s)
    {
        Matrix4 out{};
        out._11 = s.x; out._22 = s.y; out._33 = s.z; out._44 = 1.0f;
        return out;
    }
    Matrix4 Scaling(float x, float y, float z) { return MatrixScaling({ x,y,z }); }

    Matrix4 MatrixRotationQuaternion(const Float4& qn)
    {
        const Float4 q = QuaternionNormalize(qn);
        const float xx = q.x * q.x; const float yy = q.y * q.y; const float zz = q.z * q.z;
        const float xy = q.x * q.y; const float xz = q.x * q.z; const float yz = q.y * q.z;
        const float wx = q.w * q.x; const float wy = q.w * q.y; const float wz = q.w * q.z;
        Matrix4 out = Matrix4::Identity();
        out._11 = 1.0f - 2.0f * (yy + zz); out._12 = 2.0f * (xy + wz); out._13 = 2.0f * (xz - wy);
        out._21 = 2.0f * (xy - wz); out._22 = 1.0f - 2.0f * (xx + zz); out._23 = 2.0f * (yz + wx);
        out._31 = 2.0f * (xz + wy); out._32 = 2.0f * (yz - wx); out._33 = 1.0f - 2.0f * (xx + yy);
        return out;
    }
    Matrix4 RotationQuaternion(const Float4& q) { return MatrixRotationQuaternion(q); }

    Matrix4 MatrixTRS(const Float3& t, const Float4& r, const Float3& s)
    {
        return MatrixMultiply(MatrixMultiply(MatrixScaling(s), MatrixRotationQuaternion(r)), MatrixTranslation(t));
    }

    Matrix4 MatrixLookAtLH(const Float3& eye, const Float3& target, const Float3& up)
    {
        const Float3 zaxis = Normalize3(Subtract(target, eye), { 0.0f, 0.0f, 1.0f });
        const Float3 xaxis = Normalize3(Cross3(up, zaxis), { 1.0f, 0.0f, 0.0f });
        const Float3 yaxis = Cross3(zaxis, xaxis);
        Matrix4 out = Matrix4::Identity();
        out._11 = xaxis.x; out._21 = xaxis.y; out._31 = xaxis.z;
        out._12 = yaxis.x; out._22 = yaxis.y; out._32 = yaxis.z;
        out._13 = zaxis.x; out._23 = zaxis.y; out._33 = zaxis.z;
        out._41 = -Dot3(xaxis, eye); out._42 = -Dot3(yaxis, eye); out._43 = -Dot3(zaxis, eye);
        return out;
    }
    Matrix4 LookAtLH(const Float3& eye, const Float3& target, const Float3& up) { return MatrixLookAtLH(eye, target, up); }

    Matrix4 MatrixPerspectiveFovLH(float fovRad, float aspect, float zNear, float zFar)
    {
        Matrix4 out{};
        const float yScale = 1.0f / std::tan(fovRad * 0.5f);
        const float xScale = yScale / aspect;
        out._11 = xScale; out._22 = yScale; out._33 = zFar / (zFar - zNear); out._34 = 1.0f; out._43 = (-zNear * zFar) / (zFar - zNear);
        return out;
    }
    Matrix4 PerspectiveFovLH(float fovRad, float aspect, float zNear, float zFar) { return MatrixPerspectiveFovLH(fovRad, aspect, zNear, zFar); }

    Matrix4 MatrixOrthographicLH(float width, float height, float zNear, float zFar)
    {
        Matrix4 out = Matrix4::Identity();
        out._11 = 2.0f / width; out._22 = 2.0f / height; out._33 = 1.0f / (zFar - zNear); out._43 = -zNear / (zFar - zNear);
        return out;
    }
    Matrix4 OrthographicLH(float width, float height, float zNear, float zFar) { return MatrixOrthographicLH(width, height, zNear, zFar); }

    Float3 TransformPoint(const Float3& p, const Matrix4& m)
    {
        const float x = p.x * m._11 + p.y * m._21 + p.z * m._31 + m._41;
        const float y = p.x * m._12 + p.y * m._22 + p.z * m._32 + m._42;
        const float z = p.x * m._13 + p.y * m._23 + p.z * m._33 + m._43;
        const float w = p.x * m._14 + p.y * m._24 + p.z * m._34 + m._44;
        if (std::abs(w) > 1.0e-6f) { const float invW = 1.0f / w; return { x * invW, y * invW, z * invW }; }
        return { x, y, z };
    }

    Float3 TransformVector(const Float3& v, const Matrix4& m)
    {
        return { v.x * m._11 + v.y * m._21 + v.z * m._31, v.x * m._12 + v.y * m._22 + v.z * m._32, v.x * m._13 + v.y * m._23 + v.z * m._33 };
    }

    Float4 TransformFloat4(const Float4& v, const Matrix4& m)
    {
        return {
            v.x * m._11 + v.y * m._21 + v.z * m._31 + v.w * m._41,
            v.x * m._12 + v.y * m._22 + v.z * m._32 + v.w * m._42,
            v.x * m._13 + v.y * m._23 + v.z * m._33 + v.w * m._43,
            v.x * m._14 + v.y * m._24 + v.z * m._34 + v.w * m._44
        };
    }

    Float3 GetMatrixTranslation(const Matrix4& m) { return { m._41, m._42, m._43 }; }
    Float3 GetMatrixForward(const Matrix4& m) { return Normalize3({ m._31, m._32, m._33 }, { 0.0f,0.0f,1.0f }); }
    Float3 GetMatrixUp(const Matrix4& m) { return Normalize3({ m._21, m._22, m._23 }, { 0.0f,1.0f,0.0f }); }
    Float3 GetMatrixRight(const Matrix4& m) { return Normalize3({ m._11, m._12, m._13 }, { 1.0f,0.0f,0.0f }); }

    bool DecomposeTRS(const Matrix4& m, Float3& outPos, Float4& outRot, Float3& outScale)
    {
        outPos = GetMatrixTranslation(m);
        const Float3 right = { m._11, m._12, m._13 };
        const Float3 up = { m._21, m._22, m._23 };
        const Float3 forward = { m._31, m._32, m._33 };
        outScale = { Length3(right), Length3(up), Length3(forward) };
        Float3 nr = outScale.x > 1.0e-6f ? Scale3(right, 1.0f / outScale.x) : Float3{ 1,0,0 };
        Float3 nu = outScale.y > 1.0e-6f ? Scale3(up, 1.0f / outScale.y) : Float3{ 0,1,0 };
        Float3 nf = outScale.z > 1.0e-6f ? Scale3(forward, 1.0f / outScale.z) : Float3{ 0,0,1 };
        outRot = QuaternionFromBasis(nr, nu, nf);
        return true;
    }
}
