#include "ViewCullingSystem.h"

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

    float BasisLength(float x, float y, float z)
    {
        return std::sqrt(x * x + y * y + z * z);
    }

    float ComputeMaxWorldScale(const GIDX::Float4x4& m)
    {
        const float sx = BasisLength(m._11, m._12, m._13);
        const float sy = BasisLength(m._21, m._22, m._23);
        const float sz = BasisLength(m._31, m._32, m._33);
        return (std::max)({ sx, sy, sz, 1e-6f });
    }

    FrustumPlane NormalizePlane(const FrustumPlane& p)
    {
        const float len = std::sqrt(p.normal.x * p.normal.x + p.normal.y * p.normal.y + p.normal.z * p.normal.z);
        if (len <= 1e-6f)
            return p;

        const float invLen = 1.0f / len;
        FrustumPlane out = p;
        out.normal.x *= invLen;
        out.normal.y *= invLen;
        out.normal.z *= invLen;
        out.d *= invLen;
        return out;
    }

    FrustumData BuildFrustumFromViewProj(const GIDX::Float4x4& m)
    {
        FrustumData f{};
        f.planes[0] = NormalizePlane({ { m._14 + m._11, m._24 + m._21, m._34 + m._31 }, m._44 + m._41 });
        f.planes[1] = NormalizePlane({ { m._14 - m._11, m._24 - m._21, m._34 - m._31 }, m._44 - m._41 });
        f.planes[2] = NormalizePlane({ { m._14 + m._12, m._24 + m._22, m._34 + m._32 }, m._44 + m._42 });
        f.planes[3] = NormalizePlane({ { m._14 - m._12, m._24 - m._22, m._34 - m._32 }, m._44 - m._42 });
        f.planes[4] = NormalizePlane({ { m._13, m._23, m._33 }, m._43 });
        f.planes[5] = NormalizePlane({ { m._14 - m._13, m._24 - m._23, m._34 - m._33 }, m._44 - m._43 });
        f.valid = true;
        return f;
    }

    bool SphereInsideFrustum(const FrustumData& frustum, const GIDX::Float3& center, float radius)
    {
        if (!frustum.valid)
            return true;

        for (const FrustumPlane& plane : frustum.planes)
        {
            const float dist = plane.normal.x * center.x + plane.normal.y * center.y + plane.normal.z * center.z + plane.d;
            if (dist < -radius)
                return false;
        }
        return true;
    }

    bool PassesDistanceCull(const RenderBoundsComponent* bounds,
                            const GIDX::Float3& center,
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
        const GIDX::Float4x4& cullMatrix = (localView.type == RenderViewType::Shadow)
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
                        candidate.worldBoundsCenter = GIDX::TransformPoint(bounds->localCenter, worlds[i].matrix);
                        candidate.worldBoundsRadius = bounds->localSphereRadius * ComputeMaxWorldScale(worlds[i].matrix);

                        if (localView.enableFrustumCulling && !SphereInsideFrustum(localView.frustum, candidate.worldBoundsCenter, candidate.worldBoundsRadius))
                        {
                            chunk.stats.culledByFrustum++;
                            continue;
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
