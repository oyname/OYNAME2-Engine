#pragma once

// ---------------------------------------------------------------------------
// CollisionBody.h — Beschreibung und Laufzustand eines Collision Bodies.
//
// CollisionBodyDesc  — wird einmalig beim Anlegen übergeben.
// CollisionBodyState — Laufzeit-Zustand; wird pro Frame via UpdateBody()
//                      aktualisiert. Shapes liegen in Weltkoordinaten.
//
// Absichtlich KEINE:
//   - Velocity / Impuls (das gehört in ein Physics-System, nicht in den
//     reinen Collision-Kern).
//   - ECS-EntityID oder andere Engine-Abhängigkeiten.
//   - Pointer auf Meshes, Materialien oder Shader-Ressourcen.
// ---------------------------------------------------------------------------

#include "CollisionTypes.h"
#include "CollisionShapes.h"

namespace GIDX
{
    // -----------------------------------------------------------------------
    // CollisionBodyDesc — Initialisierungsparameter für CreateBody().
    //
    // shape:       Kollisionsgeometrie in Weltkoordinaten.
    // layer:       Eigener Kollisions-Layer dieses Bodies (Bitmask, 1 Bit).
    // mask:        Welche Layer testen gegen diesen Body (Bitmask).
    //              Ein Test zwischen Body A und Body B findet nur statt wenn:
    //                  (A.layer & B.mask) != 0  &&  (B.layer & A.mask) != 0
    // isTrigger:   Kein physikalischer Response — erzeugt aber Overlap-Events.
    // isStatic:    Geometry bewegt sich nicht. Broadphase kann Optimierungen
    //              nutzen (künftig), Update ist aber trotzdem möglich.
    // isKinematic: Wird extern bewegt (z. B. via UpdateBody), nicht vom
    //              Physics-Solver. Nur relevant wenn Physics-Solver ergänzt wird.
    // -----------------------------------------------------------------------
    struct CollisionBodyDesc
    {
        CollisionShape     shape       = {};
        CollisionLayerMask layer       = COLLISION_LAYER_DEFAULT;
        CollisionLayerMask mask        = COLLISION_LAYER_ALL;
        bool               isTrigger   = false;
        bool               isStatic    = true;
        bool               isKinematic = false;
    };

    // -----------------------------------------------------------------------
    // CollisionBodyState — vollständiger Laufzeit-Zustand eines Bodies.
    //
    // Wird von CollisionWorld intern gehalten und via GetBodyState() ausgegeben.
    // Die shape liegt IMMER in Weltkoordinaten — der Aufrufer ist für die
    // Transformation von Lokalraum in Weltkoordinaten zuständig.
    //
    // active == false → Body wird bei Queries und ComputeContacts übersprungen,
    //                   bleibt aber in der World registriert.
    // -----------------------------------------------------------------------
    struct CollisionBodyState
    {
        CollisionShape     shape       = {};
        CollisionLayerMask layer       = COLLISION_LAYER_DEFAULT;
        CollisionLayerMask mask        = COLLISION_LAYER_ALL;
        bool               isTrigger   = false;
        bool               isStatic    = true;
        bool               isKinematic = false;
        bool               active      = true;
    };

} // namespace GIDX
