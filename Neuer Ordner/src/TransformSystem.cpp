#include "TransformSystem.h"
#include "Components.h"
#include "Debug.h"
#include "GDXMath.h"
#include "JobSystem.h"
#include <vector>
#include <atomic>
#include <algorithm>

namespace
{
    struct DirtyNodeContext
    {
        EntityID id = NULL_ENTITY;
        EntityID parent = NULL_ENTITY;
        bool hasParent = false;
    };

    void CollectDirtyChildren(Registry& registry, EntityID parent, std::vector<EntityID>& outChildren)
    {
        if (const auto* cc = registry.Get<ChildrenComponent>(parent))
        {
            outChildren.reserve(outChildren.size() + cc->children.size());
            for (EntityID child : cc->children)
            {
                if (!registry.IsAlive(child))
                    continue;

                const auto* childT = registry.Get<TransformComponent>(child);
                const auto* childP = registry.Get<ParentComponent>(child);
                if (!childT || !childP || childP->parent != parent)
                    continue;

                outChildren.push_back(child);
            }
            return;
        }

        registry.View<TransformComponent, ParentComponent>(
            [&](EntityID childID, TransformComponent&, ParentComponent& pc)
            {
                if (pc.parent == parent)
                    outChildren.push_back(childID);
            });
    }

    bool TryBuildDirtyNodeContext(Registry& registry, EntityID id, DirtyNodeContext& out)
    {
        if (!registry.IsAlive(id))
            return false;

        const auto* t = registry.Get<TransformComponent>(id);
        const auto* wt = registry.Get<WorldTransformComponent>(id);
        if (!t || !wt)
            return false;

        out.id = id;
        out.parent = NULL_ENTITY;
        out.hasParent = false;

        const auto* pc = registry.Get<ParentComponent>(id);
        if (!pc || pc->parent == NULL_ENTITY)
            return true;

        if (!registry.IsAlive(pc->parent) || !registry.Has<WorldTransformComponent>(pc->parent))
            return true;

        out.parent = pc->parent;
        out.hasParent = true;
        return true;
    }

    void CollectDirtyEntryFrontier(Registry& registry, std::vector<DirtyNodeContext>& frontier)
    {
        frontier.clear();

        registry.View<TransformComponent, WorldTransformComponent>(
            [&](EntityID id, TransformComponent& t, WorldTransformComponent&)
            {
                if (!t.dirty)
                    return;

                DirtyNodeContext ctx;
                if (!TryBuildDirtyNodeContext(registry, id, ctx))
                    return;

                if (!ctx.hasParent)
                {
                    frontier.push_back(ctx);
                    return;
                }

                const auto* parentT = registry.Get<TransformComponent>(ctx.parent);
                if (parentT == nullptr || !parentT->dirty)
                    frontier.push_back(ctx);
            });
    }

    void CollectDirtyChildrenFrontier(Registry& registry,
        const std::vector<DirtyNodeContext>& currentFrontier,
        std::vector<DirtyNodeContext>& nextFrontier)
    {
        nextFrontier.clear();

        std::vector<EntityID> childIds;
        for (const DirtyNodeContext& current : currentFrontier)
        {
            childIds.clear();
            CollectDirtyChildren(registry, current.id, childIds);

            for (EntityID childID : childIds)
            {
                DirtyNodeContext childCtx;
                if (!TryBuildDirtyNodeContext(registry, childID, childCtx))
                    continue;

                if (!childCtx.hasParent)
                    continue;

                const auto* parentT = registry.Get<TransformComponent>(childCtx.parent);
                if (parentT != nullptr && parentT->dirty)
                    continue;

                nextFrontier.push_back(childCtx);
            }
        }
    }
}

GIDX::Float4x4 TransformSystem::ComputeLocalMatrix(const TransformComponent& t)
{
    // S * R * T  (row-vector Konvention: v * S * R * T)
    const GIDX::Float4x4 S = GIDX::Scaling(
        t.localScale.x, t.localScale.y, t.localScale.z);
    const GIDX::Float4x4 R = GIDX::RotationQuaternion(t.localRotation);
    const GIDX::Float4x4 T = GIDX::Translation(
        t.localPosition.x, t.localPosition.y, t.localPosition.z);
    return GIDX::Multiply(GIDX::Multiply(S, R), T);
}

