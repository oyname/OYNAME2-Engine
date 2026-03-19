#include "RenderGatherSystem.h"
#include "Debug.h"

#include <algorithm>
#include <utility>

namespace
{
    struct ResolvedRenderable
    {
        MeshHandle mesh = MeshHandle::Invalid();
        MaterialHandle material = MaterialHandle::Invalid();
        uint32_t submeshIndex = 0u;
        bool enabled = false;
        bool visible = true;
        bool active = true;
        uint32_t layerMask = 0x00000001u;
        bool castShadows = true;
        bool receiveShadows = true;
        uint32_t stateVersion = 0u;
    };

    bool UsesTextureAsShaderResource(const ResourceBindingSet& set, TextureHandle texture)
    {
        if (!texture.IsValid()) return false;
        for (uint32_t i = 0; i < set.textureCount; ++i)
        {
            const auto& binding = set.textures[i];
            if (binding.enabled && binding.texture == texture)
                return true;
        }
        return false;
    }

    ResourceBindingSet BuildResourceBindingSet(const MaterialResource& mat, const GDXShaderResource& shader)
    {
        ResourceBindingSet set;

        for (uint32_t i = 0; i < shader.layout.constantBufferCount; ++i)
        {
            const auto& src = shader.layout.constantBuffers[i];
            ConstantBufferBindingDesc cb{};
            cb.semantic = src.slot;
            cb.vsRegister = src.vsRegister;
            cb.psRegister = src.psRegister;
            cb.buffer = (src.slot == GDXShaderConstantBufferSlot::Material) ? mat.gpuConstantBuffer : nullptr;
            cb.enabled = (src.slot != GDXShaderConstantBufferSlot::Material) || (mat.gpuConstantBuffer != nullptr);
            cb.scope = (src.slot == GDXShaderConstantBufferSlot::Frame) ? ResourceBindingScope::Pass :
                       ((src.slot == GDXShaderConstantBufferSlot::Material) ? ResourceBindingScope::Material : ResourceBindingScope::Draw);
            set.AddConstantBufferBinding(cb);
        }

        for (uint32_t i = 0; i < shader.layout.textureBindingCount; ++i)
        {
            const auto& src = shader.layout.textureBindings[i];
            ShaderResourceBindingDesc desc{};
            switch (src.semantic)
            {
            case GDXShaderTextureSemantic::Albedo:   desc.semantic = ShaderResourceSemantic::Albedo; break;
            case GDXShaderTextureSemantic::Normal:   desc.semantic = ShaderResourceSemantic::Normal; break;
            case GDXShaderTextureSemantic::ORM:      desc.semantic = ShaderResourceSemantic::ORM; break;
            case GDXShaderTextureSemantic::Emissive: desc.semantic = ShaderResourceSemantic::Emissive; break;
            case GDXShaderTextureSemantic::Detail:   desc.semantic = ShaderResourceSemantic::Detail; break;
            case GDXShaderTextureSemantic::ShadowMap:
                desc.semantic = ShaderResourceSemantic::ShadowMap;
                desc.bindingIndex = src.shaderRegister;
                desc.texture = TextureHandle::Invalid();
                desc.uvSet = MaterialTextureUVSet::UV0;
                desc.enabled = false;
                desc.expectsSRGB = false;
                desc.requiredState = ResourceState::ShaderRead;
                desc.scope = ResourceBindingScope::Pass;
                set.AddTextureBinding(desc);
                continue;
            }

            const auto materialSlot = static_cast<MaterialTextureSlot>(static_cast<uint8_t>(desc.semantic));
            const auto& layer = mat.Layer(materialSlot);
            desc.bindingIndex = src.shaderRegister;
            desc.texture = layer.texture;
            desc.uvSet = (layer.uvSet == MaterialTextureUVSet::Auto)
                ? DefaultUVSetForSemantic(desc.semantic)
                : layer.uvSet;
            desc.enabled = layer.enabled;
            desc.expectsSRGB = layer.expectsSRGB;
            desc.requiredState = ResourceState::ShaderRead;
            desc.scope = (desc.semantic == ShaderResourceSemantic::ShadowMap) ? ResourceBindingScope::Pass : ResourceBindingScope::Material;
            set.AddTextureBinding(desc);
        }

        return set;
    }

