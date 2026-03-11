#include "TransformSystem.h"
#include <vector>

using namespace DirectX;

// ---------------------------------------------------------------------------
// ComputeLocalMatrix — baut XMFLOAT4X4 aus Position, Quaternion, Scale.
// ---------------------------------------------------------------------------
XMFLOAT4X4 TransformSystem::ComputeLocalMatrix(const TransformComponent& t)
{
    // Scale → Rotation (Quaternion) → Translation: SRT-Matrixkette.
    const XMVECTOR s = XMLoadFloat3(&t.localScale);
    const XMVECTOR r = XMLoadFloat4(&t.localRotation);  // Quaternion
    const XMVECTOR p = XMLoadFloat3(&t.localPosition);

    XMMATRIX m = XMMatrixScalingFromVector(s)
        * XMMatrixRotationQuaternion(r)
        * XMMatrixTranslationFromVector(p);

    XMFLOAT4X4 result;
    XMStoreFloat4x4(&result, m);
    return result;
}

// ---------------------------------------------------------------------------
// UpdateRoot — Root-Entity ohne Parent.
// ---------------------------------------------------------------------------
void TransformSystem::UpdateRoot(TransformComponent& t, WorldTransformComponent& wt)
{
    wt.matrix = ComputeLocalMatrix(t);

    XMMATRIX m = XMLoadFloat4x4(&wt.matrix);
    XMMATRIX inv = XMMatrixInverse(nullptr, m);
    XMStoreFloat4x4(&wt.inverse, inv);

    t.dirty = false;
}

// ---------------------------------------------------------------------------
// UpdateChild — Kind-Entity: local * parent.world
// ---------------------------------------------------------------------------
void TransformSystem::UpdateChild(TransformComponent& t, WorldTransformComponent& wt,
    const WorldTransformComponent& parentWT)
{
    XMFLOAT4X4 local = ComputeLocalMatrix(t);

    XMMATRIX localM = XMLoadFloat4x4(&local);
    XMMATRIX parentM = XMLoadFloat4x4(&parentWT.matrix);
    XMMATRIX worldM = XMMatrixMultiply(localM, parentM);

    XMStoreFloat4x4(&wt.matrix, worldM);

    XMMATRIX inv = XMMatrixInverse(nullptr, worldM);
    XMStoreFloat4x4(&wt.inverse, inv);

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
// ---------------------------------------------------------------------------
void TransformSystem::MarkDirty(Registry& registry, EntityID id)
{
    auto* t = registry.Get<TransformComponent>(id);
    if (!t) return;
    t->dirty = true;

    // Kind-Entities: alle Entities mit ParentComponent die auf id zeigen.
    // Hinweis: Dies ist O(n) über alle ParentComponents — für tiefe Hierarchien
    // in Phase 7 durch eine Parent→Children-Map ersetzen.
    registry.View<TransformComponent, ParentComponent>(
        [&](EntityID childID, TransformComponent& childT, ParentComponent& pc)
        {
            if (pc.parent == id)
            {
                childT.dirty = true;
                MarkDirty(registry, childID);  // Rekursiv für Enkel etc.
            }
        });
}