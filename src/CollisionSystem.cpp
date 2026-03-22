#include "CollisionSystem.h"
#include "CollisionBodyComponent.h"
#include "Components.h"
#include "Core/GDXMath.h"

#include <cmath>
#include <algorithm>

// ---------------------------------------------------------------------------
// Hilfsfunktion: maximale Weltskala aus der Weltmatrix extrahieren.
// Wird für Radius-Skalierung bei Sphere und Capsule genutzt.
// ---------------------------------------------------------------------------
static float MaxWorldScale(const GIDX::Float4x4& m) noexcept
{
    const float sx = std::sqrt(m._11*m._11 + m._12*m._12 + m._13*m._13);
    const float sy = std::sqrt(m._21*m._21 + m._22*m._22 + m._23*m._23);
    const float sz = std::sqrt(m._31*m._31 + m._32*m._32 + m._33*m._33);
    return std::max({ sx, sy, sz, 1e-6f });
}

// ---------------------------------------------------------------------------
// TransformShape — localShape (Lokalraum) → Weltkoordinaten-Shape.
// ---------------------------------------------------------------------------
GIDX::CollisionShape CollisionSystem::TransformShape(
    const GIDX::CollisionShape& local,
    const GIDX::Float4x4& worldMatrix)
{
    using namespace GIDX;

    switch (local.type)
    {
    case CollisionShapeType::Sphere:
    {
        // center transformieren, Radius mit maximaler Weltskala skalieren.
        const Float3 worldCenter = TransformPoint(local.sphere.center, worldMatrix);
        const float  worldRadius = local.sphere.radius * MaxWorldScale(worldMatrix);
        return CollisionShape::MakeSphere(worldCenter, worldRadius);
    }

    case CollisionShapeType::AABB:
    {
        // Alle 8 Ecken der lokalen AABB transformieren, neue AABB aufspannen (konservativ).
        const Float3& lo = local.aabb.min;
        const Float3& hi = local.aabb.max;
        const Float3 corners[8] = {
            {lo.x,lo.y,lo.z}, {hi.x,lo.y,lo.z}, {lo.x,hi.y,lo.z}, {hi.x,hi.y,lo.z},
            {lo.x,lo.y,hi.z}, {hi.x,lo.y,hi.z}, {lo.x,hi.y,hi.z}, {hi.x,hi.y,hi.z},
        };
        Float3 wMin = TransformPoint(corners[0], worldMatrix);
        Float3 wMax = wMin;
        for (int i = 1; i < 8; ++i)
        {
            const Float3 wp = TransformPoint(corners[i], worldMatrix);
            wMin.x = std::min(wMin.x, wp.x); wMax.x = std::max(wMax.x, wp.x);
            wMin.y = std::min(wMin.y, wp.y); wMax.y = std::max(wMax.y, wp.y);
            wMin.z = std::min(wMin.z, wp.z); wMax.z = std::max(wMax.z, wp.z);
        }
        return CollisionShape::MakeAABB(wMin, wMax);
    }

    case CollisionShapeType::Capsule:
    {
        // Endpunkte a und b einzeln transformieren.
        // Radius mit maximaler Weltskala skalieren.
        const Float3 wa = TransformPoint(local.capsule.a, worldMatrix);
        const Float3 wb = TransformPoint(local.capsule.b, worldMatrix);
        const float  wr = local.capsule.radius * MaxWorldScale(worldMatrix);
        return CollisionShape::MakeCapsule(wa, wb, wr);
    }

    case CollisionShapeType::Plane:
    {
        // Normal als Vektor (nicht Punkt) transformieren und normieren.
        // d aus einem transformierten Punkt auf der Ebene neu berechnen.
        const Float3 worldNormal = Normalize3(
            TransformVector(local.plane.normal, worldMatrix));
        // Punkt auf der Ebene im Lokalraum: normal * d
        const Float3 localPoint = Scale3(local.plane.normal, local.plane.d);
        const Float3 worldPoint = TransformPoint(localPoint, worldMatrix);
        const float  worldD     = Dot3(worldNormal, worldPoint);
        return CollisionShape::MakePlane(worldNormal, worldD);
    }

    default:
        return local; // Unbekannter Typ — unverändert zurückgeben
    }
}

// ---------------------------------------------------------------------------
// RegisterNewBodies — legt Bodies für noch nicht registrierte Komponenten an.
// ---------------------------------------------------------------------------
void CollisionSystem::RegisterNewBodies(Registry& registry, GIDX::CollisionWorld& world)
{
    registry.View<CollisionBodyComponent>([&](EntityID, CollisionBodyComponent& col)
    {
        if (col.registered) return;

        GIDX::CollisionBodyDesc desc{};
        desc.shape       = col.localShape; // Startposition — wird gleich via UpdateBody überschrieben
        desc.layer       = col.layer;
        desc.mask        = col.mask;
        desc.isTrigger   = col.isTrigger;
        desc.isStatic    = col.isStatic;
        desc.isKinematic = col.isKinematic;

        col.bodyID     = world.CreateBody(desc);
        col.registered = true;
    });
}

// ---------------------------------------------------------------------------
// UpdateBodyTransforms — transformiert localShape → Weltkoordinaten → UpdateBody().
// ---------------------------------------------------------------------------
void CollisionSystem::UpdateBodyTransforms(Registry& registry, GIDX::CollisionWorld& world)
{
    registry.View<CollisionBodyComponent, WorldTransformComponent>(
        [&](EntityID, CollisionBodyComponent& col, WorldTransformComponent& wt)
    {
        if (!col.registered || !col.bodyID.IsValid()) return;

        GIDX::CollisionBodyState state{};
        state.shape       = TransformShape(col.localShape, wt.matrix);
        state.layer       = col.layer;
        state.mask        = col.mask;
        state.isTrigger   = col.isTrigger;
        state.isStatic    = col.isStatic;
        state.isKinematic = col.isKinematic;
        state.active      = true;

        world.UpdateBody(col.bodyID, state);
    });
}

// ---------------------------------------------------------------------------
// Update — Haupteinsprungpunkt.
// ---------------------------------------------------------------------------
void CollisionSystem::Update(
    Registry& registry,
    GIDX::CollisionWorld& world,
    const GIDX::CollisionQueryFilter& filter)
{
    RegisterNewBodies(registry, world);
    UpdateBodyTransforms(registry, world);

    m_contacts.clear();
    world.ComputeContacts(filter, m_contacts);
}

// ---------------------------------------------------------------------------
// Clear — alle registrierten Bodies zerstören.
// ---------------------------------------------------------------------------
void CollisionSystem::Clear(Registry& registry, GIDX::CollisionWorld& world)
{
    registry.View<CollisionBodyComponent>([&](EntityID, CollisionBodyComponent& col)
    {
        if (col.registered && col.bodyID.IsValid())
        {
            world.DestroyBody(col.bodyID);
            col.bodyID     = {};
            col.registered = false;
        }
    });
    m_contacts.clear();
}
