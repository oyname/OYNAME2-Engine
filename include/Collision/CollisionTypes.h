#pragma once

// ---------------------------------------------------------------------------
// CollisionTypes.h — Basis-Datentypen des Collision-Moduls.
//
// Design-Regeln:
//   - Kein ECS, kein Renderer, kein DX11/OpenGL.
//   - Alle Typen sind Plain Data.
//   - CollisionBodyID folgt dem Handle-Schema der Engine (20 Bit Index,
//     12 Bit Generation), ist aber ein eigenständiger Typ ohne Template-
//     Tag, damit das Modul keinerlei Engine-Abhängigkeit hat.
// ---------------------------------------------------------------------------

#include <cstdint>
#include "Core/GDXMath.h"

namespace KROM
{
    // -----------------------------------------------------------------------
    // CollisionLayerMask — 32-Bit-Bitfeld. Jedes Bit repräsentiert einen
    // Kollisions-Layer.  Konvention: Bit 0 = Layer "Default".
    // -----------------------------------------------------------------------
    using CollisionLayerMask = uint32_t;

    constexpr CollisionLayerMask COLLISION_LAYER_NONE    = 0x00000000u;
    constexpr CollisionLayerMask COLLISION_LAYER_DEFAULT = 0x00000001u;
    constexpr CollisionLayerMask COLLISION_LAYER_ALL     = 0xFFFFFFFFu;

    // -----------------------------------------------------------------------
    // CollisionBodyID — generations-sicherer Handle für Collision Bodies.
    //
    // Layout (32-Bit):
    //   Bits 31–12 : Index      (20 Bit  →  max. 1 048 575 Bodies)
    //   Bits 11– 0 : Generation (12 Bit  →  4 096 Recyclings pro Slot)
    //
    // Ein Wert von 0 (Index=0, Generation=0) gilt als ungültig.
    // -----------------------------------------------------------------------
    struct CollisionBodyID
    {
        static constexpr uint32_t INDEX_BITS = 20u;
        static constexpr uint32_t GEN_BITS   = 12u;
        static constexpr uint32_t INDEX_MASK = (1u << INDEX_BITS) - 1u;   // 0x000FFFFF
        static constexpr uint32_t GEN_MASK   = (1u << GEN_BITS)  - 1u;   // 0x00000FFF

        uint32_t value = 0u;

        CollisionBodyID() = default;
        constexpr explicit CollisionBodyID(uint32_t raw) : value(raw) {}

        static CollisionBodyID Make(uint32_t index, uint32_t generation) noexcept
        {
            return CollisionBodyID{ ((index & INDEX_MASK) << GEN_BITS) | (generation & GEN_MASK) };
        }

        uint32_t Index()      const noexcept { return (value >> GEN_BITS) & INDEX_MASK; }
        uint32_t Generation() const noexcept { return value & GEN_MASK; }

        bool IsValid() const noexcept { return value != 0u; }
        explicit operator bool() const noexcept { return IsValid(); }

        bool operator==(const CollisionBodyID& o) const noexcept { return value == o.value; }
        bool operator!=(const CollisionBodyID& o) const noexcept { return value != o.value; }
        bool operator< (const CollisionBodyID& o) const noexcept { return value <  o.value; }

        static CollisionBodyID Invalid() noexcept { return {}; }
    };

    // Sentinel für ungültige Body-IDs.
    inline constexpr CollisionBodyID NULL_BODY{};

    // -----------------------------------------------------------------------
    // Ray — Ursprung + normierte Richtung + maximale Distanz.
    //
    // direction muss normiert sein, bevor er an Query-Funktionen übergeben wird.
    // -----------------------------------------------------------------------
    struct Ray
    {
        Float3 origin    = { 0.0f, 0.0f, 0.0f };
        Float3 direction = { 0.0f, 0.0f, 1.0f };  // muss normiert sein
        float  maxDist   = 1.0e30f;
    };

    // -----------------------------------------------------------------------
    // Segment — Liniensegment von start nach end.
    // -----------------------------------------------------------------------
    struct Segment
    {
        Float3 start = { 0.0f, 0.0f, 0.0f };
        Float3 end   = { 0.0f, 0.0f, 1.0f };
    };

    // -----------------------------------------------------------------------
    // CollisionHit — Ergebnis eines Raycasts oder Shape-Casts.
    //
    // Nur gültig wenn hit == true.
    // -----------------------------------------------------------------------
    struct CollisionHit
    {
        bool            hit      = false;
        float           distance = 0.0f;   // Distanz entlang des Rays
        Float3          point    = {};      // Weltkoordinaten des Auftreffpunkts
        Float3          normal   = {};      // Normierte Oberflächennormale (nach außen)
        CollisionBodyID bodyID   = {};      // Getroffener Body (NULL_BODY wenn unbekannt)
    };

    // -----------------------------------------------------------------------
    // CollisionContact — Kontaktpunkt zwischen zwei Bodies.
    //
    // normal zeigt von bodyB in Richtung bodyA (Standard-Konvention).
    // depth > 0  →  Überlappung; depth == 0  →  flüchtiger Kontakt.
    // -----------------------------------------------------------------------
    struct CollisionContact
    {
        CollisionBodyID bodyA    = {};
        CollisionBodyID bodyB    = {};
        Float3          pointOnA = {};     // Weltkoordinaten, Punkt auf A
        Float3          pointOnB = {};     // Weltkoordinaten, Punkt auf B
        Float3          normal   = {};     // Normiert, von B nach A
        float           depth    = 0.0f;  // Penetrationstiefe  (>0 = Überlapp)
    };

    // -----------------------------------------------------------------------
    // CollisionQueryFilter — steuert, welche Bodies eine Query trifft.
    // -----------------------------------------------------------------------
    struct CollisionQueryFilter
    {
        CollisionLayerMask includeMask = COLLISION_LAYER_ALL;  // Layer-Whitelist
        CollisionBodyID    ignoreBody  = {};                    // optional: diesen Body überspringen
    };

} // namespace KROM
