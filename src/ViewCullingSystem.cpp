#include "ViewCullingSystem.h"
#include "Math/Geometry/FrustumUtils.h"
#include "Core/GDXMathOps.h"

using FrustumUtils::ComputeMaxWorldScale;
using FrustumUtils::BuildFrustumFromViewProj;
using FrustumUtils::SphereInsideFrustum;
using FrustumUtils::AABBInsideFrustum;
using FrustumUtils::TransformAABB;

#include "Components.h"
#include "RenderComponents.h"
#include "ECS/ComponentPool.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
    enum class ViewFilterMode : uint8_t
    {
        Main = 0,
        Shadow = 1,
    };

    bool PassesDistanceCull(const RenderBoundsComponent* bounds,
                            const Float3& center,
                            float radius,
                            const FrameData& frame,
                            bool enableDistanceCulling)
    {
        if (!enableDistanceCulling || !bounds || !bounds->valid || !bounds->enableDistanceCull || bounds->maxViewDistance <= 0.0f)
            return true;

        const float dx = center.x - frame.cameraPos.x;
        const float dy = center.y - frame.cameraPos.y;
        const float dz = center.z - frame.cameraPos.z;
        const float maxDist = bounds->maxViewDistance + radius;
        return (dx * dx + dy * dy + dz * dz) <= (maxDist * maxDist);
    }

    bool IsRelevantForView(const VisibleRenderCandidate& candidate, const VisibleSet& set, ViewFilterMode mode)
    {
        const uint32_t mask = (mode == ViewFilterMode::Shadow) ? set.shadowCasterLayerMask : set.visibilityLayerMask;
        if ((candidate.layerMask & set.cullMask & mask) == 0u)
            return false;

        const DrawPassType requiredPass = (mode == ViewFilterMode::Shadow) ? DrawPassType::ShadowDepth
                                                                           : DrawPassType::Opaque;
        if (!HasDrawPass(candidate.drawPassMask, requiredPass) && !(mode == ViewFilterMode::Main && HasDrawPass(candidate.drawPassMask, DrawPassType::Transparent)))
            return false;

        if (mode == ViewFilterMode::Shadow)
            return candidate.castShadows;

        return candidate.visible;
    }
}

