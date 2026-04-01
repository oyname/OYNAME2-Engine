#pragma once
#include "ECS/Registry.h"
#include "Components.h"
#include "RenderComponents.h"
#include "FrameData.h"
#include "RenderQueue.h"
#include "ResourceStore.h"
#include "MeshAssetResource.h"
#include "MaterialResource.h"
#include "MaterialParams.h"
#include "GDXShaderResource.h"
#include "CameraSystem.h"
#include "RenderViewData.h"
#include "Core/JobSystem.h"
#include <functional>
#include <unordered_map>
#include <vector>

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
        MaterialParams materialParams{};
        MaterialRenderPolicy materialRenderPolicy{};
        MaterialTextureLayerArray materialTextureLayers{};
        bool transparent = false;
        uint32_t materialSortID = 0u;
        uint32_t renderStateVersion = 0u;
        bool visible = true;
        bool active = true;
        uint32_t layerMask = 0x00000001u;
        bool castShadows = true;
        bool receiveShadows = true;
        uint32_t materialStateVersion = 0u;
        uint64_t passBindingsKey = 0ull;
        uint64_t materialBindingsKey = 0ull;
        bool valid = false;
    };

    struct GatherChunkResult
    {
        std::vector<RenderCommand> opaque;
        std::vector<RenderCommand> transparent;
        std::vector<RenderCommand> shadow;
        std::unordered_map<EntityID, CachedCommandState> mainCache;
        std::unordered_map<EntityID, CachedCommandState> shadowCache;
    };


    void GatherVisibleSetChunks(
        const VisibleSet& visibleSet,
        const FrameData& frame,
        ResourceStore<MeshAssetResource, MeshTag>& meshStore,
        ResourceStore<MaterialResource, MaterialTag>& matStore,
        ResourceStore<GDXShaderResource, ShaderTag>& shaderStore,
        const ShaderResolver& resolveShader,
        std::vector<GatherChunkResult>& outChunkResults,
        const RenderGatherOptions* options,
        JobSystem* jobSystem = nullptr) const;

    void GatherShadowVisibleSetChunks(
        const VisibleSet& visibleSet,
        const FrameData& frame,
        ResourceStore<MeshAssetResource, MeshTag>& meshStore,
        ResourceStore<MaterialResource, MaterialTag>& matStore,
        ResourceStore<GDXShaderResource, ShaderTag>& shaderStore,
        const ShaderResolver& resolveShader,
        std::vector<GatherChunkResult>& outChunkResults,
        const RenderGatherOptions* options,
        JobSystem* jobSystem = nullptr) const;

    void MergeVisibleSetChunks(
        const std::vector<GatherChunkResult>& chunkResults,
        RenderQueue& outOpaqueQueue,
        RenderQueue& outTransparentQueue) const;

    void MergeShadowVisibleSetChunks(
        const std::vector<GatherChunkResult>& chunkResults,
        RenderQueue& outShadowQueue) const;

    static void SortRenderQueue(RenderQueue& queue);

    void GatherVisibleSet(
        const VisibleSet& visibleSet,
        const FrameData& frame,
        ResourceStore<MeshAssetResource, MeshTag>& meshStore,
        ResourceStore<MaterialResource, MaterialTag>& matStore,
        ResourceStore<GDXShaderResource, ShaderTag>& shaderStore,
        const ShaderResolver& resolveShader,
        RenderQueue& outOpaqueQueue,
        RenderQueue& outTransparentQueue,
        const RenderGatherOptions* options,
        JobSystem* jobSystem = nullptr) const;

    void GatherShadowVisibleSet(
        const VisibleSet& visibleSet,
        const FrameData& frame,
        ResourceStore<MeshAssetResource, MeshTag>& meshStore,
        ResourceStore<MaterialResource, MaterialTag>& matStore,
        ResourceStore<GDXShaderResource, ShaderTag>& shaderStore,
        const ShaderResolver& resolveShader,
        RenderQueue& outShadowQueue,
        const RenderGatherOptions* options,
        JobSystem* jobSystem = nullptr) const;

};