    GDXPipelineStateDesc BuildPipelineStateDesc(RenderPass pass, const MaterialResource& mat)
    {
        GDXPipelineStateDesc desc{};
        desc.alphaTestEnabled = mat.IsAlphaTest();

        if (pass == RenderPass::Shadow)
        {
            desc.blendMode = GDXBlendMode::Opaque;
            desc.cullMode = mat.IsShadowDoubleSided() ? GDXCullMode::None : GDXCullMode::Back;
            desc.depthMode = GDXDepthMode::ReadWrite;
            desc.depthTestEnabled = true;
            return desc;
        }

        const bool transparent = (pass == RenderPass::Transparent) || mat.IsTransparent();
        desc.blendMode = transparent ? GDXBlendMode::AlphaBlend : GDXBlendMode::Opaque;
        desc.cullMode = mat.IsDoubleSided() ? GDXCullMode::None : GDXCullMode::Back;
        desc.depthMode = transparent ? GDXDepthMode::ReadOnly : GDXDepthMode::ReadWrite;
        desc.depthTestEnabled = true;
        return desc;
    }

    ResolvedRenderable ResolveRenderable(const VisibleRenderCandidate& candidate)
    {
        ResolvedRenderable resolved{};
        resolved.mesh = candidate.mesh;
        resolved.material = candidate.material;
        resolved.submeshIndex = candidate.submeshIndex;
        resolved.enabled = candidate.enabled;
        resolved.stateVersion = candidate.renderableStateVersion;
        resolved.visible = candidate.visible;
        resolved.active = candidate.active;
        resolved.layerMask = candidate.layerMask;
        resolved.castShadows = candidate.castShadows;
        resolved.receiveShadows = candidate.receiveShadows;
        return resolved;
    }

    bool ShouldUseCachedState(const RenderGatherSystem::CachedCommandState& cache,
                              const ResolvedRenderable& renderable,
                              const MaterialResource& material)
    {
        return cache.valid
            && cache.renderStateVersion == renderable.stateVersion
            && cache.mesh == renderable.mesh
            && cache.material == renderable.material
            && cache.submeshIndex == renderable.submeshIndex
            && cache.visible == renderable.visible
            && cache.active == renderable.active
            && cache.layerMask == renderable.layerMask
            && cache.castShadows == renderable.castShadows
            && cache.receiveShadows == renderable.receiveShadows
            && cache.materialCpuDirtySnapshot == material.cpuDirty;
    }

