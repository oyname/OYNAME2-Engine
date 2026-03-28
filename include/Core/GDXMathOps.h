#pragma once

#include "Core/GDXMath.h"

namespace GDX
{
    constexpr float PI = 3.14159265358979323846f;

    float ToRadians(float deg);

    Float3 Add(const Float3& a, const Float3& b);
    Float3 Subtract(const Float3& a, const Float3& b);
    Float3 Multiply(const Float3& v, float s);
    Float3 Scale3(const Float3& v, float s);

    float  Dot3(const Float3& a, const Float3& b);
    Float3 Cross3(const Float3& a, const Float3& b);
    Float3 Cross(const Float3& a, const Float3& b);
    float  Length3(const Float3& v);
    Float3 Normalize3(const Float3& v, const Float3& fallback = {0.0f, 0.0f, 1.0f});

    Float4 QuaternionIdentity();
    Float4 QuaternionNormalize(const Float4& q);
    Float4 QuaternionMultiply(const Float4& a, const Float4& b);
    Float4 QuaternionFromEulerDeg(float pitchDeg, float yawDeg, float rollDeg);
    Float4 QuaternionFromBasis(const Float3& right, const Float3& up, const Float3& forward);

    Matrix4 MatrixIdentity();
    Matrix4 Identity4x4();
    Matrix4 MatrixMultiply(const Matrix4& a, const Matrix4& b);
    Matrix4 Multiply(const Matrix4& a, const Matrix4& b);
    Matrix4 MatrixInverse(const Matrix4& m);
    Matrix4 Inverse(const Matrix4& m);

    Matrix4 MatrixTranslation(const Float3& t);
    Matrix4 Translation(float x, float y, float z);
    Matrix4 MatrixScaling(const Float3& s);
    Matrix4 Scaling(float x, float y, float z);
    Matrix4 MatrixRotationQuaternion(const Float4& q);
    Matrix4 RotationQuaternion(const Float4& q);
    Matrix4 MatrixTRS(const Float3& t, const Float4& r, const Float3& s);

    Matrix4 MatrixLookAtLH(const Float3& eye, const Float3& target, const Float3& up);
    Matrix4 LookAtLH(const Float3& eye, const Float3& target, const Float3& up);
    Matrix4 MatrixPerspectiveFovLH(float fovRad, float aspect, float zNear, float zFar);
    Matrix4 PerspectiveFovLH(float fovRad, float aspect, float zNear, float zFar);
    Matrix4 MatrixOrthographicLH(float width, float height, float zNear, float zFar);
    Matrix4 OrthographicLH(float width, float height, float zNear, float zFar);

    Float3 TransformPoint(const Float3& p, const Matrix4& m);
    Float3 TransformVector(const Float3& v, const Matrix4& m);
    Float4 TransformFloat4(const Float4& v, const Matrix4& m);

    Float3 GetMatrixTranslation(const Matrix4& m);
    Float3 GetMatrixForward(const Matrix4& m);
    Float3 GetMatrixUp(const Matrix4& m);
    Float3 GetMatrixRight(const Matrix4& m);

    float Determinant(const Matrix4& m);

    float Determinant3x3(float a00, float a01, float a02,
                         float a10, float a11, float a12,
                         float a20, float a21, float a22);

    bool DecomposeTRS(const Matrix4& m, Float3& outPos, Float4& outRot, Float3& outScale);
}

// ---------------------------------------------------------------------------
// KROM-Namespace-Alias — damit GDX::* auch als KROM::* erreichbar ist.
// Float3/Float4/Matrix4 sind global; hier in KROM eingebunden damit
// KROM::Float3 etc. in App-Code kompiliert.
// ---------------------------------------------------------------------------
namespace KROM
{
    using namespace GDX;
    using ::Float2;
    using ::Float3;
    using ::Float4;
    using ::UInt4;
    using ::Matrix4;
}
