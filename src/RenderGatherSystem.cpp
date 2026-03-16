#include "RenderGatherSystem.h"
#include "Debug.h"

namespace
{

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
            cb.prepared = true;
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
                desc.prepared = true;
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
            desc.prepared = true;
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
}

void RenderGatherSystem::Gather(
    Registry&                                      registry,
    const FrameData&                               frame,
    ResourceStore<MeshAssetResource, MeshTag>&     meshStore,
    ResourceStore<MaterialResource,  MaterialTag>& matStore,
    ResourceStore<GDXShaderResource, ShaderTag>&   shaderStore,
    const ShaderResolver&                          resolveShader,
    RenderQueue&                                   outOpaqueQueue,
    RenderQueue&                                   outTransparentQueue,
    const RenderGatherOptions*                     options) const
{
    outOpaqueQueue.Clear();
    outTransparentQueue.Clear();

    registry.View<WorldTransformComponent,
                  MeshRefComponent,
                  MaterialRefComponent,
                  VisibilityComponent>(
        [&](EntityID entity,
            WorldTransformComponent& wt,
            MeshRefComponent&        mr,
            MaterialRefComponent&    matr,
            VisibilityComponent&     vis)
        {
            if (!vis.visible || !vis.active) return;
            if ((vis.layerMask & frame.cullMask) == 0u) return;
            if (options && (vis.layerMask & options->visibilityLayerMask) == 0u) return;
            if (!mr.enabled)                 return;
            if (!mr.mesh.IsValid())          return;
            if (!matr.material.IsValid())    return;

            const MeshAssetResource* mesh = meshStore.Get(mr.mesh);
            const MaterialResource*  mat  = matStore.Get(matr.material);

            if (!mesh || !mat)                            return;
            if (mr.submeshIndex >= mesh->submeshes.size()) return;
            if (!mesh->IsGpuReadyAt(mr.submeshIndex))     return;

            const SubmeshData& submesh = mesh->submeshes[mr.submeshIndex];
            const bool transparent = mat->IsTransparent();
            const RenderPass pass = transparent ? RenderPass::Transparent : RenderPass::Opaque;

            if (options)
            {
                if (transparent && !options->gatherTransparent) return;
                if (!transparent && !options->gatherOpaque) return;
            }

            const ShaderHandle shader = resolveShader(pass, submesh, *mat);
            if (!shader.IsValid()) return;

            const float ndcDepth = CameraSystem::ComputeNDCDepth(wt.matrix, frame.viewProjMatrix);
            const float depth = transparent ? (1.0f - ndcDepth) : ndcDepth;

            const GDXPipelineStateDesc pipelineState = BuildPipelineStateDesc(pass, *mat);
            const uint32_t shaderSortID = shader.Index() & 0x0FFFu;
            const uint32_t pipelineSortID = GDXPipelineStateKey::FromDesc(pipelineState).value & 0x00FFu;
            const uint32_t materialSortID = mat->sortID & 0x03FFu;
            const GDXShaderResource* shaderRes = shaderStore.Get(shader);
            if (!shaderRes) return;

            const ResourceBindingSet bindings = BuildResourceBindingSet(*mat, *shaderRes);

            if (options && options->skipSelfReferentialDraws &&
                UsesTextureAsShaderResource(bindings, options->forbiddenShaderReadTexture))
            {
                Debug::LogWarning("RenderGatherSystem: skipped self-referential RTT draw for entity ", entity.value);
                return;
            }

            RenderCommand cmd;
            cmd.mesh = mr.mesh;
            cmd.material = matr.material;
            cmd.shader = shader;
            cmd.submeshIndex = mr.submeshIndex;
            cmd.ownerEntity = entity;
            cmd.pass = pass;
            cmd.worldMatrix = wt.matrix;
            cmd.SetBindings(
                bindings,
                BuildResourceBindingScopeKey(bindings, ResourceBindingScope::Pass, shader.value),
                BuildResourceBindingScopeKey(bindings, ResourceBindingScope::Material, matr.material.value),
                BuildResourceBindingScopeKey(bindings, ResourceBindingScope::Draw, entity.value));
            cmd.SetPipelineState(pipelineState);
            cmd.materialData = mat->data;
            cmd.receiveShadows = vis.receiveShadows;
            cmd.SetSortKey(pass, shaderSortID, pipelineSortID, materialSortID, depth);

            RenderQueue& targetQueue = transparent ? outTransparentQueue : outOpaqueQueue;
            targetQueue.Submit(std::move(cmd));
        });

    outOpaqueQueue.Sort();
    outTransparentQueue.Sort();
}

