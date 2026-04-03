#include "RenderGatherSystem.h"
#include "Core/Debug.h"
#include "MaterialSemanticLayout.h"

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
        DrawPassMask drawPassMask = DrawPassBits::None;
        uint8_t renderPriority = 128u;
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

    ResourceBindingSet BuildResourceBindingSet(const MaterialResource& mat, MaterialHandle matHandle, const GDXShaderResource& shader)
    {
        ResourceBindingSet set;
        const MaterialSemanticLayout materialLayout = MaterialSemanticLayout::BuildDefault();

        for (uint32_t i = 0; i < shader.layout.constantBufferCount; ++i)
        {
            const auto& src = shader.layout.constantBuffers[i];
            ConstantBufferBindingDesc cb{};
            cb.semantic = src.slot;
            cb.bindingIndex = src.layoutBindingIndex;
            cb.bindingGroup = src.bindingGroup;
            cb.resourceClass = src.resourceClass;
            cb.visibility = src.visibility;
            cb.materialHandle = (src.slot == GDXShaderConstantBufferSlot::Material) ? matHandle : MaterialHandle::Invalid();
            cb.enabled = (src.slot != GDXShaderConstantBufferSlot::Material) || matHandle.IsValid();
            cb.required = true;
            cb.scope = GDXBindingScopeForConstantBufferSlot(src.slot);
            set.AddConstantBufferBinding(cb);
        }

        for (uint32_t i = 0; i < shader.layout.textureBindingCount; ++i)
        {
            const auto& src = shader.layout.textureBindings[i];
            ShaderResourceBindingDesc desc{};
            MaterialSemanticValidationResult validation{};
            if (!materialLayout.BuildTextureBindingDesc(src, mat, desc, &validation))
            {
                Debug::LogError(GDX_SRC_LOC, "MaterialSemanticLayout: BuildTextureBindingDesc fehlgeschlagen.");
                continue;
            }
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
        resolved.drawPassMask = candidate.drawPassMask;
        resolved.renderPriority = candidate.renderPriority;
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
            && cache.drawPassMask == renderable.drawPassMask
            && cache.renderPriority == renderable.renderPriority
            && cache.materialStateVersion == material.GetStateVersion();
    }

    bool SupportsCombinedPass(const ResolvedRenderable& renderable,
                              const MaterialResource& material,
                              const RenderViewData* view,
                              DrawPassType pass) noexcept
    {
        const DrawPassMask viewMask = view ? view->viewPassMask : DrawPassBits::AllGraphics;
        return HasDrawPass(renderable.drawPassMask, pass)
            && material.SupportsPass(pass)
            && HasDrawPass(viewMask, pass);
    }

    bool IsDepthEligible(const ResolvedRenderable& renderable,
                         const MaterialResource& material,
                         const RenderViewData* view,
                         bool transparent) noexcept
    {
        return SupportsCombinedPass(renderable, material, view, DrawPassType::Depth)
            && (!transparent || material.IsAlphaTest());
    }

    bool IsMotionVectorEligible(const ResolvedRenderable& renderable,
                                const MaterialResource& material,
                                const RenderViewData* view) noexcept
    {
        return SupportsCombinedPass(renderable, material, view, DrawPassType::MotionVectors);
    }

    bool IsDistortionEligible(const ResolvedRenderable& renderable,
                              const MaterialResource& material,
                              const RenderViewData* view) noexcept
    {
        return material.IsTransparent()
            && material.IsDistortion()
            && SupportsCombinedPass(renderable, material, view, DrawPassType::Distortion);
    }

    uint16_t BuildGeometrySortKey(const MeshHandle mesh, uint32_t submeshIndex) noexcept
    {
        const uint32_t packed = ((mesh.value & 0x03FFu) << 6) ^ (submeshIndex & 0x003Fu);
        return static_cast<uint16_t>(packed & 0xFFFFu);
    }

    RenderQueueClass ResolveQueueClass(bool transparent, bool distortion, DrawPassType drawPass) noexcept
    {
        switch (drawPass)
        {
        case DrawPassType::Depth:         return RenderQueueClass::Depth;
        case DrawPassType::MotionVectors: return RenderQueueClass::MotionVectors;
        case DrawPassType::ShadowDepth:   return RenderQueueClass::ShadowDepth;
        case DrawPassType::Distortion:    return RenderQueueClass::Distortion;
        case DrawPassType::Transparent:   return RenderQueueClass::Transparent;
        case DrawPassType::Opaque:
        default:
            return distortion ? RenderQueueClass::Distortion
                              : (transparent ? RenderQueueClass::Transparent : RenderQueueClass::Opaque);
        }
    }



    bool IsDrawBindingInstancingCompatible(const ResourceBindingSet& bindings) noexcept
    {
        for (uint32_t i = 0; i < bindings.textureCount; ++i)
        {
            if (bindings.textures[i].scope == ResourceBindingScope::Draw && bindings.textures[i].enabled)
                return false;
        }

        for (uint32_t i = 0; i < bindings.constantBufferCount; ++i)
        {
            const auto& cb = bindings.constantBuffers[i];
            if (cb.scope != ResourceBindingScope::Draw || !cb.enabled)
                continue;
            if (cb.semantic != GDXShaderConstantBufferSlot::Entity)
                return false;
        }

        return true;
    }

    uint64_t BuildInstancingKey(const RenderCommand& cmd) noexcept
    {
        uint64_t key = 1469598103934665603ull;
        auto mix = [&](uint64_t value)
        {
            key ^= value;
            key *= 1099511628211ull;
        };

        mix(static_cast<uint64_t>(cmd.pass));
        mix(static_cast<uint64_t>(cmd.mesh.value));
        mix(static_cast<uint64_t>(cmd.submeshIndex));
        mix(static_cast<uint64_t>(cmd.material.value));
        mix(static_cast<uint64_t>(cmd.shader.value));
        mix(static_cast<uint64_t>(cmd.pipelineStateKey.value));
        mix(cmd.passBindingsKey);
        mix(cmd.materialBindingsKey);
        mix(static_cast<uint64_t>(cmd.receiveShadows ? 1u : 0u));
        return key;
    }

    void AssignPackedSortKey(RenderCommand& cmd,
                             const RenderGatherSystem::CachedCommandState& cache,
                             DrawPassType drawPass,
                             float depth)
    {
        RenderSortKeyParams params{};
        params.queueClass = ResolveQueueClass(cache.transparent, cache.distortion, drawPass);
        params.renderPriority = cmd.renderPriority;
        params.transparencyClass = cache.transparencyClass;
        params.transparencySortPriority = cache.transparencySortPriority;
        params.shaderKey = static_cast<uint16_t>(cache.shader.Index() & 0x0FFFu);
        params.pipelineKey = static_cast<uint16_t>(GDXPipelineStateKey::FromDesc(cache.pipelineState).value & 0x1FFFu);
        params.materialKey = static_cast<uint16_t>(cache.materialSortID & 0x0FFFu);
        params.geometryKey = BuildGeometrySortKey(cache.mesh, cache.submeshIndex);
        params.depth = depth;
        cmd.SetSortKey(params);
    }

    bool BuildMainRenderCommand(const VisibleRenderCandidate& candidate,
                                const RenderViewData* view,
                                const FrameData& frame,
                                ResourceStore<MeshAssetResource, MeshTag>& meshStore,
                                ResourceStore<MaterialResource, MaterialTag>& matStore,
                                ResourceStore<GDXShaderResource, ShaderTag>& shaderStore,
                                const RenderGatherSystem::ShaderResolver& resolveShader,
                                const RenderGatherOptions* options,
                                RenderGatherSystem::CachedCommandState& cache,
                                RenderCommand& outCmd,
                                bool& outTransparent,
                                bool& outDistortion,
                                ResolvedRenderable& outRenderable,
                                const MaterialResource*& outMaterial)
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

        outTransparent = mat->IsTransparent();
        outDistortion = mat->IsDistortion();

        const DrawPassType primaryPass = outTransparent ? (outDistortion ? DrawPassType::Distortion : DrawPassType::Transparent)
                                                        : DrawPassType::Opaque;
        if (!SupportsCombinedPass(renderable, *mat, view, primaryPass))
            return false;

        if (options)
        {
            if (outTransparent && !options->gatherTransparent) return false;
            if (!outTransparent && !options->gatherOpaque) return false;
        }

        const SubmeshData& submesh = mesh->submeshes[renderable.submeshIndex];
        const RenderPass pass = outTransparent ? RenderPass::Transparent : RenderPass::Opaque;

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
            cache.bindings = BuildResourceBindingSet(*mat, renderable.material, *shaderRes);
            cache.transparent = outTransparent;
            cache.distortion = outDistortion;
            cache.drawPassMask = renderable.drawPassMask;
            cache.renderPriority = renderable.renderPriority;
            cache.transparencySortPriority = mat->GetTransparencySortPriority();
            cache.transparencyClass = static_cast<uint8_t>(mat->GetTransparencyClass());
            cache.materialSortID = mat->GetSortID();
            cache.renderStateVersion = renderable.stateVersion;
            cache.visible = renderable.visible;
            cache.active = renderable.active;
            cache.layerMask = renderable.layerMask;
            cache.castShadows = renderable.castShadows;
            cache.receiveShadows = renderable.receiveShadows;
            cache.materialStateVersion = mat->GetStateVersion();
            cache.valid = true;
        }
        else
        {
            outTransparent = cache.transparent;
            outDistortion = cache.distortion;
        }

        if (options && options->skipSelfReferentialDraws &&
            UsesTextureAsShaderResource(cache.bindings, options->forbiddenShaderReadTexture))
        {
            Debug::LogWarning("RenderGatherSystem: skipped self-referential RTT draw for entity ", candidate.entity.value);
            return false;
        }

        const float ndcDepth = CameraSystem::ComputeNDCDepth(candidate.worldMatrix, frame.viewProjMatrix);
        outCmd.mesh = cache.mesh;
        outCmd.material = cache.material;
        outCmd.shader = cache.shader;
        outCmd.submeshIndex = cache.submeshIndex;
        outCmd.ownerEntity = candidate.entity;
        outCmd.pass = cache.pass;
        outCmd.renderPriority = cache.renderPriority;
        outCmd.worldMatrix = candidate.worldMatrix;
        outCmd.SetBindings(
            cache.bindings,
            BuildResourceBindingScopeKey(cache.bindings, ResourceBindingScope::Pass, cache.shader.value),
            BuildResourceBindingScopeKey(cache.bindings, ResourceBindingScope::Material, cache.material.value),
            BuildResourceBindingScopeKey(cache.bindings, ResourceBindingScope::Draw, candidate.entity.value));
        outCmd.SetPipelineState(cache.pipelineState);
        outCmd.receiveShadows = renderable.receiveShadows;
        outCmd.instancingEligible = !outTransparent
            && !outDistortion
            && shaderStore.Get(cache.shader)
            && shaderStore.Get(cache.shader)->supportsInstancing
            && !shaderStore.Get(cache.shader)->supportsSkinning
            && IsDrawBindingInstancingCompatible(cache.bindings);
        outCmd.instancingKey = outCmd.instancingEligible ? BuildInstancingKey(outCmd) : 0ull;
        outCmd.hasBounds         = candidate.hasBounds;
        outCmd.worldBoundsCenter = candidate.worldBoundsCenter;
        outCmd.worldBoundsRadius = candidate.worldBoundsRadius;
        AssignPackedSortKey(outCmd, cache, primaryPass, ndcDepth);

        outRenderable = renderable;
        outMaterial = mat;
        return true;
    }

    bool BuildShadowRenderCommand(const VisibleRenderCandidate& candidate,
                                  const RenderViewData* view,
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
        if (!SupportsCombinedPass(renderable, *mat, view, DrawPassType::ShadowDepth)) return false;

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
            cache.bindings = BuildResourceBindingSet(*mat, renderable.material, *shaderRes);
            cache.transparent = false;
            cache.distortion = false;
            cache.drawPassMask = renderable.drawPassMask;
            cache.renderPriority = renderable.renderPriority;
            cache.transparencySortPriority = mat->GetTransparencySortPriority();
            cache.transparencyClass = static_cast<uint8_t>(mat->GetTransparencyClass());
            cache.materialSortID = mat->GetSortID();
            cache.renderStateVersion = renderable.stateVersion;
            cache.visible = renderable.visible;
            cache.active = renderable.active;
            cache.layerMask = renderable.layerMask;
            cache.castShadows = renderable.castShadows;
            cache.receiveShadows = renderable.receiveShadows;
            cache.materialStateVersion = mat->GetStateVersion();
            cache.valid = true;
        }

        if (options && options->skipSelfReferentialDraws &&
            UsesTextureAsShaderResource(cache.bindings, options->forbiddenShaderReadTexture))
        {
            Debug::LogWarning("RenderGatherSystem: skipped self-referential RTT shadow draw for entity ", candidate.entity.value);
            return false;
        }

        const float depth = CameraSystem::ComputeNDCDepth(candidate.worldMatrix, frame.shadowViewProjMatrix);
        outCmd.mesh = cache.mesh;
        outCmd.material = cache.material;
        outCmd.shader = cache.shader;
        outCmd.submeshIndex = cache.submeshIndex;
        outCmd.ownerEntity = candidate.entity;
        outCmd.pass = RenderPass::Shadow;
        outCmd.renderPriority = cache.renderPriority;
        outCmd.worldMatrix = candidate.worldMatrix;
        outCmd.SetBindings(
            cache.bindings,
            BuildResourceBindingScopeKey(cache.bindings, ResourceBindingScope::Pass, cache.shader.value),
            BuildResourceBindingScopeKey(cache.bindings, ResourceBindingScope::Material, cache.material.value),
            BuildResourceBindingScopeKey(cache.bindings, ResourceBindingScope::Draw, candidate.entity.value));
        outCmd.SetPipelineState(cache.pipelineState);
        outCmd.receiveShadows = true;
        outCmd.instancingEligible = shaderStore.Get(cache.shader)
            && shaderStore.Get(cache.shader)->supportsInstancing
            && !shaderStore.Get(cache.shader)->supportsSkinning
            && IsDrawBindingInstancingCompatible(cache.bindings);
        outCmd.instancingKey = outCmd.instancingEligible ? BuildInstancingKey(outCmd) : 0ull;
        outCmd.hasBounds         = candidate.hasBounds;
        outCmd.worldBoundsCenter = candidate.worldBoundsCenter;
        outCmd.worldBoundsRadius = candidate.worldBoundsRadius;
        AssignPackedSortKey(outCmd, cache, DrawPassType::ShadowDepth, depth);
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
            result.depth.reserve(end - begin);
            result.opaque.reserve(end - begin);
            result.transparent.reserve(end - begin);
            result.distortion.reserve(end - begin);
            result.motionVectors.reserve(end - begin);

            for (size_t i = begin; i < end; ++i)
            {
                const auto& candidate = visibleSet.candidates[i];
                RenderCommand cmd;
                bool transparent = false;
                bool distortion = false;
                ResolvedRenderable renderable{};
                const MaterialResource* material = nullptr;
                auto& cache = result.mainCache[candidate.entity];
                if (!BuildMainRenderCommand(candidate, nullptr, frame, meshStore, matStore, shaderStore,
                                            resolveShader, options, cache, cmd, transparent, distortion, renderable, material))
                {
                    continue;
                }

                if (!material)
                    continue;

                if (IsDepthEligible(renderable, *material, nullptr, transparent))
                {
                    RenderCommand depthCmd = cmd;
                    AssignPackedSortKey(depthCmd, cache, DrawPassType::Depth, CameraSystem::ComputeNDCDepth(candidate.worldMatrix, frame.viewProjMatrix));
                    result.depth.push_back(std::move(depthCmd));
                }
                if (IsMotionVectorEligible(renderable, *material, nullptr))
                {
                    RenderCommand motionCmd = cmd;
                    AssignPackedSortKey(motionCmd, cache, DrawPassType::MotionVectors, CameraSystem::ComputeNDCDepth(candidate.worldMatrix, frame.viewProjMatrix));
                    result.motionVectors.push_back(std::move(motionCmd));
                }
                if (IsDistortionEligible(renderable, *material, nullptr))
                {
                    RenderCommand distortionCmd = cmd;
                    distortionCmd.pass = RenderPass::Distortion;
                    AssignPackedSortKey(distortionCmd, cache, DrawPassType::Distortion, CameraSystem::ComputeNDCDepth(candidate.worldMatrix, frame.viewProjMatrix));
                    result.distortion.push_back(std::move(distortionCmd));
                }

                if (transparent)
                {
                    if (!distortion)
                        result.transparent.push_back(std::move(cmd));
                }
                else
                {
                    result.opaque.push_back(std::move(cmd));
                }
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
                if (!BuildShadowRenderCommand(candidate, nullptr, frame, meshStore, matStore, shaderStore,
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
                                              RenderQueue& outDepthQueue,
                                              RenderQueue& outOpaqueQueue,
                                              RenderQueue& outTransparentQueue,
                                              RenderQueue& outDistortionQueue,
                                              RenderQueue& outMotionVectorQueue) const
{
    outDepthQueue.Clear();
    outOpaqueQueue.Clear();
    outTransparentQueue.Clear();
    outDistortionQueue.Clear();
    outMotionVectorQueue.Clear();
    MergeCommandVectors(chunkResults, [](const GatherChunkResult& c) -> const std::vector<RenderCommand>& { return c.depth; }, outDepthQueue);
    MergeCommandVectors(chunkResults, [](const GatherChunkResult& c) -> const std::vector<RenderCommand>& { return c.opaque; }, outOpaqueQueue);
    MergeCommandVectors(chunkResults, [](const GatherChunkResult& c) -> const std::vector<RenderCommand>& { return c.transparent; }, outTransparentQueue);
    MergeCommandVectors(chunkResults, [](const GatherChunkResult& c) -> const std::vector<RenderCommand>& { return c.distortion; }, outDistortionQueue);
    MergeCommandVectors(chunkResults, [](const GatherChunkResult& c) -> const std::vector<RenderCommand>& { return c.motionVectors; }, outMotionVectorQueue);
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
                                          RenderQueue& outDepthQueue,
                                          RenderQueue& outOpaqueQueue,
                                          RenderQueue& outTransparentQueue,
                                          RenderQueue& outDistortionQueue,
                                          RenderQueue& outMotionVectorQueue,
                                          const RenderGatherOptions* options,
                                          JobSystem* jobSystem) const
{
    std::vector<GatherChunkResult> chunkResults;
    GatherVisibleSetChunks(visibleSet, frame, meshStore, matStore, shaderStore, resolveShader, chunkResults, options, jobSystem);
    MergeVisibleSetChunks(chunkResults, outDepthQueue, outOpaqueQueue, outTransparentQueue, outDistortionQueue, outMotionVectorQueue);
    SortRenderQueue(outDepthQueue);
    SortRenderQueue(outOpaqueQueue);
    SortRenderQueue(outTransparentQueue);
    SortRenderQueue(outDistortionQueue);
    SortRenderQueue(outMotionVectorQueue);
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
