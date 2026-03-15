#include "TransformSystem.h"
#include "Components.h"
#include "GDXMathHelpers.h"
#include <vector>

using namespace DirectX;

// ---------------------------------------------------------------------------
// ComputeLocalMatrix — baut XMFLOAT4X4 aus Position, Quaternion, Scale.
// ---------------------------------------------------------------------------
GIDX::Float4x4 TransformSystem::ComputeLocalMatrix(const TransformComponent& t)
{
    // Scale → Rotation (Quaternion) → Translation: SRT-Matrixkette.
    const XMVECTOR s = GDXMathHelpers::LoadFloat3(t.localScale);
    const XMVECTOR r = GDXMathHelpers::LoadFloat4(t.localRotation);  // Quaternion
    const XMVECTOR p = GDXMathHelpers::LoadFloat3(t.localPosition);

    XMMATRIX m = XMMatrixScalingFromVector(s)
        * XMMatrixRotationQuaternion(r)
        * XMMatrixTranslationFromVector(p);

    GIDX::Float4x4 result;
    GDXMathHelpers::StoreFloat4x4(result, m);
    return result;
}

// ---------------------------------------------------------------------------
// UpdateRoot — Root-Entity ohne Parent.
// ---------------------------------------------------------------------------
void TransformSystem::UpdateRoot(TransformComponent& t, WorldTransformComponent& wt)
{
    wt.matrix = ComputeLocalMatrix(t);

    XMMATRIX m = GDXMathHelpers::LoadFloat4x4(wt.matrix);
    XMMATRIX inv = XMMatrixInverse(nullptr, m);
    GDXMathHelpers::StoreFloat4x4(wt.inverse, inv);

    t.dirty = false;
}

// ---------------------------------------------------------------------------
// UpdateChild — Kind-Entity: local * parent.world
// ---------------------------------------------------------------------------
void TransformSystem::UpdateChild(TransformComponent& t, WorldTransformComponent& wt,
    const WorldTransformComponent& parentWT)
{
    GIDX::Float4x4 local = ComputeLocalMatrix(t);

    XMMATRIX localM = GDXMathHelpers::LoadFloat4x4(local);
    XMMATRIX parentM = GDXMathHelpers::LoadFloat4x4(parentWT.matrix);
    XMMATRIX worldM = XMMatrixMultiply(localM, parentM);

    GDXMathHelpers::StoreFloat4x4(wt.matrix, worldM);

    XMMATRIX inv = XMMatrixInverse(nullptr, worldM);
    GDXMathHelpers::StoreFloat4x4(wt.inverse, inv);

    t.dirty = false;
}

// ---------------------------------------------------------------------------
// EnsureWorldTransforms — WorldTransformComponent für alle Entities anlegen,
// die TransformComponent haben, aber noch kein WorldTransformComponent.
// ---------------------------------------------------------------------------
void TransformSystem::EnsureWorldTransforms(Registry& registry)
{
    // Wir sammeln erst die IDs, dann fügen wir Komponenten hinzu,
    // damit wir den Pool während der Iteration nicht modifizieren.
    std::vector<EntityID> missing;

    registry.View<TransformComponent>([&](EntityID id, TransformComponent&)
        {
            if (!registry.Has<WorldTransformComponent>(id))
                missing.push_back(id);
        });

    for (EntityID id : missing)
        registry.Add<WorldTransformComponent>(id);
}

// ---------------------------------------------------------------------------
// Update — Haupteinsprungpunkt.
// ---------------------------------------------------------------------------
void TransformSystem::Update(Registry& registry)
{
    // Sicherstellen dass alle Transform-Entities auch WorldTransform haben.
    EnsureWorldTransforms(registry);

    // -----------------------------------------------------------------------
    // Schritt 1: Root-Entities (kein ParentComponent, dirty=true).
    // -----------------------------------------------------------------------
    registry.View<TransformComponent, WorldTransformComponent>(
        [&](EntityID id, TransformComponent& t, WorldTransformComponent& wt)
        {
            if (registry.Has<ParentComponent>(id)) return;  // Hat Parent → überspringen
            if (!t.dirty) return;
            UpdateRoot(t, wt);
        });

    // -----------------------------------------------------------------------
    // Schritt 2: Kind-Entities.
    //
    // Einfache Implementierung: MAX_HIERARCHY_DEPTH Iterationen.
    // Jede Iteration schreibt eine weitere Hierarchieebene.
    // Kind-Entities werden als dirty markiert, wenn ihr Parent dirty war.
    //
    // Limitation: Zirkuläre Hierarchien führen zu MAX_HIERARCHY_DEPTH
    // Iterationen ohne Konvergenz (kein Schutz hier — Verantwortung beim Caller).
    // -----------------------------------------------------------------------
    for (int depth = 0; depth < MAX_HIERARCHY_DEPTH; ++depth)
    {
        bool anyUpdated = false;

        registry.View<TransformComponent, WorldTransformComponent, ParentComponent>(
            [&](EntityID /*id*/, TransformComponent& t, WorldTransformComponent& wt,
                ParentComponent& pc)
            {
                if (!t.dirty) return;

                // Explizite Typen statt auto* — MSVC-Analyzer akzeptiert keine
                // auto* aus Registry::Get innerhalb von Lambdas ohne Warnung.
                const WorldTransformComponent* parentWT = registry.Get<WorldTransformComponent>(pc.parent);
                if (parentWT == nullptr) return;

                const TransformComponent* parentT = registry.Get<TransformComponent>(pc.parent);
                if (parentT != nullptr && parentT->dirty) return;

                UpdateChild(t, wt, *parentWT);
                anyUpdated = true;
            });

        if (!anyUpdated) break;  // Keine weiteren Änderungen → fertig
    }
}

// ---------------------------------------------------------------------------
// MarkDirty — Entity und alle Kinder als dirty markieren.
//
// Iterativ (kein Rekursionsrisiko).
// Fast path: ChildrenComponent vorhanden → O(depth).
// Slow path: Kein ChildrenComponent → O(n) Scan pro Ebene.
// Vollständige Variante via HierarchySystem::MarkDirtySubtree() verfügbar.
// ---------------------------------------------------------------------------
void TransformSystem::MarkDirty(Registry& registry, EntityID id)
{
    std::vector<EntityID> stack;
    stack.reserve(16);
    stack.push_back(id);

    while (!stack.empty())
    {
        const EntityID cur = stack.back();
        stack.pop_back();

        if (!registry.IsAlive(cur)) continue;

        auto* t = registry.Get<TransformComponent>(cur);
        if (t) t->dirty = true;

        // Fast path: ChildrenComponent → O(1) Kindlookup
        if (const auto* cc = registry.Get<ChildrenComponent>(cur))
        {
            for (EntityID child : cc->children)
                stack.push_back(child);
        }
        else
        {
            // Slow path: O(n) Scan (Fallback wenn HierarchySystem nicht genutzt)
            registry.View<TransformComponent, ParentComponent>(
                [&](EntityID childID, TransformComponent&, ParentComponent& pc)
                {
                    if (pc.parent == cur)
                        stack.push_back(childID);
                });
        }
    }
}