    bool BuildMainRenderCommand(const VisibleRenderCandidate& candidate,
                                const FrameData& frame,
                                ResourceStore<MeshAssetResource, MeshTag>& meshStore,
                                ResourceStore<MaterialResource, MaterialTag>& matStore,
                                ResourceStore<GDXShaderResource, ShaderTag>& shaderStore,
                                const RenderGatherSystem::ShaderResolver& resolveShader,
                                const RenderGatherOptions* options,
                                RenderGatherSystem::CachedCommandState& cache,
                                RenderCommand& outCmd,
                                bool& outTransparent)
    {
        const ResolvedRenderable renderable = ResolveRenderable(candidate);
        if (!renderable.active || !renderable.visible) return false;
        if ((renderable.layerMask & frame.cullMask) == 0u) return false;
        if (options && (renderable.layerMask & options->visibilityLayerMask) == 0u) return false;
        if (!renderable.enabled) return false;
        if (!renderable.mesh.IsValid()) return false;
        if (!renderable.material.IsValid()) return false;

        const MeshAssetResource* mesh = meshStore.Get(renderable.mesh);
        const MaterialResource* mat = matStore.Get(renderable.material);
        if (!mesh || !mat) return false;
        if (renderable.submeshIndex >= mesh->submeshes.size()) return false;
        if (!mesh->IsGpuReadyAt(renderable.submeshIndex)) return false;

        const SubmeshData& submesh = mesh->submeshes[renderable.submeshIndex];
        outTransparent = mat->IsTransparent();
        const RenderPass pass = outTransparent ? RenderPass::Transparent : RenderPass::Opaque;

        if (options)
        {
            if (outTransparent && !options->gatherTransparent) return false;
            if (!outTransparent && !options->gatherOpaque) return false;
        }

        if (!ShouldUseCachedState(cache, renderable, *mat) || cache.pass != pass)
        {
            const ShaderHandle shader = resolveShader(pass, submesh, *mat);
            if (!shader.IsValid()) return false;

            const GDXShaderResource* shaderRes = shaderStore.Get(shader);
            if (!shaderRes) return false;

            cache.mesh = renderable.mesh;
            cache.material = renderable.material;
            cache.shader = shader;
            cache.submeshIndex = renderable.submeshIndex;
            cache.pass = pass;
            cache.pipelineState = BuildPipelineStateDesc(pass, *mat);
            cache.bindings = BuildResourceBindingSet(*mat, *shaderRes);
            cache.materialData = mat->data;
            cache.transparent = outTransparent;
            cache.materialSortID = mat->sortID;
            cache.renderStateVersion = renderable.stateVersion;
            cache.visible = renderable.visible;
            cache.active = renderable.active;
            cache.layerMask = renderable.layerMask;
            cache.castShadows = renderable.castShadows;
            cache.receiveShadows = renderable.receiveShadows;
            cache.materialCpuDirtySnapshot = mat->cpuDirty;
            cache.valid = true;
        }
        else
        {
            outTransparent = cache.transparent;
        }

        if (options && options->skipSelfReferentialDraws &&
            UsesTextureAsShaderResource(cache.bindings, options->forbiddenShaderReadTexture))
        {
            Debug::LogWarning("RenderGatherSystem: skipped self-referential RTT draw for entity ", candidate.entity.value);
            return false;
        }

        const float ndcDepth = CameraSystem::ComputeNDCDepth(candidate.worldMatrix, frame.viewProjMatrix);
        const float depth = outTransparent ? (1.0f - ndcDepth) : ndcDepth;
        outCmd.mesh = cache.mesh;
        outCmd.material = cache.material;
        outCmd.shader = cache.shader;
        outCmd.submeshIndex = cache.submeshIndex;
        outCmd.ownerEntity = candidate.entity;
        outCmd.pass = cache.pass;
        outCmd.worldMatrix = candidate.worldMatrix;
        outCmd.SetBindings(
            cache.bindings,
            BuildResourceBindingScopeKey(cache.bindings, ResourceBindingScope::Pass, cache.shader.value),
            BuildResourceBindingScopeKey(cache.bindings, ResourceBindingScope::Material, cache.material.value),
            BuildResourceBindingScopeKey(cache.bindings, ResourceBindingScope::Draw, candidate.entity.value));
        outCmd.SetPipelineState(cache.pipelineState);
        outCmd.materialData = cache.materialData;
        outCmd.receiveShadows = renderable.receiveShadows;
        outCmd.SetSortKey(cache.pass,
                          cache.shader.Index() & 0x0FFFu,
                          GDXPipelineStateKey::FromDesc(cache.pipelineState).value & 0x00FFu,
                          cache.materialSortID & 0x03FFu,
                          depth);
        return true;
    }

