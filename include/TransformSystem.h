#pragma once
#include "Components.h"
#include "Registry.h"

class JobSystem;

// ---------------------------------------------------------------------------
// TransformSystem — berechnet WorldTransformComponent aus TransformComponent.
//
// Verantwortlichkeiten:
//   1. Alle dirty Root-Entities (ohne ParentComponent) aktualisieren.
//   2. Dirty-Hierarchien topologisch verarbeiten, so dass Parent immer vor Kind
//      berechnet wird.
//   3. Dirty-Flag nach der Berechnung auf false setzen.
//
// Update muss VOR allen Systemen aufgerufen werden, die WorldTransformComponent
// lesen (Renderer, Physics, Audio, Camera, Light).
//
// Parent-Kind-Reihenfolge:
//   Dirty-Subtrees werden frontier-/level-basiert propagiert. Dadurch gibt es
//   keine harte Hierarchietiefen-Grenze mehr und keine Blind-Scans pro Ebene.
// ---------------------------------------------------------------------------
class TransformSystem
{
public:
    TransformSystem() = default;

    // Haupteinsprungpunkt — pro Frame aufrufen, bevor andere Systeme rendern.
    void Update(Registry& registry, JobSystem* jobSystem = nullptr);

    // Erstellt WorldTransformComponent für alle Entities, die TransformComponent
    // haben aber noch kein WorldTransformComponent.
    // Nützlich beim Setup, damit kein manuelles Add<WorldTransformComponent> nötig ist.
    void EnsureWorldTransforms(Registry& registry);

    // Markiert eine Entity und alle ihre Kind-Entities als dirty.
    // Aufrufen wenn transform.localPosition/Rotation/Scale manuell geändert wird.
    static void MarkDirty(Registry& registry, EntityID id);

private:
    // Berechnet lokale Matrix aus TransformComponent.
    static GIDX::Float4x4 ComputeLocalMatrix(const TransformComponent& t);

    // Aktualisiert eine Entity ohne Parent.
    static void UpdateRoot(TransformComponent& t, WorldTransformComponent& wt);

    // Aktualisiert eine Kind-Entity relativ zum Parent.
    static void UpdateChild(TransformComponent& t, WorldTransformComponent& wt,
                            const WorldTransformComponent& parentWT);
};
