#include "GDXEngine.h"
#include "GDXEventQueue.h"
#include "WindowDesc.h"
#include "GDXWin32Window.h"
#include "GDXWin32DX11ContextFactory.h"
#include "Debug.h"

#include "GDXECSRenderer.h"
#include "GDXDX11RenderBackend.h"

#include "Components.h"
#include "MeshAssetResource.h"
#include "MaterialResource.h"
#include "PostProcessResource.h"

#include <memory>

struct TintParams
{
    float tint[4] = { 1.0f, 0.7f, 0.7f, 1.0f };
};

class PostProcessExample
{
public:
    explicit PostProcessExample(GDXECSRenderer& renderer)
        : m_renderer(renderer)
    {
    }

    void Init()
    {
        Registry& reg = m_renderer.GetRegistry();

        MeshAssetResource asset;
        asset.debugName = "Cube";
        asset.AddSubmesh(BuiltinMeshes::Cube());
        m_cubeMesh = m_renderer.UploadMesh(std::move(asset));

        MaterialResource mat = MaterialResource::FlatColor(1.0f, 1.0f, 1.0f, 1.0f);
        m_cubeMat = m_renderer.CreateMaterial(mat);

        EntityID cube = reg.CreateEntity();
        reg.Add<TagComponent>(cube, "Cube");
        reg.Add<TransformComponent>(cube);
        reg.Add<WorldTransformComponent>(cube);
        reg.Add<RenderableComponent>(cube, m_cubeMesh, m_cubeMat, 0u);
        reg.Add<VisibilityComponent>(cube);

        if (auto* tc = reg.Get<TransformComponent>(cube))
            tc->localPosition = { 0.0f, 0.0f, 4.0f };

        EntityID cam = reg.CreateEntity();
        reg.Add<TagComponent>(cam, "Camera");
        reg.Add<TransformComponent>(cam);
        reg.Add<WorldTransformComponent>(cam);
        CameraComponent cc{};
        cc.fovDeg = 60.0f;
        cc.aspectRatio = 16.0f / 9.0f;
        cc.nearPlane = 0.1f;
        cc.farPlane = 100.0f;
        reg.Add<CameraComponent>(cam, cc);

        PostProcessPassDesc ppDesc{};
        ppDesc.vertexShaderFile = L"PostProcessFullscreenVS.hlsl";
        ppDesc.pixelShaderFile = L"PostProcessTintPS.hlsl";
        ppDesc.debugName = L"Tint PostProcess";
        ppDesc.constantBufferBytes = sizeof(TintParams);

        m_tintPass = m_renderer.CreatePostProcessPass(ppDesc);
        m_renderer.SetPostProcessConstants(m_tintPass, &m_tintParams, sizeof(m_tintParams));
    }

    void Update(float dt)
    {
        (void)dt;
    }

private:
    GDXECSRenderer& m_renderer;
    MeshHandle m_cubeMesh = MeshHandle::Invalid();
    MaterialHandle m_cubeMat = MaterialHandle::Invalid();
    PostProcessHandle m_tintPass = PostProcessHandle::Invalid();
    TintParams m_tintParams{};
};
