#pragma once

// ---------------------------------------------------------------------------
// CollisionIntersect.h — freie Intersection- und Query-Funktionen.
//
// Alle Funktionen sind zustandslos und engine-unabhängig.
// Shapes liegen in Weltkoordinaten (kein zusätzlicher Transform-Parameter).
//
// Konventionen:
//   - bool-Rückgabe: true = Überlapp / Treffer.
//   - out-Parameter nur befüllt wenn Rückgabe true.
//   - normal zeigt immer vom zweiten Shape (b / box / plane) zum ersten (a / s).
//   - depth  ist die Penetrationstiefe (≥ 0 wenn overlapping).
//
// Implementierung: src/Collision/CollisionIntersect.cpp
// ---------------------------------------------------------------------------

#include "CollisionTypes.h"
#include "CollisionShapes.h"

namespace KROM
{
    // =======================================================================
    // Boolean Overlap — nur true/false, kein Kontaktpunkt.
    // =======================================================================

    // Sphere vs Sphere
    bool OverlapSphereSphere(const SphereShape& a, const SphereShape& b) noexcept;

    // AABB vs AABB
    bool OverlapAABBAABB(const AABBShape& a, const AABBShape& b) noexcept;

    // Sphere vs AABB
    bool OverlapSphereAABB(const SphereShape& s, const AABBShape& box) noexcept;

    // Capsule vs Sphere
    bool OverlapCapsuleSphere(const CapsuleShape& cap, const SphereShape& s) noexcept;

    // Capsule vs AABB
    // Konservativ: testet ob der nächste Punkt auf dem Segment zur AABB
    // innerhalb der um cap.radius erweiterten AABB liegt.
    bool OverlapCapsuleAABB(const CapsuleShape& cap, const AABBShape& box) noexcept;

    // =======================================================================
    // Contact — Überlapp mit Kontaktdaten.
    //
    // outNormal  : normiert, zeigt von b nach a (Trennung-Richtung für a).
    // outDepth   : Penetrationstiefe > 0.
    // outPointA  : repräsentativer Kontaktpunkt auf / in Shape a (Weltkoord.).
    // outPointB  : repräsentativer Kontaktpunkt auf / in Shape b (Weltkoord.).
    // =======================================================================

    // Sphere vs Sphere
    bool ContactSphereSphere(
        const SphereShape& a, const SphereShape& b,
        Float3& outNormal, float& outDepth,
        Float3& outPointA, Float3& outPointB) noexcept;

    // Sphere vs AABB
    bool ContactSphereAABB(
        const SphereShape& s, const AABBShape& box,
        Float3& outNormal, float& outDepth,
        Float3& outPointA, Float3& outPointB) noexcept;

    // =======================================================================
    // Raycast — Ray gegen einzelne Shape.
    //
    // ray.direction muss normiert sein.
    // Gibt true zurück und befüllt outHit (bodyID ist NULL_BODY — wird von
    // CollisionWorld auf den tatsächlichen Body gesetzt).
    // =======================================================================

    bool RaycastSphere (const Ray& ray, const SphereShape&  s,   CollisionHit& outHit) noexcept;
    bool RaycastAABB   (const Ray& ray, const AABBShape&    box, CollisionHit& outHit) noexcept;
    bool RaycastPlane  (const Ray& ray, const PlaneShape&   pl,  CollisionHit& outHit) noexcept;
    bool RaycastCapsule(const Ray& ray, const CapsuleShape& cap, CollisionHit& outHit) noexcept;

    // =======================================================================
    // ShapeCast (Sweep) — Shape wird entlang eines Rays bewegt.
    //
    // Nur für Sphere implementiert (sauberste Variante in dieser Stufe).
    // ray.origin = Startposition des Sphere-Mittelpunkts.
    // ray.direction muss normiert sein.
    // shape.center wird als Offset relativ zu ray.origin interpretiert
    // (üblicherweise { 0,0,0 } wenn Sphere-Mittelpunkt == Sweep-Ursprung).
    // =======================================================================

    // SphereCast vs Sphere
    bool SphereCastSphere(
        const Ray& ray, float sweepRadius,
        const SphereShape& target,
        CollisionHit& outHit) noexcept;

    // SphereCast vs AABB
    bool SphereCastAABB(
        const Ray& ray, float sweepRadius,
        const AABBShape& target,
        CollisionHit& outHit) noexcept;

} // namespace KROM
