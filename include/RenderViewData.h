#pragma once

#include "ECS/ECSTypes.h"
#include "Core/GDXMathOps.h"
#include "FrameData.h"
#include "Components.h"
#include "RenderComponents.h"
#include "Math/Geometry/Frustum.h"

#include <vector>
#include <cstdint>

enum class RenderViewType : uint8_t
{
    Main = 0,
    Shadow = 1,
    RenderTarget = 2,
};

struct RenderViewData
{
    RenderViewType type = RenderViewType::Main;
    FrameData frame = {};
    RenderTargetHandle renderTarget = RenderTargetHandle::Invalid();
    TextureHandle forbiddenShaderReadTexture = TextureHandle::Invalid();

    bool gatherOpaque = true;
    bool gatherTransparent = true;
    bool gatherShadows = true;
    bool skipSelfReferentialDraws = false;
    bool enableFrustumCulling = true;
    bool enableDistanceCulling = true;

    uint32_t visibilityLayerMask = 0xFFFFFFFFu;
    uint32_t shadowCasterLayerMask = 0xFFFFFFFFu;
    FrustumData frustum = {};
};

struct VisibleRenderCandidate
{
    EntityID entity = NULL_ENTITY;
    Matrix4 worldMatrix = Matrix4::Identity();

    MeshHandle mesh = MeshHandle::Invalid();
    MaterialHandle material = MaterialHandle::Invalid();
    uint32_t submeshIndex = 0u;

    bool enabled = false;
    bool visible = true;
    bool active = true;
    uint32_t layerMask = 0x00000001u;
    bool castShadows = true;
    bool receiveShadows = true;
    uint32_t renderableStateVersion = 0u;
    uint32_t visibilityStateVersion = 0u;

    bool hasBounds = false;
    Float3 worldBoundsCenter = { 0.0f, 0.0f, 0.0f };
    float worldBoundsRadius = 0.0f;
};

struct ViewCullingStats
{
    uint32_t totalCandidates = 0u;
    uint32_t visibleCandidates = 0u;
    uint32_t culledByInactive = 0u;
    uint32_t culledByLayer = 0u;
    uint32_t culledByFrustum = 0u;
    uint32_t culledByDistance = 0u;
    uint32_t missingBounds = 0u;
};

struct VisibleSet
{
    RenderViewType type = RenderViewType::Main;
    uint32_t cullMask = 0xFFFFFFFFu;
    uint32_t visibilityLayerMask = 0xFFFFFFFFu;
    uint32_t shadowCasterLayerMask = 0xFFFFFFFFu;
    std::vector<VisibleRenderCandidate> candidates;
    ViewCullingStats stats{};

    void Clear()
    {
        candidates.clear();
        stats = {};
    }
};
