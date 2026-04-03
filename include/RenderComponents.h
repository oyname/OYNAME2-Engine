#pragma once

// ---------------------------------------------------------------------------
// RenderComponents.h — renderer-gekoppelte ECS-Komponenten.
//
// Enthält alle Komponenten die Handle-Typen (MeshHandle, MaterialHandle,
// RenderTargetHandle) oder Renderer-Konzepte (Bounds, Camera, Light) nutzen.
//
// Bewusst GETRENNT von Components.h:
//   Components.h  → ECS-Kern (Tag, Transform, Hierarchy) — renderer-frei
//   RenderComponents.h → Render-Layer (Renderable, Camera, Light, Bounds)
//
// Pattern analog zu CollisionBodyComponent.h.
// ---------------------------------------------------------------------------

#include "ECS/ECSTypes.h"
#include "Handle.h"
#include "Core/GDXMath.h"
#include "SubmeshData.h"
#include "RenderPassClearDesc.h"
#include "RenderPassMask.h"

#include <cstdint>
#include <vector>
#include <cmath>

// ===========================================================================
// RenderableComponent — Mesh/Material/Submesh-Zuweisung einer Entity.
// ===========================================================================
struct RenderableComponent
{
    MeshHandle     mesh;
    MaterialHandle material;
    uint32_t       submeshIndex = 0u;
    bool           enabled = true;

    bool           dirty = true;
    uint32_t       stateVersion = 1u;
    DrawPassMask   drawPassMask = DrawPassBits::Depth | DrawPassBits::Opaque | DrawPassBits::Transparent | DrawPassBits::ShadowDepth | DrawPassBits::MotionVectors;
    uint8_t        renderPriority = 128u;

    RenderableComponent() = default;
    RenderableComponent(MeshHandle meshHandle, MaterialHandle materialHandle, uint32_t slot = 0u)
        : mesh(meshHandle), material(materialHandle), submeshIndex(slot)
    {
    }
};

// ===========================================================================
// SkinComponent — Laufzeit-Bone-Palette für skinnte Meshes.
// ===========================================================================
struct SkinComponent
{
    static constexpr uint32_t MaxBones = 64u;

    std::vector<Matrix4> finalBoneMatrices;
    bool enabled = true;

    SkinComponent() = default;
};

// ===========================================================================
// VisibilityComponent — Render-Sichtbarkeits- und Layer-Flags.
// ===========================================================================
struct VisibilityComponent
{
    bool     visible = true;
    bool     active = true;
    uint32_t layerMask = 0x00000001u;  // LAYER_DEFAULT

    bool castShadows = true;
    bool receiveShadows = true;

    bool     dirty = true;
    uint32_t stateVersion = 1u;

    VisibilityComponent() = default;
};

// ===========================================================================
// RenderBoundsComponent — renderrelevante lokale Bounds für Culling.
// ===========================================================================
struct RenderBoundsComponent
{
    enum class Shape : uint8_t
    {
        Sphere = 0,
        AABB   = 1,
    };

    Shape        shape           = Shape::Sphere;
    Float3 localCenter     = { 0.0f, 0.0f, 0.0f };
    float        localSphereRadius = 0.5f;
    float        boundsScale       = 1.0f;  // Multiplikator für animierte/große Objekte

    Float3 localAabbMin = { -0.5f, -0.5f, -0.5f };
    float        _pad0 = 0.0f;
    Float3 localAabbMax = {  0.5f,  0.5f,  0.5f };
    float        _pad1 = 0.0f;

    bool  valid              = false;
    bool  enableDistanceCull = false;
    float maxViewDistance    = 0.0f;
    float _pad2              = 0.0f;

    RenderBoundsComponent() = default;

    static RenderBoundsComponent MakeSphere(const Float3& center, float radius)
    {
        RenderBoundsComponent b{};
        b.shape            = Shape::Sphere;
        b.localCenter      = center;
        b.localSphereRadius = radius;
        b.valid            = radius > 0.0f;
        return b;
    }