void RenderGatherSystem::GatherShadow(
    Registry&                                      registry,
    const FrameData&                               frame,
    ResourceStore<MeshAssetResource, MeshTag>&     meshStore,
    ResourceStore<MaterialResource,  MaterialTag>& matStore,
    ResourceStore<GDXShaderResource, ShaderTag>&   shaderStore,
    const ShaderResolver&                          resolveShader,
    RenderQueue&                                   outShadowQueue,
    const RenderGatherOptions*                     options) const
{
    outShadowQueue.Clear();
    if (options && !options->gatherShadows)
        return;

    registry.View<WorldTransformComponent,
                  MeshRefComponent,
                  MaterialRefComponent,
                  VisibilityComponent>(
        [&](EntityID entity,
            WorldTransformComponent& wt,
            MeshRefComponent&        mr,
            MaterialRefComponent&    matr,
            VisibilityComponent&     vis)
        {
            if (!vis.active || !vis.castShadows) return;
            if ((vis.layerMask & frame.cullMask) == 0u) return;
            if (options && (vis.layerMask & options->shadowCasterLayerMask) == 0u) return;
            if (!mr.enabled || !mr.mesh.IsValid()) return;
            if (!matr.material.IsValid()) return;

            const MeshAssetResource* mesh = meshStore.Get(mr.mesh);
            const MaterialResource*  mat  = matStore.Get(matr.material);
            if (!mesh || !mat) return;
            if (mr.submeshIndex >= mesh->submeshes.size()) return;
            if (!mesh->IsGpuReadyAt(mr.submeshIndex)) return;

            const SubmeshData& submesh = mesh->submeshes[mr.submeshIndex];
            const ShaderHandle shader = resolveShader(RenderPass::Shadow, submesh, *mat);
            if (!shader.IsValid()) return;

            const GDXShaderResource* shaderRes = shaderStore.Get(shader);
            if (!shaderRes) return;

            const ResourceBindingSet bindings = BuildResourceBindingSet(*mat, *shaderRes);
            if (options && options->skipSelfReferentialDraws &&
                UsesTextureAsShaderResource(bindings, options->forbiddenShaderReadTexture))
            {
                Debug::LogWarning("RenderGatherSystem: skipped self-referential RTT shadow draw for entity ", entity.value);
                return;
            }

            const GDXPipelineStateDesc pipelineState = BuildPipelineStateDesc(RenderPass::Shadow, *mat);
            const uint32_t pipelineSortID = GDXPipelineStateKey::FromDesc(pipelineState).value & 0x00FFu;

            RenderCommand cmd;
            cmd.mesh = mr.mesh;
            cmd.material = matr.material;
            cmd.shader = shader;
            cmd.submeshIndex = mr.submeshIndex;
            cmd.ownerEntity = entity;
            cmd.pass = RenderPass::Shadow;
            cmd.worldMatrix = wt.matrix;
            cmd.SetBindings(
                bindings,
                BuildResourceBindingScopeKey(bindings, ResourceBindingScope::Pass, shader.value),
                BuildResourceBindingScopeKey(bindings, ResourceBindingScope::Material, matr.material.value),
                BuildResourceBindingScopeKey(bindings, ResourceBindingScope::Draw, entity.value));
            cmd.SetPipelineState(pipelineState);
            cmd.materialData = mat->data;
            cmd.receiveShadows = true;
            cmd.SetSortKey(RenderPass::Shadow,
                           shader.Index() & 0x0FFFu,
                           pipelineSortID, 0u, 0.0f);

            outShadowQueue.Submit(std::move(cmd));
        });

    outShadowQueue.Sort();
}
