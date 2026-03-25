#pragma once

// ---------------------------------------------------------------------------
// CollisionSystem — ECS-Integration des Collision-Moduls.
//
// Verantwortlichkeiten pro Frame:
//   a) Neue Bodies registrieren  (registered == false → CreateBody)
//   b) Bodies zerstörter Entities bereinigen
//   c) WorldTransformComponent → Weltkoordinaten-Shape → UpdateBody()
//   d) ComputeContacts() → internen Contact-Buffer füllen
//
// Reihenfolge-Constraint: muss NACH TransformSystem::Update() laufen.
// Kein Singleton, kein globaler State.
// ---------------------------------------------------------------------------

#include "ECS/Registry.h"
#include "Core/GDXMath.h"
#include "CollisionBodyComponent.h"
#include "Collision/CollisionWorld.h"
#include "Collision/CollisionTypes.h"

#include <vector>

class CollisionSystem
{
public:
    CollisionSystem() = default;

    // Haupteinsprungpunkt — einmal pro Frame nach TransformSystem::Update().
    // filter steuert welche Layer bei ComputeContacts berücksichtigt werden.
    void Update(Registry& registry,
        GIDX::CollisionWorld& world,
        const GIDX::CollisionQueryFilter& filter = { GIDX::COLLISION_LAYER_ALL, {} });

    // Zerstört alle registrierten Bodies und räumt die World auf.
    // Aufrufen wenn die Szene gewechselt wird oder die Engine herunterfährt.
    void Clear(Registry& registry, GIDX::CollisionWorld& world);

    // Ergebnis des letzten ComputeContacts()-Aufrufs.
    const std::vector<GIDX::CollisionContact>& GetContacts() const { return m_contacts; }

private:
    void RegisterNewBodies(Registry& registry, GIDX::CollisionWorld& world);
    void UpdateBodyTransforms(Registry& registry, GIDX::CollisionWorld& world);

    // Transformiert localShape (Lokalraum) → Weltkoordinaten-Shape.
    static GIDX::CollisionShape TransformShape(
        const GIDX::CollisionShape& local,
        const Matrix4& worldMatrix);

    std::vector<GIDX::CollisionContact> m_contacts;
};