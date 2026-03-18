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
#include <unordered_map>

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

class RenderGatherSystem
{
public:
    RenderGatherSystem() = default;

    using ShaderResolver = std::function<ShaderHandle(RenderPass, const SubmeshData&, const MaterialResource&)>;

    struct CachedCommandState
    {
        MeshHandle mesh = MeshHandle::Invalid();
        MaterialHandle material = MaterialHandle::Invalid();
        ShaderHandle shader = ShaderHandle::Invalid();
        uint32_t submeshIndex = 0u;
        RenderPass pass = RenderPass::Opaque;
        ResourceBindingSet bindings{};
        GDXPipelineStateDesc pipelineState{};
        GDXPipelineStateKey pipelineStateKey{};
        MaterialData materialData{};
        bool transparent = false;
        uint32_t materialSortID = 0u;
        uint32_t renderStateVersion = 0u;
        bool visible = true;
        bool active = true;
        uint32_t layerMask = 0x00000001u;
        bool castShadows = true;
        bool receiveShadows = true;
        bool materialCpuDirtySnapshot = true;
        uint64_t passBindingsKey = 0ull;
        uint64_t materialBindingsKey = 0ull;
        bool valid = false;
    };

    mutable std::unordered_map<EntityID, CachedCommandState> m_mainCache;
    mutable std::unordered_map<EntityID, CachedCommandState> m_shadowCache;

    void Gather(
        Registry& registry,
        const FrameData& frame,
        ResourceStore<MeshAssetResource, MeshTag>& meshStore,
        ResourceStore<MaterialResource, MaterialTag>& matStore,
        ResourceStore<GDXShaderResource, ShaderTag>& shaderStore,
        const ShaderResolver& resolveShader,
        RenderQueue& outOpaqueQueue,
        RenderQueue& outTransparentQueue,
        const RenderGatherOptions* options) const;

    void GatherShadow(
        Registry& registry,
        const FrameData& frame,
        ResourceStore<MeshAssetResource, MeshTag>& meshStore,
        ResourceStore<MaterialResource, MaterialTag>& matStore,
        ResourceStore<GDXShaderResource, ShaderTag>& shaderStore,
        const ShaderResolver& resolveShader,
        RenderQueue& outShadowQueue,
        const RenderGatherOptions* options) const;
};
