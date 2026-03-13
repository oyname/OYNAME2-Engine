#include "HierarchySystem.h"
#include <vector>
#include <cstring>

using namespace DirectX;

// ---------------------------------------------------------------------------
// DecomposeWorldIntoLocal
//
// Zerlegt eine row-major Weltmatrix in Position, Quaternion-Rotation und Scale.
// Schreibt das Ergebnis direkt in tc und setzt dirty = true.
//
// Mathematik:
//   - Scale:    Länge der Basisvektoren (Zeilen 0-2 der Matrix)
//   - Rotation: Matrix ohne Scale → XMQuaternionRotationMatrix
//   - Position: Zeile 3 (row-major, Translation am Ende)
// ---------------------------------------------------------------------------
void HierarchySystem::DecomposeWorldIntoLocal(TransformComponent&      tc,
                                               const XMFLOAT4X4& worldMatrix)
{
    XMVECTOR scaleV, rotV, transV;
    const XMMATRIX m = XMLoadFloat4x4(&worldMatrix);

    if (!XMMatrixDecompose(&scaleV, &rotV, &transV, m))
    {
        // Degenerate matrix (z. B. Scale = 0) — Fallback auf Identity-Werte
        tc.localPosition = { 0.0f, 0.0f, 0.0f };
        tc.localRotation = { 0.0f, 0.0f, 0.0f, 1.0f };
        tc.localScale    = { 1.0f, 1.0f, 1.0f };
    }
    else
    {
        XMStoreFloat3(&tc.localPosition, transV);
        XMStoreFloat4(&tc.localRotation, rotV);
        XMStoreFloat3(&tc.localScale,    scaleV);
    }
    tc.dirty = true;
}

// ---------------------------------------------------------------------------
// UnlinkFromParent — internes Trennen ohne dirty / keepWorld
// ---------------------------------------------------------------------------
void HierarchySystem::UnlinkFromParent(Registry& registry, EntityID child)
{
    auto* pc = registry.Get<ParentComponent>(child);
    if (!pc) return;

    const EntityID oldParent = pc->parent;

    // ChildrenComponent des alten Parents aktualisieren
    if (oldParent != NULL_ENTITY && registry.IsAlive(oldParent))
    {
        auto* cc = registry.Get<ChildrenComponent>(oldParent);
        if (cc)
        {
            cc->Remove(child);
            if (cc->Count() == 0)
                registry.Remove<ChildrenComponent>(oldParent);
        }
    }

    registry.Remove<ParentComponent>(child);
}

// ---------------------------------------------------------------------------
// HasCycle
// ---------------------------------------------------------------------------
bool HierarchySystem::HasCycle(Registry& registry,
                                EntityID  proposedChild,
                                EntityID  proposedParent)
{
    // Laufe die Ahnen-Kette von proposedParent aufwärts.
    // Wenn wir proposedChild treffen, wäre ein Zyklus die Folge.
    EntityID current = proposedParent;
    while (current != NULL_ENTITY && registry.IsAlive(current))
    {
        if (current == proposedChild) return true;
        const auto* pc = registry.Get<ParentComponent>(current);
        current = pc ? pc->parent : NULL_ENTITY;
    }
    return false;
}

// ---------------------------------------------------------------------------
// MarkDirtySubtree — iterativ, BFS, nutzt ChildrenComponent wenn vorhanden
// ---------------------------------------------------------------------------
void HierarchySystem::MarkDirtySubtree(Registry& registry, EntityID root)
{
    // Expliziter Stack statt Rekursion — kein Stack-Overflow-Risiko.
    std::vector<EntityID> stack;
    stack.reserve(16);
    stack.push_back(root);

    while (!stack.empty())
    {
        const EntityID cur = stack.back();
        stack.pop_back();

        if (!registry.IsAlive(cur)) continue;

        auto* t = registry.Get<TransformComponent>(cur);
        if (t) t->dirty = true;

        // Fast path: ChildrenComponent vorhanden → O(1) Kindlookup
        if (const auto* cc = registry.Get<ChildrenComponent>(cur))
        {
            for (EntityID child : cc->children)
                stack.push_back(child);
        }
        else if (cur != root)
        {
            // Slow path: nur nötig wenn ChildrenComponent fehlt (Mischbetrieb)
            registry.View<TransformComponent, ParentComponent>(
                [&](EntityID id, TransformComponent&, ParentComponent& pc)
                {
                    if (pc.parent == cur)
                        stack.push_back(id);
                });
        }
    }
}