void ViewCullingSystem::BuildVisibleSet(Registry& registry,
                                        const RenderViewData& view,
                                        VisibleSet& outVisibleSet,
                                        JobSystem* jobSystem) const
{
    outVisibleSet.Clear();
    outVisibleSet.type = view.type;
    outVisibleSet.cullMask = view.frame.cullMask;
    outVisibleSet.visibilityLayerMask = view.visibilityLayerMask;
    outVisibleSet.shadowCasterLayerMask = view.shadowCasterLayerMask;

    auto* worldPool = registry.TryGetPool<WorldTransformComponent>();
    auto* renderablePool = registry.TryGetPool<RenderableComponent>();
    auto* visibilityPool = registry.TryGetPool<VisibilityComponent>();
    auto* boundsPool = registry.TryGetPool<RenderBoundsComponent>();
    if (!worldPool || !renderablePool || !visibilityPool)
        return;

    RenderViewData localView = view;
    if (localView.enableFrustumCulling && !localView.frustum.valid)
    {
        const Matrix4& cullMatrix = (localView.type == RenderViewType::Shadow)
            ? localView.frame.shadowViewProjMatrix
            : localView.frame.viewProjMatrix;
        localView.frustum = BuildFrustumFromViewProj(cullMatrix);
    }

    const size_t total = worldPool->Count();
    if (total == 0u)
        return;

    const size_t chunkSize = ComponentPool<WorldTransformComponent>::kChunkSize;
    const size_t chunkCount = (total + chunkSize - 1u) / chunkSize;

    struct LocalChunkResult
    {
        std::vector<VisibleRenderCandidate> visible;
        ViewCullingStats stats{};
    };

    std::vector<LocalChunkResult> local(chunkCount);

    auto processRange = [&](size_t beginChunk, size_t endChunk)
    {
        const auto& entities = worldPool->Entities();
        const auto& worlds = worldPool->Components();

        for (size_t chunkIndex = beginChunk; chunkIndex < endChunk; ++chunkIndex)
        {
            const size_t begin = chunkIndex * chunkSize;
            const size_t end = (std::min)(begin + chunkSize, total);
            LocalChunkResult& chunk = local[chunkIndex];
            chunk.visible.reserve(end - begin);

            for (size_t i = begin; i < end; ++i)
            {
                chunk.stats.totalCandidates++;

                const EntityID entity = entities[i];
                const auto* renderable = renderablePool->Get(entity);
                const auto* visibility = visibilityPool->Get(entity);
                if (!renderable || !visibility)
                    continue;

                VisibleRenderCandidate candidate{};
                candidate.entity = entity;
                candidate.worldMatrix = worlds[i].matrix;
                candidate.mesh = renderable->mesh;
                candidate.material = renderable->material;
                candidate.submeshIndex = renderable->submeshIndex;
                candidate.enabled = renderable->enabled;
                candidate.visible = visibility->visible;
                candidate.active = visibility->active;
                candidate.layerMask = visibility->layerMask;
                candidate.castShadows = visibility->castShadows;
                candidate.receiveShadows = visibility->receiveShadows;
                candidate.renderableStateVersion = renderable->stateVersion;
                candidate.visibilityStateVersion = visibility->stateVersion;
                candidate.drawPassMask = renderable->drawPassMask & view.viewPassMask;
                candidate.renderPriority = renderable->renderPriority;

                if (!candidate.active || !candidate.enabled)
                {
                    chunk.stats.culledByInactive++;
                    continue;
                }

                const ViewFilterMode mode = (localView.type == RenderViewType::Shadow)
                    ? ViewFilterMode::Shadow
                    : ViewFilterMode::Main;
                if (!IsRelevantForView(candidate, outVisibleSet, mode))
                {
                    chunk.stats.culledByLayer++;
                    continue;
                }

                if (const auto* bounds = boundsPool ? boundsPool->Get(entity) : nullptr)
                {
                    if (bounds->valid)
                    {
                        candidate.hasBounds = true;
                        candidate.worldBoundsCenter = GDX::TransformPoint(bounds->localCenter, worlds[i].matrix);
                        candidate.worldBoundsRadius = bounds->localSphereRadius * bounds->boundsScale * ComputeMaxWorldScale(worlds[i].matrix);

                        if (localView.enableFrustumCulling)
                        {
                            // Stufe 1 — Sphere (schnell, konservativ)
                            if (!SphereInsideFrustum(localView.frustum, candidate.worldBoundsCenter, candidate.worldBoundsRadius))
                            {
                                chunk.stats.culledByFrustum++;
                                continue;
                            }

                            // Stufe 2 — AABB (präziser, nur wenn Sphere nicht eindeutig raus)
                            if (bounds->shape == RenderBoundsComponent::Shape::AABB)
                            {
                                Float3 localAabbMin = bounds->localAabbMin;
                                Float3 localAabbMax = bounds->localAabbMax;
                                if (bounds->boundsScale != 1.0f)
                                {
                                    const Float3 scaledExtents = {
                                        (bounds->localAabbMax.x - bounds->localCenter.x) * bounds->boundsScale,
                                        (bounds->localAabbMax.y - bounds->localCenter.y) * bounds->boundsScale,
                                        (bounds->localAabbMax.z - bounds->localCenter.z) * bounds->boundsScale,
                                    };
                                    localAabbMin = {
                                        bounds->localCenter.x - scaledExtents.x,
                                        bounds->localCenter.y - scaledExtents.y,
                                        bounds->localCenter.z - scaledExtents.z,
                                    };
                                    localAabbMax = {
                                        bounds->localCenter.x + scaledExtents.x,
                                        bounds->localCenter.y + scaledExtents.y,
                                        bounds->localCenter.z + scaledExtents.z,
                                    };
                                }

                                Float3 worldMin, worldMax;
                                TransformAABB(localAabbMin, localAabbMax,
                                              worlds[i].matrix, worldMin, worldMax);
                                if (!AABBInsideFrustum(localView.frustum, worldMin, worldMax))
                                {
                                    chunk.stats.culledByFrustum++;
                                    continue;
                                }
                            }
                        }

                        if (!PassesDistanceCull(bounds, candidate.worldBoundsCenter, candidate.worldBoundsRadius, localView.frame, localView.enableDistanceCulling))
                        {
                            chunk.stats.culledByDistance++;
                            continue;
                        }
                    }
                    else
                    {
                        chunk.stats.missingBounds++;
                    }
                }
                else
                {
                    chunk.stats.missingBounds++;
                }

                chunk.stats.visibleCandidates++;
                chunk.visible.push_back(candidate);
            }
        }
    };

    if (jobSystem)
        jobSystem->ParallelFor(chunkCount, processRange, 1u);
    else
        processRange(0u, chunkCount);

    size_t totalVisible = 0u;
    for (const LocalChunkResult& chunk : local)
    {
        totalVisible += chunk.visible.size();
        outVisibleSet.stats.totalCandidates += chunk.stats.totalCandidates;
        outVisibleSet.stats.visibleCandidates += chunk.stats.visibleCandidates;
        outVisibleSet.stats.culledByInactive += chunk.stats.culledByInactive;
        outVisibleSet.stats.culledByLayer += chunk.stats.culledByLayer;
        outVisibleSet.stats.culledByFrustum += chunk.stats.culledByFrustum;
        outVisibleSet.stats.culledByDistance += chunk.stats.culledByDistance;
        outVisibleSet.stats.missingBounds += chunk.stats.missingBounds;
    }

    outVisibleSet.candidates.reserve(totalVisible);
    for (auto& chunk : local)
        outVisibleSet.candidates.insert(outVisibleSet.candidates.end(), chunk.visible.begin(), chunk.visible.end());
}
