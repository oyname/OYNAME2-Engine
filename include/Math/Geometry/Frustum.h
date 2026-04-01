#pragma once
// ---------------------------------------------------------------------------
// Math/Geometry/Frustum.h  --  FrustumPlane + FrustumData
//
// Reine Geometrie-Typen ohne Render-State-Abhängigkeit.
// ---------------------------------------------------------------------------
#include "Core/GDXMath.h"

struct FrustumPlane
{
    Float3 normal = { 0.0f, 0.0f, 1.0f };
    float  d      = 0.0f;
};

struct FrustumData
{
    FrustumPlane planes[6]{};
    bool         valid = false;
};
