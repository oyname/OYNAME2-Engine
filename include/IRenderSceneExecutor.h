#pragma once

#include "BackendRenderPassDesc.h"
#include "ICommandList.h"
#include "FrameData.h"
#include "RenderBackendSceneContext.h"
#include "ECS/ECSTypes.h"

#include <unordered_set>
#include <vector>

struct VisibleRenderCandidate;
class MeshAssetResource;

enum class GDXTextureFormat : uint8_t;

class IRenderSceneExecutor
{
public:
    virtual ~IRenderSceneExecutor() = default;

    virtual bool UploadMesh(MeshHandle handle, MeshAssetResource& mesh) = 0;
    virtual bool UploadMaterial(MaterialHandle handle, MaterialResource& mat) = 0;

    virtual void ExtractLightData(Registry& registry, FrameData& frame) = 0;
    virtual void UploadLightConstants(const FrameData& frame) = 0;
    virtual void UpdateFrameConstants(const FrameData& frame) = 0;

    virtual void ExecuteRenderPass(
        const BackendRenderPassDesc& passDesc,
        const std::vector<BackendPlannedTransition>& beginTransitions,
        const std::vector<BackendPlannedTransition>& endTransitions,
        const RenderBackendSceneContext& scene,
        const ICommandList& opaqueList,
        const ICommandList& alphaList) = 0;

    virtual void ExecuteShadowPass(
        const BackendRenderPassDesc& passDesc,
        const std::vector<BackendPlannedTransition>& beginTransitions,
        const std::vector<BackendPlannedTransition>& endTransitions,
        const RenderBackendSceneContext& scene,
        const ICommandList& commandList) = 0;

    virtual bool InitParticleRenderer(TextureHandle atlasTexture)
    {
        (void)atlasTexture;
        return false;
    }

    virtual bool SupportsOcclusionCulling() const { return false; }

    virtual void SubmitOcclusionQueries(
        const std::vector<VisibleRenderCandidate>& candidates,
        ResourceStore<MeshAssetResource, MeshTag>& meshStore,
        const FrameData& frame,
        RenderTargetHandle depthSourceTarget = RenderTargetHandle::Invalid())
    {
        (void)candidates; (void)meshStore; (void)frame; (void)depthSourceTarget;
    }

    virtual void CollectOcclusionResults(std::unordered_set<EntityID>& outVisible,
                                         std::unordered_set<EntityID>* outTested = nullptr)
    {
        (void)outVisible; (void)outTested;
    }
};
