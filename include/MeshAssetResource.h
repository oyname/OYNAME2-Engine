#pragma once
#include "SubmeshData.h"
#include "Handle.h"

#include <vector>
#include <cstdint>
#include <string>

// ---------------------------------------------------------------------------
// MeshAssetResource — CPU-Geometrie + Metadaten im ResourceStore.
//
// Kein GPU-Objekt. Die zugehörigen DX11MeshGpu-Einträge liegen in
// GDXDX11GpuRegistry und sind nur dem Backend zugänglich.
//
// gpuReady = true sobald das Backend alle Submeshes hochgeladen hat.
// ---------------------------------------------------------------------------
struct MeshAssetResource
{
    std::vector<SubmeshData> submeshes;
    std::string              debugName;
    bool                     gpuReady = false;

    uint32_t SubmeshCount() const noexcept
    {
        return static_cast<uint32_t>(submeshes.size());
    }

    bool IsEmpty() const noexcept { return submeshes.empty(); }

    bool IsGpuReadyAt(uint32_t submeshIndex) const noexcept
    {
        return gpuReady && submeshIndex < submeshes.size();
    }

    void AddSubmesh(SubmeshData data)
    {
        submeshes.push_back(std::move(data));
    }

    MeshAssetResource() = default;
    MeshAssetResource(const MeshAssetResource&)            = delete;
    MeshAssetResource& operator=(const MeshAssetResource&) = delete;
    MeshAssetResource(MeshAssetResource&&)                 = default;
    MeshAssetResource& operator=(MeshAssetResource&&)      = default;
    ~MeshAssetResource() = default;
};
