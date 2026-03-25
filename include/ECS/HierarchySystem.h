#pragma once
#include "Components.h"
#include "ECS/Registry.h"
#include "Core/GDXMath.h"

// ---------------------------------------------------------------------------
// HierarchySystem — statische Hilfsfunktionen für Parent-Kind-Hierarchien.
//
// Kein Instanzobjekt, kein Manager. Alle Funktionen arbeiten direkt auf der
// Registry — ECS-konform, datenorientiert.
//
// Invarianten die alle Funktionen garantieren:
//   - ParentComponent und ChildrenComponent bleiben stets synchron.
//   - Nach SetParent / Detach ist das betroffene Teilbaum dirty.
//   - Zirkuläre Hierarchien werden verhindert.
//   - Ein Entity kann nicht sein eigener Parent sein.
//
// Nutzung:
//   HierarchySystem::SetParent(registry, child, parent);
//   HierarchySystem::Detach(registry, child, true);   // keepWorld=true
//
// Zusammenspiel mit TransformSystem:
//   HierarchySystem schreibt dirty-Flags.
//   TransformSystem::Update() liest und löscht sie.
//   Reihenfolge pro Frame: HierarchySystem-Ops → TransformSystem::Update()
// ---------------------------------------------------------------------------
class HierarchySystem
{
public:
    HierarchySystem() = delete;   // Nur statische Methoden

    // -----------------------------------------------------------------------
    // SetParent
    //
    // Setzt child als Kind von newParent.
    // Wenn child bereits einen anderen Parent hat, wird dieser korrekt
    // getrennt (ChildrenComponent des alten Parents wird aktualisiert).
    //
    // keepWorldTransform == true:
    //   Berechnet eine neue localTransform so dass die Weltposition des child
    //   unverändert bleibt. Nützlich für Editor-Operationen.
    //   Setzt TransformComponent::dirty = true.
    //
    // Gibt false zurück wenn:
    //   - child == newParent (self-parent)
    //   - newParent ist Nachfahre von child (Zyklus)
    //   - child oder newParent sind nicht alive
    //
    // Gibt true zurück bei Erfolg.
    // -----------------------------------------------------------------------
    static bool SetParent(Registry& registry,
                          EntityID  child,
                          EntityID  newParent,
                          bool      keepWorldTransform = false);

    // -----------------------------------------------------------------------
    // Detach
    //
    // Löst child aus seiner Parent-Hierarchie.
    // Nach dem Aufruf hat child keine ParentComponent mehr (Root-Entity).
    //
    // keepWorldTransform == true:
    //   Setzt localPosition/Rotation/Scale so dass die Weltposition erhalten
    //   bleibt. Die localTransform wird zur ehemaligen Weltmatrix.
    //
    // keepWorldTransform == false:
    //   localTransform bleibt unverändert (Entity springt in Weltkoordinaten).
    //
    // No-op wenn child keinen Parent hat.
    // -----------------------------------------------------------------------
    static void Detach(Registry& registry,
                       EntityID  child,
                       bool      keepWorldTransform = false);

    // -----------------------------------------------------------------------
    // DestroyHierarchy
    //
    // Zerstört ein Entity und rekursiv alle seine Kinder.
    // Bereinigt ParentComponent und ChildrenComponent korrekt.
    // Sicherer als registry.DestroyEntity() direkt bei Elternentities.
    // -----------------------------------------------------------------------
    static void DestroyHierarchy(Registry& registry, EntityID root);

    // -----------------------------------------------------------------------
    // HasCycle
    //
    // Gibt true zurück wenn proposedChild ein Vorfahre von proposedParent ist
    // (d.h. das Setzen von proposedParent als Parent von proposedChild würde
    // eine zirkuläre Hierarchie erzeugen).
    //
    // O(depth der Hierarchie) — keine Rekursion.
    // -----------------------------------------------------------------------
    static bool HasCycle(Registry& registry,
                         EntityID  proposedChild,
                         EntityID  proposedParent);

    // -----------------------------------------------------------------------
    // GetChildren
    //
    // Gibt alle direkten Kinder von parent zurück.
    // Liest aus ChildrenComponent wenn vorhanden (O(1)),
    // fällt auf vollständigen Registry-Scan zurück wenn nicht (O(n)).
    // -----------------------------------------------------------------------
    static std::vector<EntityID> GetChildren(Registry& registry, EntityID parent);

    // -----------------------------------------------------------------------
    // GetAncestors
    //
    // Gibt alle Vorfahren von id zurück (direkt parent, dann parent.parent, …).
    // Reihenfolge: direkt-Parent zuerst, Root zuletzt.
    // -----------------------------------------------------------------------
    static std::vector<EntityID> GetAncestors(Registry& registry, EntityID id);

    // -----------------------------------------------------------------------
    // IsDescendantOf
    //
    // Gibt true zurück wenn child in der Hierarchie unter ancestor liegt.
    // -----------------------------------------------------------------------
    static bool IsDescendantOf(Registry& registry, EntityID child, EntityID ancestor);

    // -----------------------------------------------------------------------
    // GetWorldPosition / GetWorldForward / GetWorldUp / GetWorldRight
    //
    // Convenience-Zugriff auf Weltkoordinaten aus WorldTransformComponent.
    // Gibt { 0,0,0 } bzw. Einheitsvektoren zurück wenn keine WorldTransform.
    //
    // ACHTUNG: Gibt den Wert vom LETZTEN Frame zurück.
    // Korrekte Werte erst nach TransformSystem::Update() im aktuellen Frame.
    // -----------------------------------------------------------------------
    static Float3 GetWorldPosition(Registry& registry, EntityID id);
    static Float3 GetWorldForward (Registry& registry, EntityID id);
    static Float3 GetWorldUp      (Registry& registry, EntityID id);
    static Float3 GetWorldRight   (Registry& registry, EntityID id);

    // -----------------------------------------------------------------------
    // MarkDirtySubtree
    //
    // Markiert ein Entity und seinen gesamten Teilbaum als dirty.
    // Iterativ (kein Rekursionsrisiko). Nutzt ChildrenComponent wenn vorhanden.
    // Öffentlich für Sonderfälle — normalerweise intern aufgerufen.
    // -----------------------------------------------------------------------
    static void MarkDirtySubtree(Registry& registry, EntityID root);

private:
    // Interne Hilfsfunktion: trennt child von seinem aktuellen Parent
    // ohne dirty zu setzen und ohne keepWorld-Logik.
    static void UnlinkFromParent(Registry& registry, EntityID child);

    // Dekomponiert eine Weltmatrix zu localPosition/Rotation/Scale.
    // Schreibt direkt in die TransformComponent.
    static void DecomposeWorldIntoLocal(TransformComponent&     tc,
                                        const Matrix4& worldMatrix);
};
