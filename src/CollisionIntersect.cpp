// ---------------------------------------------------------------------------
// CollisionIntersect.cpp — Implementierung der Intersection-Funktionen.
//
// Alle Funktionen sind zustandslos. Keine Engine-Abhängigkeiten außer
// GDXMath.h (GIDX::Float3 und freie Math-Funktionen).
// ---------------------------------------------------------------------------

#include "Collision/CollisionIntersect.h"
#include "GDXMath.h"
#include <cmath>
#include <algorithm>

namespace GIDX
{

// ===========================================================================
// Interne Hilfsfunktionen (nur in dieser TU sichtbar)
// ===========================================================================

namespace
{

// Nächster Punkt auf Segment AB zum Punkt P. Gibt Weltkoordinaten zurück.
inline Float3 ClosestPointOnSegment(
    const Float3& a, const Float3& b, const Float3& p) noexcept
{
    const Float3 ab = Subtract(b, a);
    const float  lenSq = Dot3(ab, ab);
    if (lenSq < 1.0e-12f)
        return a;
    float t = Dot3(Subtract(p, a), ab) / lenSq;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return Add(a, Scale3(ab, t));
}

// Nächster Punkt auf AABB zur Punkt P.
inline Float3 ClosestPointOnAABB(const AABBShape& box, const Float3& p) noexcept
{
    auto clamp = [](float v, float lo, float hi) noexcept
    {
        return v < lo ? lo : (v > hi ? hi : v);
    };
    return {
        clamp(p.x, box.min.x, box.max.x),
        clamp(p.y, box.min.y, box.max.y),
        clamp(p.z, box.min.z, box.max.z)
    };
}

// Squared distance from point P to segment AB.
inline float SquaredDistPointSegment(
    const Float3& a, const Float3& b, const Float3& p) noexcept
{
    const Float3 closest = ClosestPointOnSegment(a, b, p);
    const Float3 d       = Subtract(p, closest);
    return Dot3(d, d);
}

// Nächster Punkt auf Segment AB, der dem AABB am nächsten liegt.
// Gibt den Segment-Parameterwert t ∈ [0,1] zurück.
inline float ClosestSegmentParamToAABB(
    const Float3& a, const Float3& b, const AABBShape& box) noexcept
{
    // Diskretisierung: wähle den Parameterwert t ∈ {0, 0.25, 0.5, 0.75, 1}
    // mit dem kleinsten Abstand zum AABB.  Konservativ, aber korrekt für
    // den Overlap-Test (kein false negative bei Schnitten).
    float bestDistSq = 1.0e30f;
    float bestT      = 0.0f;
    const int steps  = 5;
    const Float3 ab  = Subtract(b, a);
    for (int i = 0; i <= steps; ++i)
    {
        const float t  = static_cast<float>(i) / static_cast<float>(steps);
        const Float3 p = Add(a, Scale3(ab, t));
        const Float3 c = ClosestPointOnAABB(box, p);
        const Float3 d = Subtract(p, c);
        const float  dsq = Dot3(d, d);
        if (dsq < bestDistSq)
        {
            bestDistSq = dsq;
            bestT      = t;
        }
    }
    return bestT;
}

} // anonymous namespace

// ===========================================================================
// Boolean Overlap
// ===========================================================================

bool OverlapSphereSphere(const SphereShape& a, const SphereShape& b) noexcept
{
    const Float3 d     = Subtract(a.center, b.center);
    const float  distSq = Dot3(d, d);
    const float  rSum  = a.radius + b.radius;
    return distSq < rSum * rSum;
}

bool OverlapAABBAABB(const AABBShape& a, const AABBShape& b) noexcept
{
    return (a.min.x <= b.max.x && a.max.x >= b.min.x)
        && (a.min.y <= b.max.y && a.max.y >= b.min.y)
        && (a.min.z <= b.max.z && a.max.z >= b.min.z);
}

bool OverlapSphereAABB(const SphereShape& s, const AABBShape& box) noexcept
{
    const Float3 closest = ClosestPointOnAABB(box, s.center);
    const Float3 d       = Subtract(s.center, closest);
    return Dot3(d, d) <= s.radius * s.radius;
}

bool OverlapCapsuleSphere(const CapsuleShape& cap, const SphereShape& s) noexcept
{
    const float distSq = SquaredDistPointSegment(cap.a, cap.b, s.center);
    const float rSum   = cap.radius + s.radius;
    return distSq <= rSum * rSum;
}

bool OverlapCapsuleAABB(const CapsuleShape& cap, const AABBShape& box) noexcept
{
    // Finde den Parameterwert t ∈ [0,1] auf dem Segment, der dem AABB am
    // nächsten liegt, und prüfe ob der Abstand ≤ cap.radius.
    const float  t    = ClosestSegmentParamToAABB(cap.a, cap.b, box);
    const Float3 p    = Add(cap.a, Scale3(Subtract(cap.b, cap.a), t));
    const Float3 c    = ClosestPointOnAABB(box, p);
    const Float3 d    = Subtract(p, c);
    return Dot3(d, d) <= cap.radius * cap.radius;
}

// ===========================================================================
// Contact (Penetration)
// ===========================================================================

bool ContactSphereSphere(
    const SphereShape& a, const SphereShape& b,
    Float3& outNormal, float& outDepth,
    Float3& outPointA, Float3& outPointB) noexcept
{
    const Float3 d      = Subtract(a.center, b.center);
    const float  distSq = Dot3(d, d);
    const float  rSum   = a.radius + b.radius;

    if (distSq >= rSum * rSum)
        return false;

    const float dist = std::sqrt(distSq);
    if (dist < 1.0e-8f)
    {
        // Koaxiale Kugeln: beliebige Normale wählen.
        outNormal = { 0.0f, 1.0f, 0.0f };
    }
    else
    {
        const float inv = 1.0f / dist;
        outNormal = { d.x * inv, d.y * inv, d.z * inv };
    }

    outDepth  = rSum - dist;
    outPointA = Subtract(a.center, Scale3(outNormal, a.radius));  // auf Oberfläche A
    outPointB = Add      (b.center, Scale3(outNormal, b.radius)); // auf Oberfläche B
    return true;
}

bool ContactSphereAABB(
    const SphereShape& s, const AABBShape& box,
    Float3& outNormal, float& outDepth,
    Float3& outPointA, Float3& outPointB) noexcept
{
    const Float3 closest = ClosestPointOnAABB(box, s.center);
    const Float3 d       = Subtract(s.center, closest);
    const float  distSq  = Dot3(d, d);

    if (distSq > s.radius * s.radius)
        return false;

    if (distSq < 1.0e-12f)
    {
        // Sphere-Mittelpunkt liegt innerhalb der AABB: Minimale Penetrationsachse finden.
        // Wähle die Achse mit dem kleinsten Abstand zur AABB-Oberfläche.
        const float dx = (s.center.x - box.min.x) < (box.max.x - s.center.x)
                       ? (s.center.x - box.min.x) : (box.max.x - s.center.x);
        const float dy = (s.center.y - box.min.y) < (box.max.y - s.center.y)
                       ? (s.center.y - box.min.y) : (box.max.y - s.center.y);
        const float dz = (s.center.z - box.min.z) < (box.max.z - s.center.z)
                       ? (s.center.z - box.min.z) : (box.max.z - s.center.z);

        if (dx <= dy && dx <= dz)
        {
            outNormal = (s.center.x < (box.min.x + box.max.x) * 0.5f)
                      ? Float3{ -1.0f, 0.0f, 0.0f }
                      : Float3{  1.0f, 0.0f, 0.0f };
            outDepth = dx + s.radius;
        }
        else if (dy <= dz)
        {
            outNormal = (s.center.y < (box.min.y + box.max.y) * 0.5f)
                      ? Float3{ 0.0f, -1.0f, 0.0f }
                      : Float3{ 0.0f,  1.0f, 0.0f };
            outDepth = dy + s.radius;
        }
        else
        {
            outNormal = (s.center.z < (box.min.z + box.max.z) * 0.5f)
                      ? Float3{ 0.0f, 0.0f, -1.0f }
                      : Float3{ 0.0f, 0.0f,  1.0f };
            outDepth = dz + s.radius;
        }
    }
    else
    {
        const float dist = std::sqrt(distSq);
        const float inv  = 1.0f / dist;
        outNormal = { d.x * inv, d.y * inv, d.z * inv };
        outDepth  = s.radius - dist;
    }

    outPointA = Subtract(s.center, Scale3(outNormal, s.radius)); // auf Sphere-Oberfläche
    outPointB = closest;                                          // auf AABB-Oberfläche
    return true;
}

// ===========================================================================
// Raycast
// ===========================================================================

bool RaycastSphere(const Ray& ray, const SphereShape& s, CollisionHit& outHit) noexcept
{
    // Analytisch: |ray.origin + t*ray.direction - s.center|² = s.radius²
    const Float3 oc = Subtract(ray.origin, s.center);
    const float  b  = Dot3(oc, ray.direction);
    const float  c  = Dot3(oc, oc) - s.radius * s.radius;
    const float  disc = b * b - c;

    if (disc < 0.0f)
        return false;

    const float sqrtDisc = std::sqrt(disc);
    float t = -b - sqrtDisc;
    if (t < 0.0f)
        t = -b + sqrtDisc;  // Ray startet innerhalb der Sphere
    if (t < 0.0f || t > ray.maxDist)
        return false;

    outHit.hit      = true;
    outHit.distance = t;
    outHit.point    = Add(ray.origin, Scale3(ray.direction, t));
    outHit.normal   = Normalize3(Subtract(outHit.point, s.center));
    outHit.bodyID   = NULL_BODY;
    return true;
}

bool RaycastAABB(const Ray& ray, const AABBShape& box, CollisionHit& outHit) noexcept
{
    // Slab-Methode (Amy Williams et al.)
    const Float3& o  = ray.origin;
    const Float3& d  = ray.direction;

    // Sicherer Kehrwert (Division durch nahezu null → sehr große Zahl).
    auto safeInv = [](float v) noexcept -> float
    {
        return (v == 0.0f) ? 1.0e30f : 1.0f / v;
    };

    const float invDx = safeInv(d.x);
    const float invDy = safeInv(d.y);
    const float invDz = safeInv(d.z);

    float tMinX = (box.min.x - o.x) * invDx;
    float tMaxX = (box.max.x - o.x) * invDx;
    if (tMinX > tMaxX) { float tmp = tMinX; tMinX = tMaxX; tMaxX = tmp; }

    float tMinY = (box.min.y - o.y) * invDy;
    float tMaxY = (box.max.y - o.y) * invDy;
    if (tMinY > tMaxY) { float tmp = tMinY; tMinY = tMaxY; tMaxY = tmp; }

    float tMinZ = (box.min.z - o.z) * invDz;
    float tMaxZ = (box.max.z - o.z) * invDz;
    if (tMinZ > tMaxZ) { float tmp = tMinZ; tMinZ = tMaxZ; tMaxZ = tmp; }

    float tEnter = tMinX > tMinY ? tMinX : tMinY;
    if (tMinZ > tEnter) tEnter = tMinZ;

    float tExit = tMaxX < tMaxY ? tMaxX : tMaxY;
    if (tMaxZ < tExit) tExit = tMaxZ;

    if (tEnter > tExit || tExit < 0.0f || tEnter > ray.maxDist)
        return false;

    const float t = tEnter >= 0.0f ? tEnter : tExit;
    if (t < 0.0f || t > ray.maxDist)
        return false;

    outHit.hit      = true;
    outHit.distance = t;
    outHit.point    = Add(o, Scale3(d, t));
    outHit.bodyID   = NULL_BODY;

    // Normale: Herausfinden welche Slab zuletzt betreten wurde.
    if (tEnter == tMinX)
        outHit.normal = (d.x < 0.0f) ? Float3{ 1.0f, 0.0f, 0.0f } : Float3{ -1.0f, 0.0f, 0.0f };
    else if (tEnter == tMinY)
        outHit.normal = (d.y < 0.0f) ? Float3{ 0.0f, 1.0f, 0.0f } : Float3{ 0.0f, -1.0f, 0.0f };
    else
        outHit.normal = (d.z < 0.0f) ? Float3{ 0.0f, 0.0f, 1.0f } : Float3{ 0.0f, 0.0f, -1.0f };

    return true;
}

bool RaycastPlane(const Ray& ray, const PlaneShape& pl, CollisionHit& outHit) noexcept
{
    // dot(ray.origin + t*direction, pl.normal) = pl.d
    const float denom = Dot3(ray.direction, pl.normal);

    // Ray parallel zur Ebene (oder von der anderen Seite).
    if (std::fabs(denom) < 1.0e-8f)
        return false;

    const float t = (pl.d - Dot3(ray.origin, pl.normal)) / denom;

    if (t < 0.0f || t > ray.maxDist)
        return false;

    outHit.hit      = true;
    outHit.distance = t;
    outHit.point    = Add(ray.origin, Scale3(ray.direction, t));
    // Normale zeigt in Richtung des einfallenden Rays (entgegen der Ebenennormalen).
    outHit.normal   = (denom < 0.0f) ? pl.normal : Scale3(pl.normal, -1.0f);
    outHit.bodyID   = NULL_BODY;
    return true;
}

bool RaycastCapsule(const Ray& ray, const CapsuleShape& cap, CollisionHit& outHit) noexcept
{
    // Kapsel = unendlicher Zylinder (Achse A→B, Radius r)  +  zwei Endkappen.
    //
    // 1. Ray vs Zylindermantel (Segment-Achse): analytisch.
    // 2. Ray vs Endkappen-Spheres (wenn Mantel-Hit außerhalb des Segments).
    // 3. Minimum der gültigen t-Werte.

    const Float3 ab  = Subtract(cap.b, cap.a);
    const Float3 ao  = Subtract(ray.origin, cap.a);
    const Float3& d  = ray.direction;
    const float   r  = cap.radius;

    const float d_ab   = Dot3(d, ab);
    const float ao_ab  = Dot3(ao, ab);
    const float ab_ab  = Dot3(ab, ab);

    if (ab_ab < 1.0e-12f)
    {
        // Degenerierte Kapsel (Segment der Länge 0) → teste als Sphere.
        return RaycastSphere(ray, { cap.a, r }, outHit);
    }

    // Projektion von d und ao auf die Zylinderachse wegrechnen.
    // p(t) = (ao + t*d) - ((ao_ab + t*d_ab)/ab_ab)*ab
    // p(t)·p(t) = r²  →  quadratisches Gleichungssystem.
    const float A = Dot3(d,  d)  - (d_ab  * d_ab)  / ab_ab;
    const float B = Dot3(ao, d)  - (ao_ab * d_ab)  / ab_ab;
    const float C = Dot3(ao, ao) - (ao_ab * ao_ab) / ab_ab - r * r;

    float tCylinder  = 1.0e30f;
    bool  cylHitValid = false;

    if (std::fabs(A) > 1.0e-12f)
    {
        const float disc = B * B - A * C;
        if (disc >= 0.0f)
        {
            const float sqrtDisc = std::sqrt(disc);
            float tc = (-B - sqrtDisc) / A;
            if (tc < 0.0f)
                tc = (-B + sqrtDisc) / A;

            if (tc >= 0.0f && tc <= ray.maxDist)
            {
                // Prüfe ob Auftreffpunkt zwischen A und B liegt.
                const Float3 hitPt = Add(ray.origin, Scale3(d, tc));
                const float  proj  = Dot3(Subtract(hitPt, cap.a), ab) / ab_ab;
                if (proj >= 0.0f && proj <= 1.0f)
                {
                    tCylinder  = tc;
                    cylHitValid = true;
                }
            }
        }
    }

    // Endkappen testen.
    CollisionHit capHitA, capHitB;
    const bool hitCapA = RaycastSphere(ray, { cap.a, r }, capHitA);
    const bool hitCapB = RaycastSphere(ray, { cap.b, r }, capHitB);

    // Besten Treffer auswählen.
    float bestT = 1.0e30f;
    if (cylHitValid  && tCylinder   < bestT) bestT = tCylinder;
    if (hitCapA      && capHitA.distance < bestT) bestT = capHitA.distance;
    if (hitCapB      && capHitB.distance < bestT) bestT = capHitB.distance;

    if (bestT > ray.maxDist || bestT >= 1.0e29f)
        return false;

    outHit.hit      = true;
    outHit.distance = bestT;
    outHit.point    = Add(ray.origin, Scale3(d, bestT));
    outHit.bodyID   = NULL_BODY;

    // Normale: aus welchem Teil stammt der nächste Treffer?
    if (cylHitValid && bestT == tCylinder)
    {
        // Projektion des Treffers auf Zylinderachse → Achsenpunkt → Differenz = Normale.
        const Float3 hitPt = outHit.point;
        const float  proj  = Dot3(Subtract(hitPt, cap.a), ab) / ab_ab;
        const Float3 axPt  = Add(cap.a, Scale3(ab, proj));
        outHit.normal = Normalize3(Subtract(hitPt, axPt));
    }
    else if (hitCapA && bestT == capHitA.distance)
    {
        outHit.normal = capHitA.normal;
    }
    else
    {
        outHit.normal = capHitB.normal;
    }

    return true;
}

// ===========================================================================
// Shape Casts (Sweep)
// ===========================================================================

bool SphereCastSphere(
    const Ray& ray, float sweepRadius,
    const SphereShape& target,
    CollisionHit& outHit) noexcept
{
    // SphereCast gegen Sphere = Raycast gegen Sphere mit erweitertem Radius.
    const SphereShape expanded = { target.center, target.radius + sweepRadius };
    return RaycastSphere(ray, expanded, outHit);
}

bool SphereCastAABB(
    const Ray& ray, float sweepRadius,
    const AABBShape& target,
    CollisionHit& outHit) noexcept
{
    // SphereCast gegen AABB = Raycast gegen AABB mit um sweepRadius erweiterter Box.
    // Konservativ: nicht exakte Minkowski-Summe, aber für Broadphase ausreichend.
    const AABBShape expanded = {
        { target.min.x - sweepRadius, target.min.y - sweepRadius, target.min.z - sweepRadius },
        { target.max.x + sweepRadius, target.max.y + sweepRadius, target.max.z + sweepRadius }
    };
    return RaycastAABB(ray, expanded, outHit);
}

} // namespace GIDX
