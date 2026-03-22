#pragma once

#include <cstdint>
#include <cmath>
#include <cstring>

// ---------------------------------------------------------------------------
// GDXMath.h
// Backend-neutrales Math-Modul für giDX³ Engine.
//
// Dieser Header ist vollständig ohne externe Abhängigkeiten kompilierbar.
// Die DirectX-Brücke (GDXMathDX) wird nur aktiviert wenn DirectXMath
// verfügbar ist (Windows-DX-Builds). Für die ECS-Systeme sind ausschließlich
// die GIDX:: Funktionen zu verwenden.
// ---------------------------------------------------------------------------

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

    // Row-major layout (row-vector convention: v * M).
    // Felder _ij: i = Zeile (1-basiert), j = Spalte (1-basiert).
    // Speicherlayout: [_11 _12 _13 _14 | _21 _22 _23 _24 | _31 _32 _33 _34 | _41 _42 _43 _44]
    //                  row0              row1              row2              row3(Translation)
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

    // -------------------------------------------------------------------------
    // Bestehende Float3 / Float4 Hilfsfunktionen (unverändert)
    // -------------------------------------------------------------------------

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

    // TransformPoint: [x y z 1] * M (Translation eingeschlossen)
    inline Float3 TransformPoint(const Float3& p, const Float4x4& m) noexcept
    {
        return {
            p.x * m._11 + p.y * m._21 + p.z * m._31 + m._41,
            p.x * m._12 + p.y * m._22 + p.z * m._32 + m._42,
            p.x * m._13 + p.y * m._23 + p.z * m._33 + m._43
        };
    }

    // TransformVector: [x y z 0] * M (kein Translation, kein W-Divide)
    inline Float3 TransformVector(const Float3& v, const Float4x4& m) noexcept
    {
        return {
            v.x * m._11 + v.y * m._21 + v.z * m._31,
            v.x * m._12 + v.y * m._22 + v.z * m._32,
            v.x * m._13 + v.y * m._23 + v.z * m._33
        };
    }

    // =========================================================================
    // Backend-neutrale Matrix-Fabrik- und Algebra-Funktionen
    //
    // Rein C++17, nur <cmath>. Kein DirectXMath, kein SIMD, kein GLM.
    // Kompatibel mit HLSL "row_major float4x4" cbuffers.
    // =========================================================================

    // Identitätsmatrix
    inline constexpr Float4x4 Identity4x4() noexcept
    {
        return {};
    }

    // Alias (von ECS-Systemen verwendet)
    inline constexpr Float4x4 Identity() noexcept
    {
        return {};
    }

    // Skalierungsmatrix
    inline Float4x4 Scaling(float sx, float sy, float sz) noexcept
    {
        Float4x4 m{};
        m._11 = sx;
        m._22 = sy;
        m._33 = sz;
        m._44 = 1.0f;
        return m;
    }

    // Translationsmatrix (Translation in Zeile 3, row-vector Konvention)
    inline Float4x4 Translation(float tx, float ty, float tz) noexcept
    {
        Float4x4 m{};     // default = identity
        m._41 = tx;
        m._42 = ty;
        m._43 = tz;
        return m;
    }

    // Rotationsmatrix aus Quaternion q = {x, y, z, w}
    // Entspricht XMMatrixRotationQuaternion
    inline Float4x4 RotationQuaternion(const Float4& q) noexcept
    {
        const float x = q.x, y = q.y, z = q.z, w = q.w;
        const float x2 = x * x, y2 = y * y, z2 = z * z;

        Float4x4 m{};
        // Row 0 — X-Basis
        m._11 = 1.0f - 2.0f * (y2 + z2);
        m._12 =        2.0f * (x * y + w * z);
        m._13 =        2.0f * (x * z - w * y);
        m._14 = 0.0f;
        // Row 1 — Y-Basis
        m._21 =        2.0f * (x * y - w * z);
        m._22 = 1.0f - 2.0f * (x2 + z2);
        m._23 =        2.0f * (y * z + w * x);
        m._24 = 0.0f;
        // Row 2 — Z-Basis (Forward)
        m._31 =        2.0f * (x * z + w * y);
        m._32 =        2.0f * (y * z - w * x);
        m._33 = 1.0f - 2.0f * (x2 + y2);
        m._34 = 0.0f;
        // Row 3 — Translation (keine)
        m._41 = 0.0f; m._42 = 0.0f; m._43 = 0.0f; m._44 = 1.0f;
        return m;
    }

    // Matrizenmultiplikation A * B (row-vector: v * A * B)
    // Entspricht XMMatrixMultiply(A, B)
    inline Float4x4 Multiply(const Float4x4& A, const Float4x4& B) noexcept
    {
        // Direkte Verwendung der benannten Felder; der Compiler optimiert das.
        Float4x4 C{};
        // Benutze rohe Pointer für die Schleifen (kein UB: struct ist trivial POD,
        // static_assert sichert kein Padding).
        const float* a = reinterpret_cast<const float*>(&A);
        const float* b = reinterpret_cast<const float*>(&B);
        float*       c = reinterpret_cast<float*>(&C);

        for (int r = 0; r < 4; ++r)
            for (int col = 0; col < 4; ++col)
            {
                float s = 0.0f;
                for (int k = 0; k < 4; ++k)
                    s += a[r * 4 + k] * b[k * 4 + col];
                c[r * 4 + col] = s;
            }
        return C;
    }

    // Transponierte Matrix
    inline Float4x4 Transpose(const Float4x4& A) noexcept
    {
        Float4x4 T{};
        const float* a = reinterpret_cast<const float*>(&A);
        float*       t = reinterpret_cast<float*>(&T);
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                t[c * 4 + r] = a[r * 4 + c];
        return T;
    }

    // Determinante einer 4×4-Matrix
    // Entspricht XMVectorGetX(XMMatrixDeterminant(m))
    inline float Determinant(const Float4x4& m) noexcept
    {
        const float* s = reinterpret_cast<const float*>(&m);
        const float c00 =  s[5] * (s[10]*s[15] - s[11]*s[14])
                          -s[9] * (s[ 6]*s[15] - s[ 7]*s[14])
                          +s[13]* (s[ 6]*s[11] - s[ 7]*s[10]);
        const float c10 = -s[4] * (s[10]*s[15] - s[11]*s[14])
                          +s[8] * (s[ 6]*s[15] - s[ 7]*s[14])
                          -s[12]* (s[ 6]*s[11] - s[ 7]*s[10]);
        const float c20 =  s[4] * (s[ 9]*s[15] - s[11]*s[13])
                          -s[8] * (s[ 5]*s[15] - s[ 7]*s[13])
                          +s[12]* (s[ 5]*s[11] - s[ 7]*s[ 9]);
        const float c30 = -s[4] * (s[ 9]*s[14] - s[10]*s[13])
                          +s[8] * (s[ 5]*s[14] - s[ 6]*s[13])
                          -s[12]* (s[ 5]*s[10] - s[ 6]*s[ 9]);
        return s[0]*c00 + s[1]*c10 + s[2]*c20 + s[3]*c30;
    }

    // Inverse einer 4×4-Matrix (Kofaktor-Methode).
    // Gibt Identity() zurück wenn die Matrix singulär ist.
    // Entspricht XMMatrixInverse(nullptr, m)
    inline Float4x4 Inverse(const Float4x4& m) noexcept
    {
        const float* s = reinterpret_cast<const float*>(&m);
        float inv[16];

        inv[ 0] =  s[ 5]*(s[10]*s[15]-s[11]*s[14]) - s[ 9]*(s[ 6]*s[15]-s[ 7]*s[14]) + s[13]*(s[ 6]*s[11]-s[ 7]*s[10]);
        inv[ 4] = -s[ 4]*(s[10]*s[15]-s[11]*s[14]) + s[ 8]*(s[ 6]*s[15]-s[ 7]*s[14]) - s[12]*(s[ 6]*s[11]-s[ 7]*s[10]);
        inv[ 8] =  s[ 4]*(s[ 9]*s[15]-s[11]*s[13]) - s[ 8]*(s[ 5]*s[15]-s[ 7]*s[13]) + s[12]*(s[ 5]*s[11]-s[ 7]*s[ 9]);
        inv[12] = -s[ 4]*(s[ 9]*s[14]-s[10]*s[13]) + s[ 8]*(s[ 5]*s[14]-s[ 6]*s[13]) - s[12]*(s[ 5]*s[10]-s[ 6]*s[ 9]);

        inv[ 1] = -s[ 1]*(s[10]*s[15]-s[11]*s[14]) + s[ 9]*(s[ 2]*s[15]-s[ 3]*s[14]) - s[13]*(s[ 2]*s[11]-s[ 3]*s[10]);
        inv[ 5] =  s[ 0]*(s[10]*s[15]-s[11]*s[14]) - s[ 8]*(s[ 2]*s[15]-s[ 3]*s[14]) + s[12]*(s[ 2]*s[11]-s[ 3]*s[10]);
        inv[ 9] = -s[ 0]*(s[ 9]*s[15]-s[11]*s[13]) + s[ 8]*(s[ 1]*s[15]-s[ 3]*s[13]) - s[12]*(s[ 1]*s[11]-s[ 3]*s[ 9]);
        inv[13] =  s[ 0]*(s[ 9]*s[14]-s[10]*s[13]) - s[ 8]*(s[ 1]*s[14]-s[ 2]*s[13]) + s[12]*(s[ 1]*s[10]-s[ 2]*s[ 9]);

        inv[ 2] =  s[ 1]*(s[ 6]*s[15]-s[ 7]*s[14]) - s[ 5]*(s[ 2]*s[15]-s[ 3]*s[14]) + s[13]*(s[ 2]*s[ 7]-s[ 3]*s[ 6]);
        inv[ 6] = -s[ 0]*(s[ 6]*s[15]-s[ 7]*s[14]) + s[ 4]*(s[ 2]*s[15]-s[ 3]*s[14]) - s[12]*(s[ 2]*s[ 7]-s[ 3]*s[ 6]);
        inv[10] =  s[ 0]*(s[ 5]*s[15]-s[ 7]*s[13]) - s[ 4]*(s[ 1]*s[15]-s[ 3]*s[13]) + s[12]*(s[ 1]*s[ 7]-s[ 3]*s[ 5]);
        inv[14] = -s[ 0]*(s[ 5]*s[14]-s[ 6]*s[13]) + s[ 4]*(s[ 1]*s[14]-s[ 2]*s[13]) - s[12]*(s[ 1]*s[ 6]-s[ 2]*s[ 5]);

        inv[ 3] = -s[ 1]*(s[ 6]*s[11]-s[ 7]*s[10]) + s[ 5]*(s[ 2]*s[11]-s[ 3]*s[10]) - s[ 9]*(s[ 2]*s[ 7]-s[ 3]*s[ 6]);
        inv[ 7] =  s[ 0]*(s[ 6]*s[11]-s[ 7]*s[10]) - s[ 4]*(s[ 2]*s[11]-s[ 3]*s[10]) + s[ 8]*(s[ 2]*s[ 7]-s[ 3]*s[ 6]);
        inv[11] = -s[ 0]*(s[ 5]*s[11]-s[ 7]*s[ 9]) + s[ 4]*(s[ 1]*s[11]-s[ 3]*s[ 9]) - s[ 8]*(s[ 1]*s[ 7]-s[ 3]*s[ 5]);
        inv[15] =  s[ 0]*(s[ 5]*s[10]-s[ 6]*s[ 9]) - s[ 4]*(s[ 1]*s[10]-s[ 2]*s[ 9]) + s[ 8]*(s[ 1]*s[ 6]-s[ 2]*s[ 5]);

        const float det = s[0]*inv[0] + s[1]*inv[4] + s[2]*inv[8] + s[3]*inv[12];
        if (std::abs(det) < 1e-30f)
            return Identity4x4();

        Float4x4 out{};
        float* o = reinterpret_cast<float*>(&out);
        const float invDet = 1.0f / det;
        for (int i = 0; i < 16; ++i)
            o[i] = inv[i] * invDet;
        return out;
    }

    // View-Matrix (Left-Handed).
    // Entspricht XMMatrixLookAtLH(eye, target, up).
    inline Float4x4 LookAtLH(const Float3& eye, const Float3& target, const Float3& up) noexcept
    {
        const Float3 zAxis = Normalize3(Subtract(target, eye));
        const Float3 xAxis = Normalize3(Cross(up, zAxis));
        const Float3 yAxis = Cross(zAxis, xAxis);

        Float4x4 m{};
        m._11 = xAxis.x;  m._12 = yAxis.x;  m._13 = zAxis.x;  m._14 = 0.0f;
        m._21 = xAxis.y;  m._22 = yAxis.y;  m._23 = zAxis.y;  m._24 = 0.0f;
        m._31 = xAxis.z;  m._32 = yAxis.z;  m._33 = zAxis.z;  m._34 = 0.0f;
        m._41 = -Dot3(xAxis, eye);
        m._42 = -Dot3(yAxis, eye);
        m._43 = -Dot3(zAxis, eye);
        m._44 = 1.0f;
        return m;
    }

    // Perspektivprojektion (Left-Handed, Tiefe [0,1]).
    // Entspricht XMMatrixPerspectiveFovLH(fovY, aspect, nearZ, farZ).
    inline Float4x4 PerspectiveFovLH(float fovY, float aspect, float nearZ, float farZ) noexcept
    {
        const float yScale = 1.0f / std::tan(fovY * 0.5f);
        const float xScale = yScale / aspect;
        const float fRange = farZ / (farZ - nearZ);

        Float4x4 m{};
        m._11 = xScale;
        m._12 = 0.0f;  m._13 = 0.0f;  m._14 = 0.0f;
        m._21 = 0.0f;
        m._22 = yScale;
        m._23 = 0.0f;  m._24 = 0.0f;
        m._31 = 0.0f;  m._32 = 0.0f;
        m._33 = fRange;
        m._34 = 1.0f;   // W_out = Z_in (perspektivische Division)
        m._41 = 0.0f;  m._42 = 0.0f;
        m._43 = -fRange * nearZ;
        m._44 = 0.0f;
        return m;
    }

    // Orthografische Projektion (Left-Handed, Tiefe [0,1]).
    // Entspricht XMMatrixOrthographicLH(w, h, nearZ, farZ).
    inline Float4x4 OrthographicLH(float w, float h, float nearZ, float farZ) noexcept
    {
        const float fRange = 1.0f / (farZ - nearZ);

        Float4x4 m{};
        m._11 = 2.0f / w;
        m._22 = 2.0f / h;
        m._33 = fRange;
        m._43 = -fRange * nearZ;
        m._44 = 1.0f;
        return m;
    }

    // Vollständige 4-Komponenten Vektortransformation: [x y z w] * M
    // Benötigt für NDC-Tiefenberechnung und Frustum-Corner-Entfaltung.
    // Entspricht XMVector4Transform(v, M)
    inline Float4 TransformFloat4(const Float4& v, const Float4x4& M) noexcept
    {
        return {
            v.x * M._11 + v.y * M._21 + v.z * M._31 + v.w * M._41,
            v.x * M._12 + v.y * M._22 + v.z * M._32 + v.w * M._42,
            v.x * M._13 + v.y * M._23 + v.z * M._33 + v.w * M._43,
            v.x * M._14 + v.y * M._24 + v.z * M._34 + v.w * M._44
        };
    }

    // Zerlegt eine SRT-Weltmatrix in Scale, Quaternion-Rotation, Translation.
    // Entspricht XMMatrixDecompose(&scaleV, &rotV, &transV, m).
    // Gibt false zurück bei degenerierter Matrix (Scale ≈ 0).
    inline bool Decompose(const Float4x4&  m,
                           Float3&          outScale,
                           Float4&          outRotation,
                           Float3&          outTranslation) noexcept
    {
        // Translation: Zeile 3
        outTranslation = { m._41, m._42, m._43 };

        // Basisvektoren (obere 3×3)
        const Float3 row0 = { m._11, m._12, m._13 };
        const Float3 row1 = { m._21, m._22, m._23 };
        const Float3 row2 = { m._31, m._32, m._33 };

        float sx = Length3(row0);
        float sy = Length3(row1);
        float sz = Length3(row2);

        if (sx < 1e-8f || sy < 1e-8f || sz < 1e-8f)
            return false;

        // Vorzeichen des Determinanten der oberen 3×3 prüfen (Spiegelung)
        if (Dot3(row0, Cross(row1, row2)) < 0.0f)
            sz = -sz;

        outScale = { sx, sy, sz };

        // Normierte Zeilen = reine Rotationsmatrix
        const Float3 r0 = Scale3(row0, 1.0f / sx);
        const Float3 r1 = Scale3(row1, 1.0f / sy);
        const Float3 r2 = Scale3(row2, 1.0f / std::abs(sz));

        // Rotation → Quaternion (Shepperd's method)
        // r0 = X-Basis, r1 = Y-Basis, r2 = Z-Basis
        const float trace = r0.x + r1.y + r2.z;
        Float4 q{};

        if (trace > 0.0f)
        {
            const float s = 2.0f * std::sqrt(trace + 1.0f);   // s = 4w
            q.w = 0.25f * s;
            q.x = (r1.z - r2.y) / s;
            q.y = (r2.x - r0.z) / s;
            q.z = (r0.y - r1.x) / s;
        }
        else if (r0.x > r1.y && r0.x > r2.z)
        {
            const float s = 2.0f * std::sqrt(1.0f + r0.x - r1.y - r2.z);  // s = 4x
            q.w = (r1.z - r2.y) / s;
            q.x = 0.25f * s;
            q.y = (r0.y + r1.x) / s;
            q.z = (r2.x + r0.z) / s;
        }
        else if (r1.y > r2.z)
        {
            const float s = 2.0f * std::sqrt(1.0f + r1.y - r0.x - r2.z);  // s = 4y
            q.w = (r2.x - r0.z) / s;
            q.x = (r0.y + r1.x) / s;
            q.y = 0.25f * s;
            q.z = (r1.z + r2.y) / s;
        }
        else
        {
            const float s = 2.0f * std::sqrt(1.0f + r2.z - r0.x - r1.y);  // s = 4z
            q.w = (r0.y - r1.x) / s;
            q.x = (r2.x + r0.z) / s;
            q.y = (r1.z + r2.y) / s;
            q.z = 0.25f * s;
        }

        outRotation = NormalizeQuat(q);
        return true;
    }

} // namespace GIDX

// ---------------------------------------------------------------------------
// GDXMathDX — DirectX-Brücke (nur wenn DirectXMath verfügbar ist).
// Wird von GDXMathHelpers.h benötigt.
// ---------------------------------------------------------------------------
#if __has_include(<DirectXMath.h>)
#include <DirectXMath.h>

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
} // namespace GDXMathDX

#endif // __has_include(<DirectXMath.h>)
