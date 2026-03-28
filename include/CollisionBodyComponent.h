#pragma once

// ---------------------------------------------------------------------------
// CollisionBodyComponent — verbindet eine Entity mit der CollisionWorld.
//
// BEWUSST in eigener Datei — nicht in Components.h — um den Circular Include
//   Components.h → Collision/CollisionBody.h → CollisionShapes.h → GDXMath.h
// zu vermeiden, der Registry/ResourceStore-Layouts korrumpiert.
//
// Includet von:
//   - CollisionSystem.h / CollisionSystem.cpp
//   - App-Code der Collision-Bodies anlegt
// ---------------------------------------------------------------------------

#include "Collision/CollisionBody.h"
#include "Collision/CollisionTypes.h"
#include "Collision/CollisionShapes.h"

namespace KROM { }  // ensure KROM types visible

struct CollisionBodyComponent
{
    KROM::CollisionBodyID    bodyID     = {};
    KROM::CollisionShape     localShape = {};
    KROM::CollisionLayerMask layer      = KROM::COLLISION_LAYER_DEFAULT;
    KROM::CollisionLayerMask mask       = KROM::COLLISION_LAYER_ALL;
    bool isTrigger   = false;
    bool isStatic    = true;
    bool isKinematic = false;
    bool registered  = false;  // true sobald CreateBody() erfolgt ist

    CollisionBodyComponent() = default;
};
