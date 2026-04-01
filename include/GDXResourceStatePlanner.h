#pragma once

#include "GDXResourceState.h"

#include <array>
#include <cstdint>

struct GDXPlannedResourceTransition
{
    GDXPassResourceKind kind = GDXPassResourceKind::Texture;
    TextureHandle texture = TextureHandle::Invalid();
    RenderTargetHandle renderTarget = RenderTargetHandle::Invalid();
    ResourceState before = ResourceState::Unknown;
    ResourceState after = ResourceState::Unknown;
    const wchar_t* debugName = L"";
};

struct GDXPlannedResourceTransitionSet
{
    static constexpr uint32_t MaxEntries = GDXPassResourceUsageSet::MaxEntries;
    std::array<GDXPlannedResourceTransition, MaxEntries> entries{};
    uint32_t count = 0u;

    void Clear() noexcept
    {
        count = 0u;
        for (auto& e : entries)
            e = {};
    }

    bool Add(const GDXPlannedResourceTransition& entry) noexcept
    {
        if (count >= MaxEntries)
            return false;
        entries[count++] = entry;
        return true;
    }
};

constexpr ResourceState GDXInferResourceStateForAccess(GDXPassResourceAccess access) noexcept
{
    switch (access)
    {
    case GDXPassResourceAccess::Read:         return ResourceState::ShaderRead;
    case GDXPassResourceAccess::Write:        return ResourceState::UnorderedAccess;
    case GDXPassResourceAccess::ReadWrite:    return ResourceState::UnorderedAccess;
    case GDXPassResourceAccess::RenderTarget: return ResourceState::RenderTarget;
    case GDXPassResourceAccess::DepthTarget:  return ResourceState::DepthWrite;
    case GDXPassResourceAccess::Present:      return ResourceState::Present;
    case GDXPassResourceAccess::None:
    default:                                  return ResourceState::Unknown;
    }
}

inline void GDXNormalizePassResourceUsageEntry(GDXPassResourceUsageEntry& entry) noexcept
{
    const ResourceState inferred = GDXInferResourceStateForAccess(entry.access);

    if (entry.stateDuring == ResourceState::Unknown)
        entry.stateDuring = inferred;

    if (entry.requiredBefore == ResourceState::Unknown)
    {
        switch (entry.kind)
        {
        case GDXPassResourceKind::Backbuffer:
            entry.requiredBefore = ResourceState::Present;
            break;
        case GDXPassResourceKind::RenderTarget:
        case GDXPassResourceKind::ShadowMap:
        case GDXPassResourceKind::Texture:
        default:
            entry.requiredBefore = ResourceState::ShaderRead;
            break;
        }
    }

    if (entry.stateAfter == ResourceState::Unknown)
    {
        switch (entry.kind)
        {
        case GDXPassResourceKind::Backbuffer:
            entry.stateAfter = ResourceState::Present;
            break;
        case GDXPassResourceKind::RenderTarget:
        case GDXPassResourceKind::ShadowMap:
        case GDXPassResourceKind::Texture:
        default:
            entry.stateAfter = ResourceState::ShaderRead;
            break;
        }
    }
}

inline void GDXNormalizePassResourceUsageSet(GDXPassResourceUsageSet& usageSet) noexcept
{
    for (uint32_t i = 0u; i < usageSet.count; ++i)
        GDXNormalizePassResourceUsageEntry(usageSet.entries[i]);
}

inline void GDXBuildPassTransitionPlan(
    const GDXPassResourceUsageSet& usageSet,
    GDXPlannedResourceTransitionSet& beginTransitions,
    GDXPlannedResourceTransitionSet& endTransitions) noexcept
{
    beginTransitions.Clear();
    endTransitions.Clear();

    for (uint32_t i = 0u; i < usageSet.count; ++i)
    {
        const GDXPassResourceUsageEntry& entry = usageSet.entries[i];
        if (entry.requiredBefore != ResourceState::Unknown &&
            entry.stateDuring != ResourceState::Unknown &&
            entry.requiredBefore != entry.stateDuring)
        {
            beginTransitions.Add({ entry.kind, entry.texture, entry.renderTarget,
                                   entry.requiredBefore, entry.stateDuring, entry.debugName });
        }

        if (entry.stateDuring != ResourceState::Unknown &&
            entry.stateAfter != ResourceState::Unknown &&
            entry.stateDuring != entry.stateAfter)
        {
            endTransitions.Add({ entry.kind, entry.texture, entry.renderTarget,
                                 entry.stateDuring, entry.stateAfter, entry.debugName });
        }
    }
}
