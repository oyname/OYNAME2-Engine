#pragma once

#include "ECS/Registry.h"
#include "RenderViewData.h"
#include "Core/JobSystem.h"

class ViewCullingSystem
{
public:
    void BuildVisibleSet(
        Registry& registry,
        const RenderViewData& view,
        VisibleSet& outVisibleSet,
        JobSystem* jobSystem = nullptr) const;
};
