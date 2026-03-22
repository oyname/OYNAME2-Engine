#pragma once

#include "Registry.h"
#include "RenderViewData.h"
#include "JobSystem.h"

class ViewCullingSystem
{
public:
    void BuildVisibleSet(
        Registry& registry,
        const RenderViewData& view,
        VisibleSet& outVisibleSet,
        JobSystem* jobSystem = nullptr) const;
};
