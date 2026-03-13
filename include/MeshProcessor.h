#pragma once

#include "MeshAssetResource.h"
#include "MeshUtilities.h"

struct MeshBuildSettings
{
    bool validateInput = true;
    bool removeDegenerateTriangles = true;
    bool computeNormalsIfMissing = true;
    bool recomputeNormals = false;
    bool computeTangentsIfPossible = false;
    bool recomputeTangents = false;
};

namespace MeshProcessor
{
    inline bool Process(SubmeshData& s, const MeshBuildSettings& settings = {})
    {
        if (settings.validateInput && !MeshUtilities::Validate(s))
            return false;

        if (settings.removeDegenerateTriangles)
            MeshUtilities::RemoveDegenerateTriangles(s);

        if (settings.recomputeNormals || (settings.computeNormalsIfMissing && !s.HasNormals()))
            MeshUtilities::ComputeNormals(s);

        const bool shouldTangents = settings.recomputeTangents
            || (settings.computeTangentsIfPossible && !s.HasTangents());

        if (shouldTangents && s.HasUV0())
            MeshUtilities::ComputeTangents(s);

        return !settings.validateInput || MeshUtilities::Validate(s);
    }

    inline bool Process(MeshAssetResource& asset, const MeshBuildSettings& settings = {})
    {
        for (auto& s : asset.submeshes)
        {
            if (!Process(s, settings))
                return false;
        }
        return true;
    }
}