// ---------------------------------------------------------------------------
// SetParent
// ---------------------------------------------------------------------------
bool HierarchySystem::SetParent(Registry& registry,
                                 EntityID  child,
                                 EntityID  newParent,
                                 bool      keepWorldTransform)
{
    // Validierung
    if (!registry.IsAlive(child) || !registry.IsAlive(newParent)) return false;
    if (child == newParent)                                         return false;  // self-parent
    if (HasCycle(registry, child, newParent))                       return false;  // Zyklus

    // Bereits derselbe Parent? → nur dirty setzen
    if (const auto* pc = registry.Get<ParentComponent>(child))
        if (pc->parent == newParent) return true;

    // -----------------------------------------------------------------------
    // keepWorldTransform: neue localMatrix = childWorld * inv(newParentWorld)
    // Muss VOR dem Umhängen berechnet werden solange die alten Matrizen noch
    // gültig sind.
    // -----------------------------------------------------------------------
    XMFLOAT4X4 newLocalMatrix = {};
    bool        hasNewLocal   = false;

    if (keepWorldTransform)
    {
        const auto* childWT     = registry.Get<WorldTransformComponent>(child);
        const auto* newParentWT = registry.Get<WorldTransformComponent>(newParent);

        if (childWT && newParentWT)
        {
            const XMMATRIX childWorld     = XMLoadFloat4x4(&childWT->matrix);
            const XMMATRIX parentWorldInv = XMMatrixInverse(nullptr,
                                                XMLoadFloat4x4(&newParentWT->matrix));
            const XMMATRIX localM         = XMMatrixMultiply(childWorld, parentWorldInv);
            XMStoreFloat4x4(&newLocalMatrix, localM);
            hasNewLocal = true;
        }
    }

    // Alten Parent trennen (bereinigt ChildrenComponent des alten Parents)
    UnlinkFromParent(registry, child);

    // Neuen Parent setzen
    registry.Add<ParentComponent>(child, ParentComponent(newParent));

    // ChildrenComponent des neuen Parents aktualisieren
    auto* cc = registry.Get<ChildrenComponent>(newParent);
    if (!cc)
    {
        registry.Add<ChildrenComponent>(newParent);
        cc = registry.Get<ChildrenComponent>(newParent);
    }
    cc->Add(child);

    // keepWorldTransform: lokale TRS aus berechneter neuer LocalMatrix ableiten
    if (hasNewLocal)
    {
        auto* tc = registry.Get<TransformComponent>(child);
        if (tc)
            DecomposeWorldIntoLocal(*tc, newLocalMatrix);
    }

    // Gesamten Teilbaum als dirty markieren
    MarkDirtySubtree(registry, child);

    return true;
}

// ---------------------------------------------------------------------------
// Detach
// ---------------------------------------------------------------------------
void HierarchySystem::Detach(Registry& registry,
                              EntityID  child,
                              bool      keepWorldTransform)
{
    if (!registry.IsAlive(child))              return;
    if (!registry.Has<ParentComponent>(child)) return;   // kein Parent → no-op

    // keepWorldTransform: aktuelle Weltmatrix als neue lokale Matrix verwenden
    if (keepWorldTransform)
    {
        const auto* wt = registry.Get<WorldTransformComponent>(child);
        auto*       tc = registry.Get<TransformComponent>(child);

        if (wt && tc)
            DecomposeWorldIntoLocal(*tc, wt->matrix);
    }

    // Vom Parent trennen
    UnlinkFromParent(registry, child);

    // Als Root-Entity dirty markieren (kein Teilbaum nötig, aber Kinder propagieren)
    MarkDirtySubtree(registry, child);
}

// ---------------------------------------------------------------------------
// DestroyHierarchy
// ---------------------------------------------------------------------------
void HierarchySystem::DestroyHierarchy(Registry& registry, EntityID root)
{
    if (!registry.IsAlive(root)) return;

    // Iterativer Post-Order-Durchlauf: Kinder zuerst zerstören, dann Parent.
    // Nutzt ChildrenComponent für den Aufbau der Traversal-Liste.
    std::vector<EntityID> toDestroy;
    std::vector<EntityID> stack = { root };

    while (!stack.empty())
    {
        const EntityID cur = stack.back();
        stack.pop_back();
        toDestroy.push_back(cur);

        if (const auto* cc = registry.Get<ChildrenComponent>(cur))
        {
            // Kopie nötig da ChildrenComponent sich beim Zerstören ändert
            const std::vector<EntityID> childCopy = cc->children;
            for (EntityID child : childCopy)
                if (registry.IsAlive(child))
                    stack.push_back(child);
        }
        else
        {
            // Slow path für Entities ohne ChildrenComponent
            registry.View<ParentComponent>(
                [&](EntityID id, ParentComponent& pc)
                {
                    if (pc.parent == cur && registry.IsAlive(id))
                        stack.push_back(id);
                });
        }
    }

    // Vom eigenen Parent des Root-Nodes trennen
    UnlinkFromParent(registry, root);

    // Alle gesammelten Entities zerstören (Kinder zuerst wegen Post-Order)
    // toDestroy ist Pre-Order → umkehren für Post-Order
    for (auto it = toDestroy.rbegin(); it != toDestroy.rend(); ++it)
    {
        if (registry.IsAlive(*it))
            registry.DestroyEntity(*it);
    }
}

