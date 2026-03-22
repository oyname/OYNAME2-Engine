#pragma once

// ---------------------------------------------------------------------------
// CollisionWorld.h — unabhängige Collision World.
//
// Verwaltet Bodies, führt Queries aus und erzeugt Kontaktlisten.
// Keine ECS-Abhängigkeit, kein Renderer-Code, kein DX11/OpenGL.
//
// Broadphase:
//   Lineare Broadphase (AABB-Reject-Test vor Narrow Phase).
//   Einfach, korrekt, ausreichend für kleine Szenen.
//   Kann später durch Uniform Grid oder BVH ersetzt werden,
//   ohne die Außen-API zu ändern.
//
// Nutzungs-Muster:
//   CollisionWorld world;
//   CollisionBodyID box   = world.CreateBody(desc_box);
//   CollisionBodyID ball  = world.CreateBody(desc_ball);
//
//   // Pro Frame:
//   world.UpdateBody(ball, newState);
//
//   CollisionHit hit;
//   if (world.Raycast(ray, filter, hit)) { ... }
//
//   std::vector<CollisionContact> contacts;
//   world.ComputeContacts(filter, contacts);
// ---------------------------------------------------------------------------

#include "CollisionBody.h"
#include "CollisionTypes.h"
#include <vector>
#include <cstdint>

namespace GIDX
{
    // -----------------------------------------------------------------------
    // CollisionWorld
    // -----------------------------------------------------------------------
    class CollisionWorld
    {
    public:
        CollisionWorld()  = default;
        ~CollisionWorld() = default;

        // Non-copyable, movable.
        CollisionWorld(const CollisionWorld&)            = delete;
        CollisionWorld& operator=(const CollisionWorld&) = delete;
        CollisionWorld(CollisionWorld&&)                 = default;
        CollisionWorld& operator=(CollisionWorld&&)      = default;

        // -------------------------------------------------------------------
        // Body-Verwaltung
        // -------------------------------------------------------------------

        // Legt einen neuen Body an und gibt eine gültige ID zurück.
        CollisionBodyID CreateBody(const CollisionBodyDesc& desc) noexcept;

        // Entfernt einen Body. Slot wird für spätere Wiederverwendung freigegeben.
        // Ungültige IDs werden still ignoriert.
        void DestroyBody(CollisionBodyID id) noexcept;

        // Überschreibt den kompletten Zustand eines Bodies (Shape, Layer,
        // Flags, active-Flag).  Wird typischerweise einmal pro Frame gerufen,
        // um Weltkoordinaten-Shapes aus Transforms zu aktualisieren.
        // Gibt false zurück wenn id ungültig.
        bool UpdateBody(CollisionBodyID id, const CollisionBodyState& state) noexcept;

        // Liest den aktuellen Zustand aus.  Gibt false zurück wenn id ungültig.
        bool GetBodyState(CollisionBodyID id, CollisionBodyState& outState) const noexcept;

        // Zahl der aktuell lebendigen Bodies.
        uint32_t BodyCount() const noexcept { return m_liveCount; }

        // -------------------------------------------------------------------
        // Raycast
        // -------------------------------------------------------------------

        // Testet den Ray gegen alle aktiven Bodies und gibt den nächsten Treffer zurück.
        // filter.includeMask bestimmt welche Layer getestet werden.
        // filter.ignoreBody  überspringt genau diesen Body.
        // Gibt true zurück wenn mindestens ein Body getroffen wurde.
        bool Raycast(
            const Ray&                ray,
            const CollisionQueryFilter& filter,
            CollisionHit&             outHit) const noexcept;

        // -------------------------------------------------------------------
        // Overlap Queries
        // -------------------------------------------------------------------

        // Gibt alle aktiven Bodies zurück, deren Shape die Testkugel überlappt.
        // outBodies wird nicht geleert — Treffer werden angehängt.
        void OverlapSphere(
            const SphereShape&          testSphere,
            const CollisionQueryFilter& filter,
            std::vector<CollisionBodyID>& outBodies) const noexcept;

        // Gibt alle aktiven Bodies zurück, deren Shape die Test-AABB überlappt.
        void OverlapAABB(
            const AABBShape&            testBox,
            const CollisionQueryFilter& filter,
            std::vector<CollisionBodyID>& outBodies) const noexcept;

        // -------------------------------------------------------------------
        // Kontaktgenerierung
        // -------------------------------------------------------------------

        // Erzeugt für alle aktiven Body-Paare, die sich überlappen und
        // layer-kompatibel sind, einen CollisionContact-Eintrag.
        // outContacts wird nicht geleert — neue Kontakte werden angehängt.
        //
        // Trigger-Bodies produzieren Einträge mit depth == 0 als Marker,
        // damit der Aufrufer Trigger-Events separat behandeln kann.
        //
        // Hinweis: O(n²) Broadphase — für große Szenen später durch
        //          Uniform Grid / BVH ersetzen (API bleibt identisch).
        void ComputeContacts(
            const CollisionQueryFilter& filter,
            std::vector<CollisionContact>& outContacts) const noexcept;

        // Verwirft alle Bodies. World ist danach leer.
        void Clear() noexcept;

    private:
        // -------------------------------------------------------------------
        // Interner Body-Slot
        // -------------------------------------------------------------------
        struct BodySlot
        {
            CollisionBodyState state       = {};
            uint32_t           generation  = 0u;
            bool               live        = false;
        };

        // -------------------------------------------------------------------
        // Broadphase-Hilfsfunktionen (inline, nur intern sichtbar)
        // -------------------------------------------------------------------

        // Prüft ob eine Body-ID intern gültig (live + korrekte Generation) ist.
        bool IsAlive(CollisionBodyID id) const noexcept;

        // Liefert const-Referenz auf den Slot — nur nach IsAlive() aufrufen.
        const BodySlot& GetSlot(CollisionBodyID id) const noexcept;
              BodySlot& GetSlot(CollisionBodyID id)       noexcept;

        // Broadphase-AABB-Reject: false = sicher kein Overlap.
        bool BroadphaseAABBOverlap(const BodySlot& a, const BodySlot& b) const noexcept;

        // Layer-Kompatibilitäts-Check.
        static bool LayersCompatible(const CollisionBodyState& a, const CollisionBodyState& b) noexcept;

        // -------------------------------------------------------------------
        // Daten
        // -------------------------------------------------------------------
        std::vector<BodySlot> m_slots;
        std::vector<uint32_t> m_freeList;   // freie Slot-Indizes (recycelbar)
        uint32_t              m_liveCount = 0u;
    };

} // namespace GIDX
