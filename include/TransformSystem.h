#pragma once
#include "Components.h"
#include "Registry.h"

// ---------------------------------------------------------------------------
// TransformSystem — berechnet WorldTransformComponent aus TransformComponent.
//
// Verantwortlichkeiten:
//   1. Alle Root-Entities (ohne ParentComponent) bei dirty=true aktualisieren.
//   2. Alle Kind-Entities (mit ParentComponent) nachgelagert aktualisieren,
//      so dass Parent immer vor Kind verarbeitet wird.
//   3. dirty-Flag nach der Berechnung auf false setzen.
//
// Update muss VOR allen Systemen aufgerufen werden, die WorldTransformComponent
// lesen (Renderer, Physics, Audio, Camera, Light).
//
// Parent-Kind-Reihenfolge (Phase 1):
//   Einfache Implementierung: mehrere Iterationen (max. Hierarchietiefe).
//   Für tiefe Hierarchien besser: topologische Sortierung (Phase 7).
// ---------------------------------------------------------------------------
class TransformSystem
{
public:
    TransformSystem() = default;

    // Haupteinsprungpunkt — pro Frame aufrufen, bevor andere Systeme rendern.
    void Update(Registry& registry);

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

    // Maximale Hierarchietiefe für die iterative Parent-Propagation.
    static constexpr int MAX_HIERARCHY_DEPTH = 8;
};
