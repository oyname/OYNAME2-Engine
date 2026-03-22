#pragma once

// ---------------------------------------------------------------------------
// MeshBoundsUtil — berechnet RenderBoundsComponent aus Mesh-Vertices.
//
// Bewusst als freie Funktion statt MeshAssetResource-Methode um den
// Circular Include MeshAssetResource.h ↔ Components.h zu vermeiden.
//
// Aufruf: auto bounds = ComputeMeshBounds(asset);
// ---------------------------------------------------------------------------

#include "MeshAssetResource.h"
#include "Components.h"

inline RenderBoundsComponent ComputeMeshBounds(const MeshAssetResource& asset) noexcept
{
    return RenderBoundsComponent::MakeFromSubmeshes(asset.submeshes);
}