    static RenderBoundsComponent MakeFromSubmeshes(const std::vector<SubmeshData>& submeshes)
    {
        bool hasAny = false;
        Float3 bMin{}, bMax{};

        for (const auto& submesh : submeshes)
        {
            for (const auto& p : submesh.positions)
            {
                if (!hasAny) { bMin = bMax = p; hasAny = true; }
                else
                {
                    if (p.x < bMin.x) bMin.x = p.x; if (p.x > bMax.x) bMax.x = p.x;
                    if (p.y < bMin.y) bMin.y = p.y; if (p.y > bMax.y) bMax.y = p.y;
                    if (p.z < bMin.z) bMin.z = p.z; if (p.z > bMax.z) bMax.z = p.z;
                }
            }
        }

        if (!hasAny) return {};

        const Float3 center = {
            (bMin.x + bMax.x) * 0.5f,
            (bMin.y + bMax.y) * 0.5f,
            (bMin.z + bMax.z) * 0.5f,
        };

        float radius = 0.f;
        for (const auto& submesh : submeshes)
        {
            for (const auto& p : submesh.positions)
            {
                const float dx = p.x - center.x;
                const float dy = p.y - center.y;
                const float dz = p.z - center.z;
                const float d2 = dx*dx + dy*dy + dz*dz;
                if (d2 > radius) radius = d2;
            }
        }
        radius = std::sqrt(radius);
        RenderBoundsComponent b{};
        b.shape             = Shape::AABB;
        b.localCenter       = center;
        b.localAabbMin      = bMin;
        b.localAabbMax      = bMax;
        b.localSphereRadius = radius;  // Sphere als konservativer Fallback
        b.valid             = radius > 0.0f;
        return b;
    }
};

// ===========================================================================
// CameraComponent — Projektionsparameter.
// ===========================================================================
struct CameraComponent
{
    float fovDeg      = 60.0f;
    float nearPlane   = 0.1f;
    float farPlane    = 1000.0f;
    float aspectRatio = 16.0f / 9.0f;

    bool  isOrtho     = false;
    float orthoWidth  = 10.0f;
    float orthoHeight = 10.0f;

    uint32_t cullMask = 0xFFFFFFFFu;  // LAYER_ALL

    CameraComponent() = default;
};

// Genau eine Entity soll diesen Tag tragen.
struct ActiveCameraTag {};

// ===========================================================================
// RenderTargetCameraComponent — Offscreen-Render-Kamera.
// ===========================================================================
struct RenderTargetCameraComponent
{
    RenderTargetHandle target   = RenderTargetHandle::Invalid();
    bool enabled                = true;
    bool autoAspectFromTarget   = true;

    bool renderShadows          = true;
    bool renderOpaque           = true;
    bool renderTransparent      = true;
    bool skipSelfReferentialDraws = true;
    DrawPassMask viewPassMask     = DrawPassBits::Depth | DrawPassBits::Opaque | DrawPassBits::Transparent | DrawPassBits::ShadowDepth | DrawPassBits::MotionVectors | DrawPassBits::Distortion | DrawPassBits::Particles;

    RenderPassClearDesc clear;

    RenderTargetCameraComponent() = default;
    explicit RenderTargetCameraComponent(RenderTargetHandle h) : target(h) {}
};

// ===========================================================================
// LightComponent
// ===========================================================================
enum class LightKind : uint8_t
{
    Directional = 0,
    Point       = 1,
    Spot        = 2,
};

struct LightComponent
{
    LightKind    kind          = LightKind::Directional;
    Float4 diffuseColor  = { 1.0f, 1.0f, 1.0f, 1.0f };

    float radius           = 10.0f;
    float intensity        = 1.0f;
    float innerConeAngle   = 15.0f;
    float outerConeAngle   = 30.0f;

    bool  castShadows        = false;
    float shadowOrthoSize    = 50.0f;
    float shadowNear         = 0.1f;
    float shadowFar          = 1000.0f;

    uint32_t shadowCascadeCount  = 4u;
    float    shadowCascadeLambda = 0.75f;
    uint32_t shadowMapSize       = 2048u;

    uint32_t affectLayerMask = 0xFFFFFFFFu;
    uint32_t shadowLayerMask = 0xFFFFFFFFu;

    LightComponent() = default;
};
