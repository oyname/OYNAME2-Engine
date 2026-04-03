#pragma once

#include "ResourceStore.h"
#include "MeshAssetResource.h"
#include "MaterialResource.h"
#include "GDXShaderResource.h"
#include "GDXTextureResource.h"
#include "GDXRenderTargetResource.h"
#include "PostProcessResource.h"
#include "ECS/Registry.h"

struct RenderBackendSceneContext
{
    Registry* registry = nullptr;
    ResourceStore<MeshAssetResource, MeshTag>* meshStore = nullptr;
    ResourceStore<MaterialResource, MaterialTag>* matStore = nullptr;
    ResourceStore<GDXShaderResource, ShaderTag>* shaderStore = nullptr;
    ResourceStore<GDXTextureResource, TextureTag>* texStore = nullptr;
    ResourceStore<GDXRenderTargetResource, RenderTargetTag>* rtStore = nullptr;
    ResourceStore<PostProcessResource, PostProcessTag>* postProcessStore = nullptr;

    bool IsValid() const noexcept
    {
        return registry && meshStore && matStore && shaderStore && texStore && rtStore;
    }
};