    bool BuildShadowRenderCommand(const VisibleRenderCandidate& candidate,
                                  const FrameData& frame,
                                  ResourceStore<MeshAssetResource, MeshTag>& meshStore,
                                  ResourceStore<MaterialResource, MaterialTag>& matStore,
                                  ResourceStore<GDXShaderResource, ShaderTag>& shaderStore,
                                  const RenderGatherSystem::ShaderResolver& resolveShader,
                                  const RenderGatherOptions* options,
                                  RenderGatherSystem::CachedCommandState& cache,
                                  RenderCommand& outCmd)
    {
        const ResolvedRenderable renderable = ResolveRenderable(candidate);
        if (!renderable.active || !renderable.castShadows) return false;
        if ((renderable.layerMask & frame.cullMask) == 0u) return false;
        if (options && (renderable.layerMask & options->shadowCasterLayerMask) == 0u) return false;
        if (!renderable.enabled) return false;
        if (!renderable.mesh.IsValid()) return false;
        if (!renderable.material.IsValid()) return false;

        const MeshAssetResource* mesh = meshStore.Get(renderable.mesh);
        const MaterialResource* mat = matStore.Get(renderable.material);
        if (!mesh || !mat) return false;
        if (renderable.submeshIndex >= mesh->submeshes.size()) return false;
        if (!mesh->IsGpuReadyAt(renderable.submeshIndex)) return false;

        const SubmeshData& submesh = mesh->submeshes[renderable.submeshIndex];
        if (!ShouldUseCachedState(cache, renderable, *mat) || cache.pass != RenderPass::Shadow)
        {
            const ShaderHandle shader = resolveShader(RenderPass::Shadow, submesh, *mat);
            if (!shader.IsValid()) return false;

            const GDXShaderResource* shaderRes = shaderStore.Get(shader);
            if (!shaderRes) return false;

            cache.mesh = renderable.mesh;
            cache.material = renderable.material;
            cache.shader = shader;
            cache.submeshIndex = renderable.submeshIndex;
            cache.pass = RenderPass::Shadow;
            cache.pipelineState = BuildPipelineStateDesc(RenderPass::Shadow, *mat);
            cache.bindings = BuildResourceBindingSet(*mat, *shaderRes);
            cache.materialData = mat->data;
            cache.transparent = false;
            cache.materialSortID = mat->sortID;
            cache.renderStateVersion = renderable.stateVersion;
            cache.visible = renderable.visible;
            cache.active = renderable.active;
            cache.layerMask = renderable.layerMask;
            cache.castShadows = renderable.castShadows;
            cache.receiveShadows = renderable.receiveShadows;
            cache.materialCpuDirtySnapshot = mat->cpuDirty;
            cache.valid = true;
        }

        if (options && options->skipSelfReferentialDraws &&
            UsesTextureAsShaderResource(cache.bindings, options->forbiddenShaderReadTexture))
        {
            Debug::LogWarning("RenderGatherSystem: skipped self-referential RTT shadow draw for entity ", candidate.entity.value);
            return false;
        }

        outCmd.mesh = cache.mesh;
        outCmd.material = cache.material;
        outCmd.shader = cache.shader;
        outCmd.submeshIndex = cache.submeshIndex;
        outCmd.ownerEntity = candidate.entity;
        outCmd.pass = RenderPass::Shadow;
        outCmd.worldMatrix = candidate.worldMatrix;
        outCmd.SetBindings(
            cache.bindings,
            BuildResourceBindingScopeKey(cache.bindings, ResourceBindingScope::Pass, cache.shader.value),
            BuildResourceBindingScopeKey(cache.bindings, ResourceBindingScope::Material, cache.material.value),
            BuildResourceBindingScopeKey(cache.bindings, ResourceBindingScope::Draw, candidate.entity.value));
        outCmd.SetPipelineState(cache.pipelineState);
        outCmd.materialData = cache.materialData;
        outCmd.receiveShadows = true;
        outCmd.SetSortKey(RenderPass::Shadow,
                          cache.shader.Index() & 0x0FFFu,
                          GDXPipelineStateKey::FromDesc(cache.pipelineState).value & 0x00FFu,
                          0u,
                          0.0f);
        return true;
    }