void TransformSystem::UpdateRoot(TransformComponent& t, WorldTransformComponent& wt)
{
    wt.matrix = ComputeLocalMatrix(t);
    // Inverse wird für Root-Entities nicht gespeichert
    // (preserviert Originalverhalten: berechnet aber nicht assigned)
    (void)GIDX::Inverse(wt.matrix);

    t.dirty = false;
    ++t.worldVersion;
}

void TransformSystem::UpdateChild(TransformComponent& t,
    WorldTransformComponent& wt,
    const WorldTransformComponent& parentWT)
{
    const GIDX::Float4x4 local = ComputeLocalMatrix(t);
    wt.matrix  = GIDX::Multiply(local, parentWT.matrix);
    wt.inverse = GIDX::Inverse(wt.matrix);

    t.dirty = false;
    ++t.worldVersion;
}

void TransformSystem::EnsureWorldTransforms(Registry& registry)
{
    std::vector<EntityID> missing;

    registry.View<TransformComponent>(
        [&](EntityID id, TransformComponent&)
        {
            if (!registry.Has<WorldTransformComponent>(id))
                missing.push_back(id);
        });

    for (EntityID id : missing)
        registry.Add<WorldTransformComponent>(id);
}

void TransformSystem::Update(Registry& registry, JobSystem* jobSystem)
{
    EnsureWorldTransforms(registry);

    std::vector<DirtyNodeContext> frontier;
    std::vector<DirtyNodeContext> nextFrontier;
    CollectDirtyEntryFrontier(registry, frontier);

    auto updateFrontier = [&](const std::vector<DirtyNodeContext>& nodes)
        {
            auto worker = [&](size_t begin, size_t end)
                {
                    for (size_t i = begin; i < end; ++i)
                    {
                        const DirtyNodeContext& node = nodes[i];

                        auto* t = registry.Get<TransformComponent>(node.id);
                        auto* wt = registry.Get<WorldTransformComponent>(node.id);
                        if (!t || !wt)
                            continue;

                        const bool forceByParent = node.hasParent;
                        if (!t->dirty && !forceByParent)
                            continue;

                        if (!node.hasParent)
                        {
                            UpdateRoot(*t, *wt);
                            continue;
                        }

                        const auto* parentWT = registry.Get<WorldTransformComponent>(node.parent);
                        if (!parentWT)
                            continue;

                        const auto* parentT = registry.Get<TransformComponent>(node.parent);
                        if (parentT != nullptr && parentT->dirty)
                            continue;

                        UpdateChild(*t, *wt, *parentWT);
                    }
                };

            if (jobSystem)
                jobSystem->ParallelFor(nodes.size(), worker, 32u);
            else
                worker(0u, nodes.size());
        };

    while (!frontier.empty())
    {
        updateFrontier(frontier);
        CollectDirtyChildrenFrontier(registry, frontier, nextFrontier);
        frontier.swap(nextFrontier);
    }

#ifdef _DEBUG
    size_t unresolvedDirtyCount = 0u;
    registry.View<TransformComponent>(
        [&](EntityID, TransformComponent& t)
        {
            if (t.dirty)
                ++unresolvedDirtyCount;
        });

    if (unresolvedDirtyCount > 0u)
    {
        DBWARN(GDX_SRC_LOC,
            "TransformSystem left ",
            unresolvedDirtyCount,
            " dirty transform(s) unresolved. Check parent links or hierarchy cycles.");
    }
#endif
}

void TransformSystem::MarkDirty(Registry& registry, EntityID id)
{
    std::vector<EntityID> stack;
    stack.reserve(16);
    stack.push_back(id);

    while (!stack.empty())
    {
        const EntityID cur = stack.back();
        stack.pop_back();

        if (!registry.IsAlive(cur))
            continue;

        auto* t = registry.Get<TransformComponent>(cur);
        if (t)
        {
            t->dirty = true;
            ++t->localVersion;
        }

        if (const auto* cc = registry.Get<ChildrenComponent>(cur))
        {
            for (EntityID child : cc->children)
                stack.push_back(child);
        }
        else
        {
            registry.View<TransformComponent, ParentComponent>(
                [&](EntityID childID, TransformComponent&, ParentComponent& pc)
                {
                    if (pc.parent == cur)
                        stack.push_back(childID);
                });
        }
    }
}
