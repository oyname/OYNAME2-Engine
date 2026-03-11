#include "RenderGatherSystem.h"

void RenderGatherSystem::Gather(
    Registry&                                      registry,
    const FrameData&                               frame,
    ResourceStore<MeshAssetResource, MeshTag>&    meshStore,
    ResourceStore<MaterialResource,  MaterialTag>& matStore,
    ShaderHandle                                   defaultShader,
    RenderQueue&                                   outQueue) const
{
    outQueue.Clear();

    registry.View<WorldTransformComponent,
                  MeshRefComponent,
                  MaterialRefComponent,
                  VisibilityComponent>(
        [&](EntityID,
            WorldTransformComponent& wt,
            MeshRefComponent&        mr,
            MaterialRefComponent&    matr,
            VisibilityComponent&     vis)
        {
            if (!vis.visible || !vis.active) return;
            if (!mr.enabled)                 return;
            if (!mr.mesh.IsValid())          return;
            if (!matr.material.IsValid())    return;

            const MeshAssetResource* mesh = meshStore.Get(mr.mesh);
            const MaterialResource*  mat  = matStore.Get(matr.material);

            if (!mesh || !mat)                         return;
            if (!mesh->IsGpuReadyAt(mr.submeshIndex))  return;

            // Shader aus Material — fällt auf defaultShader zurück wenn keiner gesetzt.
            const ShaderHandle shader = mat->shader.IsValid()
                                      ? mat->shader
                                      : defaultShader;

            if (!shader.IsValid()) return;

            const bool       transparent = mat->IsTransparent();
            const RenderPass pass        = transparent
                                         ? RenderPass::Transparent
                                         : RenderPass::Opaque;

            float depth = CameraSystem::ComputeNDCDepth(wt.matrix, frame.viewProjMatrix);
            if (!transparent) depth = 1.0f - depth;

            const uint32_t shaderSortID   = shader.Index() & 0x3FFFu;
            const uint32_t materialSortID = mat->sortID;

            outQueue.Submit(mr.mesh, matr.material, shader,
                            mr.submeshIndex, wt.matrix,
                            pass, shaderSortID, materialSortID, depth);
        });

    outQueue.Sort();
}

void RenderGatherSystem::GatherShadow(
    Registry&                                      registry,
    const FrameData&                               frame,
    ResourceStore<MeshAssetResource, MeshTag>&    meshStore,
    MaterialHandle                                 shadowMaterial,
    ShaderHandle                                   shadowShader,
    RenderQueue&                                   outShadowQueue) const
{
    outShadowQueue.Clear();

    registry.View<WorldTransformComponent,
                  MeshRefComponent,
                  VisibilityComponent,
                  ShadowCasterTag>(
        [&](EntityID,
            WorldTransformComponent& wt,
            MeshRefComponent&        mr,
            VisibilityComponent&     vis,
            ShadowCasterTag&)
        {
            if (!vis.visible || !vis.active || !vis.castShadows) return;
            if (!mr.enabled || !mr.mesh.IsValid())               return;

            const MeshAssetResource* mesh = meshStore.Get(mr.mesh);
            if (!mesh || !mesh->IsGpuReadyAt(mr.submeshIndex))   return;

            outShadowQueue.Submit(mr.mesh, shadowMaterial, shadowShader,
                                  mr.submeshIndex, wt.matrix,
                                  RenderPass::Shadow,
                                  shadowShader.Index() & 0x3FFFu,
                                  0u, 0.0f);
        });

    outShadowQueue.Sort();
}
