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

    ResourceBindingSet BuildResourceBindingSet(const MaterialResource& mat)
    {
        ResourceBindingSet set;
        set.materialConstantBuffer = mat.gpuConstantBuffer;
        set.materialConstantBufferSlot = 2u;

        for (uint32_t i = 0; i < static_cast<uint32_t>(MaterialTextureSlot::Count); ++i)
        {
            const auto slot = static_cast<MaterialTextureSlot>(i);
            const auto& layer = mat.Layer(slot);

            ShaderResourceBindingDesc desc;
            desc.semantic = ToShaderResourceSemantic(slot);
            desc.bindingIndex = BindingIndexForSemantic(desc.semantic);
            desc.texture = layer.texture;
            desc.uvSet = (layer.uvSet == MaterialTextureUVSet::Auto)
                ? DefaultUVSetForSemantic(desc.semantic)
                : layer.uvSet;
            desc.enabled = layer.enabled;
            desc.expectsSRGB = layer.expectsSRGB;
            desc.requiredState = ResourceState::ShaderRead;
            set.AddTextureBinding(desc);
        }

        return set;
    }
}

void RenderGatherSystem::Gather(
    Registry&                                      registry,
    const FrameData&                               frame,
    ResourceStore<MeshAssetResource, MeshTag>&     meshStore,
    ResourceStore<MaterialResource,  MaterialTag>& matStore,
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

            const uint32_t shaderSortID = shader.Index() & 0x3FFFu;
            const uint32_t materialSortID = mat->sortID;
            const ResourceBindingSet bindings = BuildResourceBindingSet(*mat);

            if (options && options->skipSelfReferentialDraws &&
                UsesTextureAsShaderResource(bindings, options->forbiddenShaderReadTexture))
            {
                Debug::LogWarning("RenderGatherSystem: skipped self-referential RTT draw for entity ", entity.value);
                return;
            }

            RenderQueue& targetQueue = transparent ? outTransparentQueue : outOpaqueQueue;
            targetQueue.Submit(mr.mesh, matr.material, shader,
                               mr.submeshIndex, entity, wt.matrix,
                               pass, shaderSortID, materialSortID, depth,
                               vis.receiveShadows,
                               &bindings);
        });

    outOpaqueQueue.Sort();
    outTransparentQueue.Sort();
}

void RenderGatherSystem::GatherShadow(
    Registry&                                      registry,
    const FrameData&                               frame,
    ResourceStore<MeshAssetResource, MeshTag>&     meshStore,
    ResourceStore<MaterialResource,  MaterialTag>& matStore,
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

            const ResourceBindingSet bindings = BuildResourceBindingSet(*mat);
            if (options && options->skipSelfReferentialDraws &&
                UsesTextureAsShaderResource(bindings, options->forbiddenShaderReadTexture))
            {
                Debug::LogWarning("RenderGatherSystem: skipped self-referential RTT shadow draw for entity ", entity.value);
                return;
            }

            outShadowQueue.Submit(mr.mesh, matr.material, shader,
                                  mr.submeshIndex, entity, wt.matrix,
                                  RenderPass::Shadow,
                                  shader.Index() & 0x3FFFu,
                                  0u, 0.0f,
                                  true,
                                  &bindings);
        });

    outShadowQueue.Sort();
}
