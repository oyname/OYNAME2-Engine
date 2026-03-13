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
#include <functional>

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

    using ShaderResolver = std::function<ShaderHandle(RenderPass, const SubmeshData&, const MaterialResource&)>;

    void Gather(
        Registry&                                             registry,
        const FrameData&                                      frame,
        ResourceStore<MeshAssetResource, MeshTag>&            meshStore,
        ResourceStore<MaterialResource,  MaterialTag>&        matStore,
        const ShaderResolver&                                 resolveShader,
        RenderQueue&                                          outQueue) const;

    void GatherShadow(
        Registry&                                             registry,
        const FrameData&                                      frame,
        ResourceStore<MeshAssetResource, MeshTag>&            meshStore,
        ResourceStore<MaterialResource,  MaterialTag>&        matStore,
        const ShaderResolver&                                 resolveShader,
        RenderQueue&                                          outShadowQueue) const;
};