// ---------------------------------------------------------------------------
// GetChildren
// ---------------------------------------------------------------------------
std::vector<EntityID> HierarchySystem::GetChildren(Registry& registry, EntityID parent)
{
    // Fast path: ChildrenComponent vorhanden
    if (const auto* cc = registry.Get<ChildrenComponent>(parent))
        return cc->children;

    // Slow path: vollständiger Scan
    std::vector<EntityID> result;
    registry.View<ParentComponent>(
        [&](EntityID id, ParentComponent& pc)
        {
            if (pc.parent == parent) result.push_back(id);
        });
    return result;
}

// ---------------------------------------------------------------------------
// GetAncestors
// ---------------------------------------------------------------------------
std::vector<EntityID> HierarchySystem::GetAncestors(Registry& registry, EntityID id)
{
    std::vector<EntityID> result;
    EntityID current = id;

    // Guard gegen defekte Hierarchien: max 256 Ebenen
    constexpr int MAX_DEPTH = 256;
    for (int i = 0; i < MAX_DEPTH; ++i)
    {
        const auto* pc = registry.Get<ParentComponent>(current);
        if (!pc || pc->parent == NULL_ENTITY || !registry.IsAlive(pc->parent)) break;
        result.push_back(pc->parent);
        current = pc->parent;
    }
    return result;
}

// ---------------------------------------------------------------------------
// IsDescendantOf
// ---------------------------------------------------------------------------
bool HierarchySystem::IsDescendantOf(Registry& registry, EntityID child, EntityID ancestor)
{
    EntityID current = child;
    constexpr int MAX_DEPTH = 256;

    for (int i = 0; i < MAX_DEPTH; ++i)
    {
        const auto* pc = registry.Get<ParentComponent>(current);
        if (!pc || pc->parent == NULL_ENTITY) return false;
        if (pc->parent == ancestor)           return true;
        current = pc->parent;
    }
    return false;
}

// ---------------------------------------------------------------------------
// GetWorldPosition
// ---------------------------------------------------------------------------
XMFLOAT3 HierarchySystem::GetWorldPosition(Registry& registry, EntityID id)
{
    const auto* wt = registry.Get<WorldTransformComponent>(id);
    if (!wt) return { 0.0f, 0.0f, 0.0f };
    // row-major: Translation in Zeile 3
    return { wt->matrix._41, wt->matrix._42, wt->matrix._43 };
}

// ---------------------------------------------------------------------------
// GetWorldForward (−Z in LH, also +Z-Achse der Matrix)
// ---------------------------------------------------------------------------
XMFLOAT3 HierarchySystem::GetWorldForward(Registry& registry, EntityID id)
{
    const auto* wt = registry.Get<WorldTransformComponent>(id);
    if (!wt) return { 0.0f, 0.0f, 1.0f };

    const XMMATRIX m = XMLoadFloat4x4(&wt->matrix);
    const XMVECTOR forward = XMVector3Normalize(m.r[2]);
    XMFLOAT3 result;
    XMStoreFloat3(&result, forward);
    return result;
}

// ---------------------------------------------------------------------------
// GetWorldUp (+Y)
// ---------------------------------------------------------------------------
XMFLOAT3 HierarchySystem::GetWorldUp(Registry& registry, EntityID id)
{
    const auto* wt = registry.Get<WorldTransformComponent>(id);
    if (!wt) return { 0.0f, 1.0f, 0.0f };

    const XMMATRIX m = XMLoadFloat4x4(&wt->matrix);
    const XMVECTOR up = XMVector3Normalize(m.r[1]);
    XMFLOAT3 result;
    XMStoreFloat3(&result, up);
    return result;
}

// ---------------------------------------------------------------------------
// GetWorldRight (+X)
// ---------------------------------------------------------------------------
XMFLOAT3 HierarchySystem::GetWorldRight(Registry& registry, EntityID id)
{
    const auto* wt = registry.Get<WorldTransformComponent>(id);
    if (!wt) return { 1.0f, 0.0f, 0.0f };

    const XMMATRIX m = XMLoadFloat4x4(&wt->matrix);
    const XMVECTOR right = XMVector3Normalize(m.r[0]);
    XMFLOAT3 result;
    XMStoreFloat3(&result, right);
    return result;
}