    template<typename Selector>
    void MergeCommandVectors(const std::vector<RenderGatherSystem::GatherChunkResult>& chunks,
                             Selector selector,
                             RenderQueue& outQueue)
    {
        size_t totalCount = 0u;
        for (const auto& chunk : chunks)
            totalCount += selector(chunk).size();

        outQueue.commands.clear();
        outQueue.commands.reserve(totalCount);
        for (const auto& chunk : chunks)
        {
            const auto& src = selector(chunk);
            outQueue.commands.insert(outQueue.commands.end(), src.begin(), src.end());
        }
    }
}

void RenderGatherSystem::GatherVisibleSetChunks(const VisibleSet& visibleSet,
                                                 const FrameData& frame,
                                                 ResourceStore<MeshAssetResource, MeshTag>& meshStore,
                                                 ResourceStore<MaterialResource, MaterialTag>& matStore,
                                                 ResourceStore<GDXShaderResource, ShaderTag>& shaderStore,
                                                 const ShaderResolver& resolveShader,
                                                 std::vector<GatherChunkResult>& outChunkResults,
                                                 const RenderGatherOptions* options,
                                                 JobSystem* jobSystem) const
{
    outChunkResults.clear();

    const size_t total = visibleSet.candidates.size();
    if (total == 0u)
        return;

    const size_t batchSize = 128u;
    const size_t chunkCount = (total + batchSize - 1u) / batchSize;
    outChunkResults.resize(chunkCount);

    auto processRange = [&](size_t beginChunk, size_t endChunk)
    {
        for (size_t chunkIndex = beginChunk; chunkIndex < endChunk; ++chunkIndex)
        {
            const size_t begin = chunkIndex * batchSize;
            const size_t end = (std::min)(begin + batchSize, total);
            auto& result = outChunkResults[chunkIndex];
            result.opaque.reserve(end - begin);
            result.transparent.reserve(end - begin);

            for (size_t i = begin; i < end; ++i)
            {
                const auto& candidate = visibleSet.candidates[i];
                RenderCommand cmd;
                bool transparent = false;
                auto& cache = result.mainCache[candidate.entity];
                if (!BuildMainRenderCommand(candidate, frame, meshStore, matStore, shaderStore,
                                            resolveShader, options, cache, cmd, transparent))
                {
                    continue;
                }

                if (transparent)
                    result.transparent.push_back(std::move(cmd));
                else
                    result.opaque.push_back(std::move(cmd));
            }
        }
    };

    if (jobSystem)
        jobSystem->ParallelFor(chunkCount, processRange, 1u);
    else
        processRange(0u, chunkCount);
}

void RenderGatherSystem::GatherShadowVisibleSetChunks(const VisibleSet& visibleSet,
                                                      const FrameData& frame,
                                                      ResourceStore<MeshAssetResource, MeshTag>& meshStore,
                                                      ResourceStore<MaterialResource, MaterialTag>& matStore,
                                                      ResourceStore<GDXShaderResource, ShaderTag>& shaderStore,
                                                      const ShaderResolver& resolveShader,
                                                      std::vector<GatherChunkResult>& outChunkResults,
                                                      const RenderGatherOptions* options,
                                                      JobSystem* jobSystem) const
{
    outChunkResults.clear();
    if (options && !options->gatherShadows)
        return;

    const size_t total = visibleSet.candidates.size();
    if (total == 0u)
        return;

    const size_t batchSize = 128u;
    const size_t chunkCount = (total + batchSize - 1u) / batchSize;
    outChunkResults.resize(chunkCount);

    auto processRange = [&](size_t beginChunk, size_t endChunk)
    {
        for (size_t chunkIndex = beginChunk; chunkIndex < endChunk; ++chunkIndex)
        {
            const size_t begin = chunkIndex * batchSize;
            const size_t end = (std::min)(begin + batchSize, total);
            auto& result = outChunkResults[chunkIndex];
            result.shadow.reserve(end - begin);

            for (size_t i = begin; i < end; ++i)
            {
                const auto& candidate = visibleSet.candidates[i];
                RenderCommand cmd;
                auto& cache = result.shadowCache[candidate.entity];
                if (!BuildShadowRenderCommand(candidate, frame, meshStore, matStore, shaderStore,
                                              resolveShader, options, cache, cmd))
                {
                    continue;
                }
                result.shadow.push_back(std::move(cmd));
            }
        }
    };

    if (jobSystem)
        jobSystem->ParallelFor(chunkCount, processRange, 1u);
    else
        processRange(0u, chunkCount);
}

