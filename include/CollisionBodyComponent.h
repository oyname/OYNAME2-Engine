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

namespace GIDX { }  // ensure GIDX types visible

struct CollisionBodyComponent
{
    GIDX::CollisionBodyID    bodyID     = {};
    GIDX::CollisionShape     localShape = {};
    GIDX::CollisionLayerMask layer      = GIDX::COLLISION_LAYER_DEFAULT;
    GIDX::CollisionLayerMask mask       = GIDX::COLLISION_LAYER_ALL;
    bool isTrigger   = false;
    bool isStatic    = true;
    bool isKinematic = false;
    bool registered  = false;  // true sobald CreateBody() erfolgt ist

    CollisionBodyComponent() = default;
};
