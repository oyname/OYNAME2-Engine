#pragma once

// ---------------------------------------------------------------------------
// CollisionShapes.h — Shape-Datentypen des Collision-Moduls.
//
// Unterstützte Shapes dieser Ausbaustufe:
//   SphereShape   — Kugel mit Mittelpunkt + Radius
//   AABBShape     — achsenparallele Bounding Box (min/max in Weltkoordinaten)
//   CapsuleShape  — Liniensegment A→B + Radius
//   PlaneShape    — unendliche Ebene (normal + Abstand d zum Ursprung)
//
// Shapes enthalten direkt Weltkoordinaten-Daten.
// Der aufrufende Code (z. B. CollisionWorld) ist für die Transformation
// aus Lokal- in Weltkoordinaten zuständig, bevor er die Shape übergeben
// oder in CollisionBodyState ablegt.
//
// Kein OBB, kein Triangle Mesh, kein Convex Hull in dieser Stufe.
// ---------------------------------------------------------------------------

#include <cstdint>
#include "GDXMath.h"

namespace GIDX
{
    // -----------------------------------------------------------------------
    // CollisionShapeType — Diskriminator für den CollisionShape-Container.
    // -----------------------------------------------------------------------
    enum class CollisionShapeType : uint8_t
    {
        Sphere  = 0,
        AABB    = 1,
        Capsule = 2,
        Plane   = 3,
    };

    // -----------------------------------------------------------------------
    // SphereShape — Kugel.
    // -----------------------------------------------------------------------
    struct SphereShape
    {
        Float3 center = { 0.0f, 0.0f, 0.0f };
        float  radius = 0.5f;
    };

    // -----------------------------------------------------------------------
    // AABBShape — Axis-Aligned Bounding Box (Weltkoordinaten).
    //
    // Invariante: min <= max komponentenweise.
    // -----------------------------------------------------------------------
    struct AABBShape
    {
        Float3 min = { -0.5f, -0.5f, -0.5f };
        Float3 max = {  0.5f,  0.5f,  0.5f };
    };

    // -----------------------------------------------------------------------
    // CapsuleShape — Liniensegment A→B mit uniformem Radius.
    //
    // Die Kapsel ist die Menge aller Punkte mit Abstand ≤ radius zum Segment.
    // a und b liegen in Weltkoordinaten.
    // -----------------------------------------------------------------------
    struct CapsuleShape
    {
        Float3 a      = { 0.0f, -0.5f, 0.0f };
        Float3 b      = { 0.0f,  0.5f, 0.0f };
        float  radius = 0.25f;
    };

    // -----------------------------------------------------------------------
    // PlaneShape — unendliche Ebene.
    //
    // Ebenengleichung: dot(normal, p) = d
    //   → d > 0  : Ebene liegt in Normalenrichtung versetzt vom Ursprung
    //   → d == 0 : Ebene durch den Ursprung
    //   → d < 0  : Ebene entgegengesetzt zur Normalenrichtung vom Ursprung
    //
    // normal muss normiert sein.
    // -----------------------------------------------------------------------
    struct PlaneShape
    {
        Float3 normal = { 0.0f, 1.0f, 0.0f };   // normiert
        float  d      = 0.0f;
    };

    // -----------------------------------------------------------------------
    // CollisionShape — getaggte Union aller Shape-Typen.
    //
    // Bewusst ohne virtuelle Methoden. Diskriminierung über type-Feld.
    // Fabrik-Methoden für komfortablen Aufbau.
    // -----------------------------------------------------------------------
    struct CollisionShape
    {
        CollisionShapeType type = CollisionShapeType::Sphere;

        union
        {
            SphereShape  sphere;
            AABBShape    aabb;
            CapsuleShape capsule;
            PlaneShape   plane;
        };

        // Standard-Konstruktor: Einheits-Kugel.
        CollisionShape() noexcept : type(CollisionShapeType::Sphere), sphere{} {}

        // --- Fabrik-Methoden ------------------------------------------------

        static CollisionShape MakeSphere(const Float3& center, float radius) noexcept
        {
            CollisionShape s;
            s.type          = CollisionShapeType::Sphere;
            s.sphere.center = center;
            s.sphere.radius = radius;
            return s;
        }

        static CollisionShape MakeAABB(const Float3& min, const Float3& max) noexcept
        {
            CollisionShape s;
            s.type    = CollisionShapeType::AABB;
            s.aabb.min = min;
            s.aabb.max = max;
            return s;
        }

        static CollisionShape MakeCapsule(const Float3& a, const Float3& b, float radius) noexcept
        {
            CollisionShape s;
            s.type           = CollisionShapeType::Capsule;
            s.capsule.a      = a;
            s.capsule.b      = b;
            s.capsule.radius = radius;
            return s;
        }

        static CollisionShape MakePlane(const Float3& normal, float d) noexcept
        {
            CollisionShape s;
            s.type         = CollisionShapeType::Plane;
            s.plane.normal = normal;
            s.plane.d      = d;
            return s;
        }

        // Convenience: AABB aus Mittelpunkt + halber Ausdehnung (half-extents).
        static CollisionShape MakeAABBCentered(const Float3& center, const Float3& halfExtents) noexcept
        {
            return MakeAABB(
                { center.x - halfExtents.x, center.y - halfExtents.y, center.z - halfExtents.z },
                { center.x + halfExtents.x, center.y + halfExtents.y, center.z + halfExtents.z }
            );
        }
    };

    // -----------------------------------------------------------------------
    // Hilfsfunktionen: AABB aus Shape berechnen.
    //
    // Wird intern von Broadphase und World für schnelle Reject-Tests genutzt.
    // Die Berechnung ist konservativ (outer bound).
    // -----------------------------------------------------------------------
    inline AABBShape ComputeShapeAABB(const CollisionShape& shape) noexcept
    {
        switch (shape.type)
        {
        case CollisionShapeType::Sphere:
        {
            const Float3& c = shape.sphere.center;
            const float   r = shape.sphere.radius;
            return {
                { c.x - r, c.y - r, c.z - r },
                { c.x + r, c.y + r, c.z + r }
            };
        }
        case CollisionShapeType::AABB:
            return shape.aabb;

        case CollisionShapeType::Capsule:
        {
            const CapsuleShape& cap = shape.capsule;
            const float r = cap.radius;
            AABBShape bounds;
            bounds.min = {
                (cap.a.x < cap.b.x ? cap.a.x : cap.b.x) - r,
                (cap.a.y < cap.b.y ? cap.a.y : cap.b.y) - r,
                (cap.a.z < cap.b.z ? cap.a.z : cap.b.z) - r
            };
            bounds.max = {
                (cap.a.x > cap.b.x ? cap.a.x : cap.b.x) + r,
                (cap.a.y > cap.b.y ? cap.a.y : cap.b.y) + r,
                (cap.a.z > cap.b.z ? cap.a.z : cap.b.z) + r
            };
            return bounds;
        }
        case CollisionShapeType::Plane:
            // Ebene ist unendlich — AABB ist symbolisch sehr groß.
            return {
                { -1.0e15f, -1.0e15f, -1.0e15f },
                {  1.0e15f,  1.0e15f,  1.0e15f }
            };

        default:
            return {};
        }
    }

} // namespace GIDX
