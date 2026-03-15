#include "GDXEngine.h"
#include "GDXEventQueue.h"
#include "WindowDesc.h"
#include "GDXWin32Window.h"
#include "GDXWin32DX11ContextFactory.h"
#include "GDXDX11RenderBackend.h"
#include "GDXECSRenderer.h"

#include "Components.h"
#include "MeshAssetResource.h"
#include "MaterialResource.h"

#include <memory>
#include <cmath>

static constexpr uint32_t LAYER_DEFAULT = 1u << 0;

static void SetLookAt(
    TransformComponent& tc,
    const DirectX::XMVECTOR& position,
    const DirectX::XMVECTOR& target,
    const DirectX::XMVECTOR& upVec = DirectX::XMVectorSet(0, 0, 1, 0))
{
    using namespace DirectX;

    XMVECTOR forward = XMVector3Normalize(XMVectorSubtract(target, position));
    XMVECTOR up = upVec;

    float dot = fabsf(XMVectorGetX(XMVector3Dot(forward, up)));
    if (dot > 0.9999f)
    {
        up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
        dot = fabsf(XMVectorGetX(XMVector3Dot(forward, up)));
        if (dot > 0.9999f)
            up = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
    }

    XMVECTOR right = XMVector3Normalize(XMVector3Cross(up, forward));
    up = XMVector3Cross(forward, right);

    DirectX::XMMATRIX rotMatrix;
    rotMatrix.r[0] = XMVectorSetW(right, 0.0f);
    rotMatrix.r[1] = XMVectorSetW(up, 0.0f);
    rotMatrix.r[2] = XMVectorSetW(forward, 0.0f);
    rotMatrix.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);

    XMStoreFloat4(&tc.localRotation,
        XMQuaternionNormalize(XMQuaternionRotationMatrix(rotMatrix)));

    XMStoreFloat3(&tc.localPosition, position);
    tc.dirty = true;
}

int main()
{
    GDXEventQueue events;

    WindowDesc desc;
    desc.width = 1280;
    desc.height = 720;
    desc.title = "GIDX - Simple White Cube Scene";
    desc.resizable = true;
    desc.borderless = false;

    auto window = std::make_unique<GDXWin32Window>(desc, events);
    if (!window->Create())
        return 1;

    auto adapters = GDXWin32DX11ContextFactory::EnumerateAdapters();
    if (adapters.empty())
        return 1;

    GDXWin32DX11ContextFactory dx11Factory;
    auto dxgiContext = dx11Factory.Create(
        *window,
        GDXWin32DX11ContextFactory::FindBestAdapter(adapters));
    if (!dxgiContext)
        return 1;

    auto backend = std::make_unique<GDXDX11RenderBackend>(std::move(dxgiContext));
    auto rendererOwned = std::make_unique<GDXECSRenderer>(std::move(backend));
    GDXECSRenderer* renderer = rendererOwned.get();

    renderer->SetClearColor(0.08f, 0.08f, 0.10f);
    renderer->SetSceneAmbient(0.18f, 0.18f, 0.18f);

    GDXEngine engine(std::move(window), std::move(rendererOwned), events);
    if (!engine.Initialize())
        return 1;

    Registry& reg = renderer->GetRegistry();

    MeshAssetResource cubeAsset;
    cubeAsset.debugName = "Cube";
    cubeAsset.AddSubmesh(BuiltinMeshes::Cube());
    MeshHandle cubeMesh = renderer->UploadMesh(std::move(cubeAsset));

    MaterialResource whiteMatRes;
    whiteMatRes.data.baseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    MaterialHandle whiteMaterial = renderer->CreateMaterial(std::move(whiteMatRes));

    // Cube at 0,0,0
    {
        EntityID cube = reg.CreateEntity();
        reg.Add<TagComponent>(cube, "WhiteCube");

        TransformComponent tc;
        tc.localPosition = { 0.0f, 0.0f, 0.0f };
        tc.localScale = { 4.0f, 1.0f, 4.0f };
        tc.SetEulerDeg(0.0f, 0.0f, 0.0f);
        reg.Add<TransformComponent>(cube, tc);

        reg.Add<WorldTransformComponent>(cube);
        reg.Add<MeshRefComponent>(cube, cubeMesh, 0u);
        reg.Add<MaterialRefComponent>(cube, whiteMaterial);

        VisibilityComponent vc;
        vc.visible = true;
        vc.active = true;
        vc.layerMask = LAYER_DEFAULT;
        vc.castShadows = true;
        reg.Add<VisibilityComponent>(cube, vc);
    }

    // kleiner Würfel bei -4,2,-4 ohne Schatten
    //{
    //    EntityID cubeSmall = reg.CreateEntity();
    //    reg.Add<TagComponent>(cubeSmall, "SmallCube");
    //
    //    TransformComponent tc;
    //    tc.localPosition = { -2.0f, 2.0f, -2.0f };
    //    tc.localScale = { 1.0f, 1.0f, 1.0f };
    //    tc.SetEulerDeg(0.0f, 0.0f, 0.0f);
    //    reg.Add<TransformComponent>(cubeSmall, tc);
    //
    //    reg.Add<WorldTransformComponent>(cubeSmall);
    //    reg.Add<MeshRefComponent>(cubeSmall, cubeMesh, 0u);
    //    reg.Add<MaterialRefComponent>(cubeSmall, whiteMaterial);
    //
    //    VisibilityComponent vc;
    //    vc.visible = true;
    //    vc.active = true;
    //    vc.layerMask = LAYER_DEFAULT;
    //    vc.castShadows = false;
    //    reg.Add<VisibilityComponent>(cubeSmall, vc);
    //}

    // Camera at 0,10,0 looking at scene origin
    {
        EntityID camera = reg.CreateEntity();
        reg.Add<TagComponent>(camera, "MainCamera");

        TransformComponent tc;
        reg.Add<TransformComponent>(camera, tc);
        reg.Add<WorldTransformComponent>(camera);
        {
            CameraComponent cam;
            cam.fovDeg = 60.0f;
            cam.nearPlane = 0.1f;
            cam.farPlane = 1000.0f;
            cam.aspectRatio = 1280.0f / 720.0f;
            cam.cullMask = LAYER_DEFAULT;
            reg.Add<CameraComponent>(camera, cam);
            reg.Add<ActiveCameraTag>(camera);
        }
        {
            TransformComponent tc;
            tc.localPosition = { 0.0f, 10.0f, 0.0f };
            tc.SetEulerDeg(90.0f, 0.0f, 0.0f);
            reg.Add<TransformComponent>(camera, tc);
            reg.Add<WorldTransformComponent>(camera);
        }
    }

    // Directional light, shadows disabled
    {
        EntityID light = reg.CreateEntity();
        reg.Add<TagComponent>(light, "DirectionalLight");

        {
            LightComponent lc;
            lc.kind = LightKind::Directional;
            lc.diffuseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
            lc.intensity = 1.5f;
            lc.castShadows = false;
            reg.Add<LightComponent>(light, lc);
        }
        {
            TransformComponent tc;
            tc.localPosition = { 0.0f, 0.0f, 0.0f };
            tc.SetEulerDeg(90.0f, 0.0f, 0.0f);
            reg.Add<TransformComponent>(light, tc);
            reg.Add<WorldTransformComponent>(light);
        }
    }

    engine.Run();
    engine.Shutdown();
    return 0;
}