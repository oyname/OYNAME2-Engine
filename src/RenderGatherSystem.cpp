#include "RenderGatherSystem.h"

void RenderGatherSystem::Gather(
    Registry&                                      registry,
    const FrameData&                               frame,
    ResourceStore<MeshAssetResource, MeshTag>&     meshStore,
    ResourceStore<MaterialResource,  MaterialTag>& matStore,
    const ShaderResolver&                          resolveShader,
    RenderQueue&                                   outQueue) const
{
    outQueue.Clear();

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
            if (!mr.enabled)                 return;
            if (!mr.mesh.IsValid())          return;
            if (!matr.material.IsValid())    return;

            const MeshAssetResource* mesh = meshStore.Get(mr.mesh);
            const MaterialResource*  mat  = matStore.Get(matr.material);

            if (!mesh || !mat)                         return;
            if (mr.submeshIndex >= mesh->submeshes.size()) return;
            if (!mesh->IsGpuReadyAt(mr.submeshIndex))  return;

            const SubmeshData& submesh = mesh->submeshes[mr.submeshIndex];
            const bool       transparent = mat->IsTransparent();
            const RenderPass pass        = transparent
                                         ? RenderPass::Transparent
                                         : RenderPass::Opaque;

            const ShaderHandle shader = resolveShader(pass, submesh, *mat);
            if (!shader.IsValid()) return;

            float depth = CameraSystem::ComputeNDCDepth(wt.matrix, frame.viewProjMatrix);
            if (!transparent) depth = 1.0f - depth;

            const uint32_t shaderSortID   = shader.Index() & 0x3FFFu;
            const uint32_t materialSortID = mat->sortID;

            outQueue.Submit(mr.mesh, matr.material, shader,
                            mr.submeshIndex, entity, wt.matrix,
                            pass, shaderSortID, materialSortID, depth);
        });

    outQueue.Sort();
}

void RenderGatherSystem::GatherShadow(
    Registry&                                      registry,
    const FrameData&                               frame,
    ResourceStore<MeshAssetResource, MeshTag>&     meshStore,
    ResourceStore<MaterialResource,  MaterialTag>& matStore,
    const ShaderResolver&                          resolveShader,
    RenderQueue&                                   outShadowQueue) const
{
    outShadowQueue.Clear();

    registry.View<WorldTransformComponent,
                  MeshRefComponent,
                  MaterialRefComponent,
                  VisibilityComponent,
                  ShadowCasterTag>(
        [&](EntityID entity,
            WorldTransformComponent& wt,
            MeshRefComponent&        mr,
            MaterialRefComponent&    matr,
            VisibilityComponent&     vis,
            ShadowCasterTag&)
        {
            if (!vis.visible || !vis.active || !vis.castShadows) return;
            if ((vis.layerMask & frame.cullMask) == 0u) return;
            if (!mr.enabled || !mr.mesh.IsValid())               return;
            if (!matr.material.IsValid())                        return;

            const MeshAssetResource* mesh = meshStore.Get(mr.mesh);
            const MaterialResource*  mat  = matStore.Get(matr.material);
            if (!mesh || !mat) return;
            if (mr.submeshIndex >= mesh->submeshes.size()) return;
            if (!mesh->IsGpuReadyAt(mr.submeshIndex))   return;

            const SubmeshData& submesh = mesh->submeshes[mr.submeshIndex];
            const ShaderHandle shader = resolveShader(RenderPass::Shadow, submesh, *mat);
            if (!shader.IsValid()) return;

            outShadowQueue.Submit(mr.mesh, matr.material, shader,
                                  mr.submeshIndex, entity, wt.matrix,
                                  RenderPass::Shadow,
                                  shader.Index() & 0x3FFFu,
                                  0u, 0.0f);
        });

    outShadowQueue.Sort();
}