void RenderGatherSystem::MergeVisibleSetChunks(const std::vector<GatherChunkResult>& chunkResults,
                                              RenderQueue& outOpaqueQueue,
                                              RenderQueue& outTransparentQueue) const
{
    outOpaqueQueue.Clear();
    outTransparentQueue.Clear();
    MergeCommandVectors(chunkResults, [](const GatherChunkResult& c) -> const std::vector<RenderCommand>& { return c.opaque; }, outOpaqueQueue);
    MergeCommandVectors(chunkResults, [](const GatherChunkResult& c) -> const std::vector<RenderCommand>& { return c.transparent; }, outTransparentQueue);
}

void RenderGatherSystem::MergeShadowVisibleSetChunks(const std::vector<GatherChunkResult>& chunkResults,
                                                     RenderQueue& outShadowQueue) const
{
    outShadowQueue.Clear();
    MergeCommandVectors(chunkResults, [](const GatherChunkResult& c) -> const std::vector<RenderCommand>& { return c.shadow; }, outShadowQueue);
}

void RenderGatherSystem::SortRenderQueue(RenderQueue& queue)
{
    queue.Sort();
}

void RenderGatherSystem::GatherVisibleSet(const VisibleSet& visibleSet,
                                          const FrameData& frame,
                                          ResourceStore<MeshAssetResource, MeshTag>& meshStore,
                                          ResourceStore<MaterialResource, MaterialTag>& matStore,
                                          ResourceStore<GDXShaderResource, ShaderTag>& shaderStore,
                                          const ShaderResolver& resolveShader,
                                          RenderQueue& outOpaqueQueue,
                                          RenderQueue& outTransparentQueue,
                                          const RenderGatherOptions* options,
                                          JobSystem* jobSystem) const
{
    std::vector<GatherChunkResult> chunkResults;
    GatherVisibleSetChunks(visibleSet, frame, meshStore, matStore, shaderStore, resolveShader, chunkResults, options, jobSystem);
    MergeVisibleSetChunks(chunkResults, outOpaqueQueue, outTransparentQueue);
    SortRenderQueue(outOpaqueQueue);
    SortRenderQueue(outTransparentQueue);
}

void RenderGatherSystem::GatherShadowVisibleSet(const VisibleSet& visibleSet,
                                                const FrameData& frame,
                                                ResourceStore<MeshAssetResource, MeshTag>& meshStore,
                                                ResourceStore<MaterialResource, MaterialTag>& matStore,
                                                ResourceStore<GDXShaderResource, ShaderTag>& shaderStore,
                                                const ShaderResolver& resolveShader,
                                                RenderQueue& outShadowQueue,
                                                const RenderGatherOptions* options,
                                                JobSystem* jobSystem) const
{
    std::vector<GatherChunkResult> chunkResults;
    GatherShadowVisibleSetChunks(visibleSet, frame, meshStore, matStore, shaderStore, resolveShader, chunkResults, options, jobSystem);
    MergeShadowVisibleSetChunks(chunkResults, outShadowQueue);
    SortRenderQueue(outShadowQueue);
}
