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

struct RenderGatherOptions
{
    bool gatherOpaque = true;
    bool gatherTransparent = true;
    bool gatherShadows = true;
    bool skipSelfReferentialDraws = false;
    TextureHandle forbiddenShaderReadTexture = TextureHandle::Invalid();
    uint32_t visibilityLayerMask = 0xFFFFFFFFu;
    uint32_t shadowCasterLayerMask = 0xFFFFFFFFu;
};

// ---------------------------------------------------------------------------
// RenderGatherSystem — baut RenderQueue aus ECS-Komponenten.
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
        RenderQueue&                                          outOpaqueQueue,
        RenderQueue&                                          outTransparentQueue,
        const RenderGatherOptions*                            options = nullptr) const;

    void GatherShadow(
        Registry&                                             registry,
        const FrameData&                                      frame,
        ResourceStore<MeshAssetResource, MeshTag>&            meshStore,
        ResourceStore<MaterialResource,  MaterialTag>&        matStore,
        const ShaderResolver&                                 resolveShader,
        RenderQueue&                                          outShadowQueue,
        const RenderGatherOptions*                            options = nullptr) const;
};
