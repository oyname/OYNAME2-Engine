#pragma once

#include "GDXResourceState.h"

#include <vector>

// Legacy compatibility header.
// The old declarative pass-resource usage planner was removed.
// Framegraph-planned transitions are now provided directly by the graph and
// forwarded to the backend execute path. Keep only the minimal transition data
// types here so stale includes do not break the build.

struct GDXPlannedResourceTransition
{
    TextureHandle texture = TextureHandle::Invalid();
    RenderTargetHandle renderTarget = RenderTargetHandle::Invalid();
    ResourceState before = ResourceState::Unknown;
    ResourceState after = ResourceState::Unknown;
    const char* debugName = "";
};

struct GDXPlannedResourceTransitionSet
{
    std::vector<GDXPlannedResourceTransition> entries{};

    void Clear() noexcept
    {
        entries.clear();
    }

    bool Add(const GDXPlannedResourceTransition& entry)
    {
        entries.push_back(entry);
        return true;
    }

    [[nodiscard]] size_t Count() const noexcept
    {
        return entries.size();
    }
};
