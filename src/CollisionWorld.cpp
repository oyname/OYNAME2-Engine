// ---------------------------------------------------------------------------
// CollisionWorld.cpp — Implementierung der CollisionWorld.
//
// Broadphase: lineare AABB-Überschneidungsprüfung vor der Narrow Phase.
// Narrow Phase: Intersection-Funktionen aus CollisionIntersect.h.
// Speicher: Slot-Array mit Freelist für Body-Recycling.
// ---------------------------------------------------------------------------

#include "Collision/CollisionWorld.h"
#include "Collision/CollisionIntersect.h"
#include "Collision/CollisionShapes.h"

#include <cassert>
#include <cstdint>

namespace GIDX
{

// ===========================================================================
// Body-Verwaltung
// ===========================================================================

CollisionBodyID CollisionWorld::CreateBody(const CollisionBodyDesc& desc) noexcept
{
    uint32_t slotIndex = 0u;

    if (!m_freeList.empty())
    {
        slotIndex = m_freeList.back();
        m_freeList.pop_back();
        // Generation wurde beim Destroy inkrementiert.
    }
    else
    {
        slotIndex = static_cast<uint32_t>(m_slots.size());
        m_slots.emplace_back();
    }

    BodySlot& slot       = m_slots[slotIndex];
    slot.live            = true;
    slot.state.shape     = desc.shape;
    slot.state.layer     = desc.layer;
    slot.state.mask      = desc.mask;
    slot.state.isTrigger = desc.isTrigger;
    slot.state.isStatic  = desc.isStatic;
    slot.state.isKinematic = desc.isKinematic;
    slot.state.active    = true;

    ++m_liveCount;

    // Index == 0 mit Generation == 0 ist das NULL_BODY-Sentinel.
    // Um Konflikte zu vermeiden: wenn slotIndex == 0, starte Generation bei 1.
    if (slotIndex == 0u && slot.generation == 0u)
        slot.generation = 1u;

    return CollisionBodyID::Make(slotIndex, slot.generation);
}

void CollisionWorld::DestroyBody(CollisionBodyID id) noexcept
{
    if (!id.IsValid())
        return;

    const uint32_t idx = id.Index();
    if (idx >= static_cast<uint32_t>(m_slots.size()))
        return;

    BodySlot& slot = m_slots[idx];
    if (!slot.live || slot.generation != id.Generation())
        return;  // veraltete oder bereits entfernte ID

    slot.live = false;
    slot.state = {};

    // Generation inkrementieren → alte IDs ungültig.
    slot.generation = (slot.generation + 1u) & CollisionBodyID::GEN_MASK;
    if (slot.generation == 0u)
        slot.generation = 1u;  // 0 ist das Sentinel-Muster für NULL_BODY bei Index 0

    m_freeList.push_back(idx);
    --m_liveCount;
}

bool CollisionWorld::UpdateBody(CollisionBodyID id, const CollisionBodyState& state) noexcept
{
    if (!IsAlive(id))
        return false;
    GetSlot(id).state = state;
    return true;
}

bool CollisionWorld::GetBodyState(CollisionBodyID id, CollisionBodyState& outState) const noexcept
{
    if (!IsAlive(id))
        return false;
    outState = GetSlot(id).state;
    return true;
}

void CollisionWorld::Clear() noexcept
{
    m_slots.clear();
    m_freeList.clear();
    m_liveCount = 0u;
}

// ===========================================================================
// Interne Hilfsfunktionen
// ===========================================================================

bool CollisionWorld::IsAlive(CollisionBodyID id) const noexcept
{
    if (!id.IsValid())
        return false;
    const uint32_t idx = id.Index();
    if (idx >= static_cast<uint32_t>(m_slots.size()))
        return false;
    const BodySlot& slot = m_slots[idx];
    return slot.live && slot.generation == id.Generation();
}

const CollisionWorld::BodySlot& CollisionWorld::GetSlot(CollisionBodyID id) const noexcept
{
    return m_slots[id.Index()];
}

CollisionWorld::BodySlot& CollisionWorld::GetSlot(CollisionBodyID id) noexcept
{
    return m_slots[id.Index()];
}

bool CollisionWorld::LayersCompatible(
    const CollisionBodyState& a, const CollisionBodyState& b) noexcept
{
    return (a.layer & b.mask) != 0u && (b.layer & a.mask) != 0u;
}

bool CollisionWorld::BroadphaseAABBOverlap(
    const BodySlot& a, const BodySlot& b) const noexcept
{
    const AABBShape aabb_a = ComputeShapeAABB(a.state.shape);
    const AABBShape aabb_b = ComputeShapeAABB(b.state.shape);
    return OverlapAABBAABB(aabb_a, aabb_b);
}

// ===========================================================================
// Raycast
// ===========================================================================

bool CollisionWorld::Raycast(
    const Ray&                  ray,
    const CollisionQueryFilter& filter,
    CollisionHit&               outHit) const noexcept
{
    bool     found   = false;
    float    bestT   = ray.maxDist;
    CollisionHit bestHit = {};

    const uint32_t count = static_cast<uint32_t>(m_slots.size());
    for (uint32_t i = 0u; i < count; ++i)
    {
        const BodySlot& slot = m_slots[i];
        if (!slot.live || !slot.state.active)
            continue;

        const CollisionBodyID id = CollisionBodyID::Make(i, slot.generation);

        if (id == filter.ignoreBody)
            continue;

        if ((slot.state.layer & filter.includeMask) == 0u)
            continue;

        CollisionHit hit{};
        Ray testRay = ray;
        testRay.maxDist = bestT;

        bool didHit = false;
        switch (slot.state.shape.type)
        {
        case CollisionShapeType::Sphere:
            didHit = RaycastSphere(testRay, slot.state.shape.sphere, hit);
            break;
        case CollisionShapeType::AABB:
            didHit = RaycastAABB(testRay, slot.state.shape.aabb, hit);
            break;
        case CollisionShapeType::Capsule:
            didHit = RaycastCapsule(testRay, slot.state.shape.capsule, hit);
            break;
        case CollisionShapeType::Plane:
            didHit = RaycastPlane(testRay, slot.state.shape.plane, hit);
            break;
        default:
            break;
        }

        if (didHit && hit.distance < bestT)
        {
            bestT       = hit.distance;
            bestHit     = hit;
            bestHit.bodyID = id;
            found       = true;
        }
    }

    if (found)
        outHit = bestHit;

    return found;
}

// ===========================================================================
// Overlap Queries
// ===========================================================================

void CollisionWorld::OverlapSphere(
    const SphereShape&            testSphere,
    const CollisionQueryFilter&   filter,
    std::vector<CollisionBodyID>& outBodies) const noexcept
{
    // Broadphase: AABB der Testkugel.
    const AABBShape testAABB = {
        { testSphere.center.x - testSphere.radius,
          testSphere.center.y - testSphere.radius,
          testSphere.center.z - testSphere.radius },
        { testSphere.center.x + testSphere.radius,
          testSphere.center.y + testSphere.radius,
          testSphere.center.z + testSphere.radius }
    };

    const uint32_t count = static_cast<uint32_t>(m_slots.size());
    for (uint32_t i = 0u; i < count; ++i)
    {
        const BodySlot& slot = m_slots[i];
        if (!slot.live || !slot.state.active)
            continue;

        const CollisionBodyID id = CollisionBodyID::Make(i, slot.generation);

        if (id == filter.ignoreBody)
            continue;
        if ((slot.state.layer & filter.includeMask) == 0u)
            continue;

        // Broadphase reject.
        const AABBShape bodyAABB = ComputeShapeAABB(slot.state.shape);
        if (!OverlapAABBAABB(testAABB, bodyAABB))
            continue;

        // Narrow phase.
        bool overlap = false;
        switch (slot.state.shape.type)
        {
        case CollisionShapeType::Sphere:
            overlap = OverlapSphereSphere(testSphere, slot.state.shape.sphere);
            break;
        case CollisionShapeType::AABB:
            overlap = OverlapSphereAABB(testSphere, slot.state.shape.aabb);
            break;
        case CollisionShapeType::Capsule:
            overlap = OverlapCapsuleSphere(slot.state.shape.capsule, testSphere);
            break;
        case CollisionShapeType::Plane:
        {
            // Sphere vs Plane: signierter Abstand < radius
            const float dist = Dot3(testSphere.center, slot.state.shape.plane.normal)
                             - slot.state.shape.plane.d;
            overlap = dist < testSphere.radius;
            break;
        }
        default:
            break;
        }

        if (overlap)
            outBodies.push_back(id);
    }
}

void CollisionWorld::OverlapAABB(
    const AABBShape&              testBox,
    const CollisionQueryFilter&   filter,
    std::vector<CollisionBodyID>& outBodies) const noexcept
{
    const uint32_t count = static_cast<uint32_t>(m_slots.size());
    for (uint32_t i = 0u; i < count; ++i)
    {
        const BodySlot& slot = m_slots[i];
        if (!slot.live || !slot.state.active)
            continue;

        const CollisionBodyID id = CollisionBodyID::Make(i, slot.generation);

        if (id == filter.ignoreBody)
            continue;
        if ((slot.state.layer & filter.includeMask) == 0u)
            continue;

        // Broadphase: AABB vs AABB.
        const AABBShape bodyAABB = ComputeShapeAABB(slot.state.shape);
        if (!OverlapAABBAABB(testBox, bodyAABB))
            continue;

        // Narrow phase.
        bool overlap = false;
        switch (slot.state.shape.type)
        {
        case CollisionShapeType::Sphere:
            overlap = OverlapSphereAABB(slot.state.shape.sphere, testBox);
            break;
        case CollisionShapeType::AABB:
            overlap = OverlapAABBAABB(testBox, slot.state.shape.aabb);
            break;
        case CollisionShapeType::Capsule:
            overlap = OverlapCapsuleAABB(slot.state.shape.capsule, testBox);
            break;
        case CollisionShapeType::Plane:
            // Plane vs AABB: Ebene schneidet AABB wenn nicht alle Ecken
            // auf derselben Seite liegen.
        {
            const PlaneShape& pl = slot.state.shape.plane;
            // Wähle den positiven / negativen Extrempunkt der AABB in Normalenrichtung.
            const float px = (pl.normal.x >= 0.0f) ? testBox.max.x : testBox.min.x;
            const float py = (pl.normal.y >= 0.0f) ? testBox.max.y : testBox.min.y;
            const float pz = (pl.normal.z >= 0.0f) ? testBox.max.z : testBox.min.z;
            const float nx = (pl.normal.x >= 0.0f) ? testBox.min.x : testBox.max.x;
            const float ny = (pl.normal.y >= 0.0f) ? testBox.min.y : testBox.max.y;
            const float nz = (pl.normal.z >= 0.0f) ? testBox.min.z : testBox.max.z;
            const float dPos = pl.normal.x * px + pl.normal.y * py + pl.normal.z * pz;
            const float dNeg = pl.normal.x * nx + pl.normal.y * ny + pl.normal.z * nz;
            overlap = (dNeg <= pl.d && dPos >= pl.d);
            break;
        }
        default:
            break;
        }

        if (overlap)
            outBodies.push_back(id);
    }
}

// ===========================================================================
// Kontaktgenerierung
// ===========================================================================

void CollisionWorld::ComputeContacts(
    const CollisionQueryFilter&    filter,
    std::vector<CollisionContact>& outContacts) const noexcept
{
    const uint32_t count = static_cast<uint32_t>(m_slots.size());

    for (uint32_t i = 0u; i < count; ++i)
    {
        const BodySlot& slotA = m_slots[i];
        if (!slotA.live || !slotA.state.active)
            continue;
        if ((slotA.state.layer & filter.includeMask) == 0u)
            continue;

        const CollisionBodyID idA = CollisionBodyID::Make(i, slotA.generation);
        if (idA == filter.ignoreBody)
            continue;

        for (uint32_t j = i + 1u; j < count; ++j)
        {
            const BodySlot& slotB = m_slots[j];
            if (!slotB.live || !slotB.state.active)
                continue;
            if ((slotB.state.layer & filter.includeMask) == 0u)
                continue;

            const CollisionBodyID idB = CollisionBodyID::Make(j, slotB.generation);
            if (idB == filter.ignoreBody)
                continue;

            if (!LayersCompatible(slotA.state, slotB.state))
                continue;

            // Broadphase: AABB-Reject.
            if (!BroadphaseAABBOverlap(slotA, slotB))
                continue;

            // Narrow Phase: nur Kombinationen die sauber implementiert sind.
            // Beide Trigger erzeugen keinen Physik-Kontakt → depth = 0 als Marker.
            const CollisionShapeType typeA = slotA.state.shape.type;
            const CollisionShapeType typeB = slotB.state.shape.type;

            Float3 normal = { 0.0f, 1.0f, 0.0f };
            float  depth  = 0.0f;
            Float3 ptA    = {};
            Float3 ptB    = {};
            bool   contact = false;

            if (typeA == CollisionShapeType::Sphere && typeB == CollisionShapeType::Sphere)
            {
                contact = ContactSphereSphere(
                    slotA.state.shape.sphere, slotB.state.shape.sphere,
                    normal, depth, ptA, ptB);
            }
            else if (typeA == CollisionShapeType::Sphere && typeB == CollisionShapeType::AABB)
            {
                contact = ContactSphereAABB(
                    slotA.state.shape.sphere, slotB.state.shape.aabb,
                    normal, depth, ptA, ptB);
            }
            else if (typeA == CollisionShapeType::AABB && typeB == CollisionShapeType::Sphere)
            {
                // Reihenfolge tauschen, Normale umkehren.
                contact = ContactSphereAABB(
                    slotB.state.shape.sphere, slotA.state.shape.aabb,
                    normal, depth, ptB, ptA);
                normal = Scale3(normal, -1.0f);
            }
            else if (typeA == CollisionShapeType::AABB && typeB == CollisionShapeType::AABB)
            {
                // Für AABB vs AABB: SAT auf 3 Achsen.
                const AABBShape& ba = slotA.state.shape.aabb;
                const AABBShape& bb = slotB.state.shape.aabb;

                const float overlapX = (ba.max.x < bb.max.x ? ba.max.x : bb.max.x)
                                     - (ba.min.x > bb.min.x ? ba.min.x : bb.min.x);
                const float overlapY = (ba.max.y < bb.max.y ? ba.max.y : bb.max.y)
                                     - (ba.min.y > bb.min.y ? ba.min.y : bb.min.y);
                const float overlapZ = (ba.max.z < bb.max.z ? ba.max.z : bb.max.z)
                                     - (ba.min.z > bb.min.z ? ba.min.z : bb.min.z);

                if (overlapX > 0.0f && overlapY > 0.0f && overlapZ > 0.0f)
                {
                    contact = true;
                    // Minimale Penetrationsachse.
                    if (overlapX <= overlapY && overlapX <= overlapZ)
                    {
                        const float cA = (ba.min.x + ba.max.x) * 0.5f;
                        const float cB = (bb.min.x + bb.max.x) * 0.5f;
                        normal = (cA < cB) ? Float3{ -1.0f, 0.0f, 0.0f }
                                           : Float3{  1.0f, 0.0f, 0.0f };
                        depth  = overlapX;
                    }
                    else if (overlapY <= overlapZ)
                    {
                        const float cA = (ba.min.y + ba.max.y) * 0.5f;
                        const float cB = (bb.min.y + bb.max.y) * 0.5f;
                        normal = (cA < cB) ? Float3{ 0.0f, -1.0f, 0.0f }
                                           : Float3{ 0.0f,  1.0f, 0.0f };
                        depth  = overlapY;
                    }
                    else
                    {
                        const float cA = (ba.min.z + ba.max.z) * 0.5f;
                        const float cB = (bb.min.z + bb.max.z) * 0.5f;
                        normal = (cA < cB) ? Float3{ 0.0f, 0.0f, -1.0f }
                                           : Float3{ 0.0f, 0.0f,  1.0f };
                        depth  = overlapZ;
                    }
                    // Repräsentative Kontaktpunkte: Mittelpunkte der Überlapp-Region.
                    ptA = { (ba.min.x + ba.max.x) * 0.5f,
                            (ba.min.y + ba.max.y) * 0.5f,
                            (ba.min.z + ba.max.z) * 0.5f };
                    ptB = { (bb.min.x + bb.max.x) * 0.5f,
                            (bb.min.y + bb.max.y) * 0.5f,
                            (bb.min.z + bb.max.z) * 0.5f };
                }
            }
            else if (typeA == CollisionShapeType::Capsule && typeB == CollisionShapeType::Sphere)
            {
                // Capsule vs Sphere über den nächsten Punkt auf dem Segment.
                const CapsuleShape& cap = slotA.state.shape.capsule;
                const SphereShape&  sph = slotB.state.shape.sphere;

                const Float3 ab      = Subtract(cap.b, cap.a);
                const float  lenSq   = Dot3(ab, ab);
                Float3       closest = cap.a;
                if (lenSq > 1.0e-12f)
                {
                    float t = Dot3(Subtract(sph.center, cap.a), ab) / lenSq;
                    if (t < 0.0f) t = 0.0f;
                    if (t > 1.0f) t = 1.0f;
                    closest = Add(cap.a, Scale3(ab, t));
                }

                SphereShape capsuleSphere = { closest, cap.radius };
                contact = ContactSphereSphere(capsuleSphere, sph, normal, depth, ptA, ptB);
                // normal zeigt von sph nach capsuleSphere → für A→B: umkehren.
                normal = Scale3(normal, -1.0f);
                Float3 tmp = ptA; ptA = ptB; ptB = tmp;
            }
            else if (typeA == CollisionShapeType::Sphere && typeB == CollisionShapeType::Capsule)
            {
                const SphereShape&  sph = slotA.state.shape.sphere;
                const CapsuleShape& cap = slotB.state.shape.capsule;

                const Float3 ab    = Subtract(cap.b, cap.a);
                const float  lenSq = Dot3(ab, ab);
                Float3 closest = cap.a;
                if (lenSq > 1.0e-12f)
                {
                    float t = Dot3(Subtract(sph.center, cap.a), ab) / lenSq;
                    if (t < 0.0f) t = 0.0f;
                    if (t > 1.0f) t = 1.0f;
                    closest = Add(cap.a, Scale3(ab, t));
                }

                SphereShape capsuleSphere = { closest, cap.radius };
                contact = ContactSphereSphere(sph, capsuleSphere, normal, depth, ptA, ptB);
            }
            // Kombinationen mit Plane oder Capsule vs AABB nicht in dieser Ausbaustufe
            // implementiert — kein false positive erzeugen, lieber weglassen.

            if (!contact)
                continue;

            CollisionContact c{};
            c.bodyA    = idA;
            c.bodyB    = idB;
            c.pointOnA = ptA;
            c.pointOnB = ptB;
            c.normal   = normal;
            // Trigger-Bodies: depth auf 0 setzen als Marker für den Aufrufer.
            c.depth    = (slotA.state.isTrigger || slotB.state.isTrigger) ? 0.0f : depth;

            outContacts.push_back(c);
        }
    }
}

} // namespace GIDX
