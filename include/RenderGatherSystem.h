#pragma once
#include "Registry.h"
#include "Components.h"
#include "FrameData.h"
#include "RenderQueue.h"
#include "ResourceStore.h"
#include "MeshAssetResource.h"
#include "MaterialResource.h"
#include "GDXShaderResource.h"
#include "CameraSystem.h"

// ---------------------------------------------------------------------------
// RenderGatherSystem — baut RenderQueue aus ECS-Komponenten.
//
// Liest ShaderHandle aus MaterialResource und schreibt ihn in den
// RenderCommand → Executor kann pro Draw-Call den richtigen Shader binden.
// ---------------------------------------------------------------------------
class RenderGatherSystem
{
public:
    RenderGatherSystem() = default;

    void Gather(
        Registry&                                             registry,
        const FrameData&                                      frame,
        ResourceStore<MeshAssetResource, MeshTag>&           meshStore,
        ResourceStore<MaterialResource,  MaterialTag>&        matStore,
        ShaderHandle                                          defaultShader,
        RenderQueue&                                          outQueue) const;

    void GatherShadow(
        Registry&                                             registry,
        const FrameData&                                      frame,
        ResourceStore<MeshAssetResource, MeshTag>&           meshStore,
        MaterialHandle                                        shadowMaterial,
        ShaderHandle                                          shadowShader,
        RenderQueue&                                          outShadowQueue) const;
};
