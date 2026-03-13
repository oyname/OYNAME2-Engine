#include "GDXEngine.h"
#include "GDXEventQueue.h"
#include "WindowDesc.h"
#include "GDXWin32Window.h"
#include "GDXWin32DX11ContextFactory.h"
#include "Debug.h"

#include "GDXECSRenderer.h"
#include "GDXDX11RenderBackend.h"

#include "Components.h"
#include "SubmeshBuilder.h"
#include "MeshProcessor.h"
#include "MeshAssetResource.h"
#include "MaterialResource.h"

#include <DirectXMath.h>
#include <memory>

using namespace DirectX;

// -----------------------------------------------------------------------------
// Würfel mit UV0 + UV1
// -----------------------------------------------------------------------------
static SubmeshData BuildCubeUV0UV1(float size = 1.0f, float detailTile = 4.0f)
{
    using namespace DirectX;

    SubmeshBuilder b;
    const float h = size * 0.5f;

    auto addFace = [&](XMFLOAT3 p0,
        XMFLOAT3 p1,
        XMFLOAT3 p2,
        XMFLOAT3 p3,
        XMFLOAT3 n)
        {
            const uint32_t i0 = b.AddVertex(p0);
            const uint32_t i1 = b.AddVertex(p1);
            const uint32_t i2 = b.AddVertex(p2);
            const uint32_t i3 = b.AddVertex(p3);

            b.SetNormal(i0, n);
            b.SetNormal(i1, n);
            b.SetNormal(i2, n);
            b.SetNormal(i3, n);

            b.SetUV0(i0, XMFLOAT2(0.0f, 0.0f));
            b.SetUV0(i1, XMFLOAT2(1.0f, 0.0f));
            b.SetUV0(i2, XMFLOAT2(1.0f, 1.0f));
            b.SetUV0(i3, XMFLOAT2(0.0f, 1.0f));

            b.SetUV1(i0, XMFLOAT2(0.0f, 0.0f));
            b.SetUV1(i1, XMFLOAT2(detailTile, 0.0f));
            b.SetUV1(i2, XMFLOAT2(detailTile, detailTile));
            b.SetUV1(i3, XMFLOAT2(0.0f, detailTile));

            b.SetColor(i0, XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f));
            b.SetColor(i1, XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f));
            b.SetColor(i2, XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f));
            b.SetColor(i3, XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f));

            b.AddTriangle(i0, i1, i2);
            b.AddTriangle(i0, i2, i3);
        };

    addFace({ -h, -h, -h }, { h, -h, -h }, { h,  h, -h }, { -h,  h, -h }, { 0.0f,  0.0f, -1.0f });
    addFace({ h, -h,  h }, { -h, -h,  h }, { -h,  h,  h }, { h,  h,  h }, { 0.0f,  0.0f,  1.0f });
    addFace({ -h, -h,  h }, { -h, -h, -h }, { -h,  h, -h }, { -h,  h,  h }, { -1.0f,  0.0f,  0.0f });
    addFace({ h, -h, -h }, { h, -h,  h }, { h,  h,  h }, { h,  h, -h }, { 1.0f,  0.0f,  0.0f });
    addFace({ -h,  h, -h }, { h,  h, -h }, { h,  h,  h }, { -h,  h,  h }, { 0.0f,  1.0f,  0.0f });
    addFace({ -h, -h,  h }, { h, -h,  h }, { h, -h, -h }, { -h, -h, -h }, { 0.0f, -1.0f,  0.0f });

    SubmeshData s = b.MoveBuild();

    MeshBuildSettings build;
    build.validateInput = true;
    build.removeDegenerateTriangles = true;
    build.computeNormalsIfMissing = false;
    build.computeTangentsIfPossible = true;

    MeshProcessor::Process(s, build);
    return s;
}

int main()
{
    GDXEventQueue events;

    WindowDesc desc;
    desc.width = 1280;
    desc.height = 720;
    desc.title = "GIDX - Builder Cube UV0 UV1";
    desc.resizable = true;

    auto windowOwned = std::make_unique<GDXWin32Window>(desc, events);
    if (!windowOwned->Create())
    {
        Debug::LogError("Fenster konnte nicht erstellt werden");
        return 1;
    }

    GDXWin32Window* windowRaw = windowOwned.get();

    auto adapters = GDXWin32DX11ContextFactory::EnumerateAdapters();
    if (adapters.empty())
    {
        Debug::LogError("Kein DX11 Adapter gefunden");
        return 2;
    }

    const unsigned int adapterIdx = GDXWin32DX11ContextFactory::FindBestAdapter(adapters);

    GDXWin32DX11ContextFactory dx11Factory;
    auto dxContext = dx11Factory.Create(*windowRaw, adapterIdx);
    if (!dxContext)
    {
        Debug::LogError("DX11 Context konnte nicht erstellt werden");
        return 3;
    }

    auto backendOwned = std::make_unique<GDXDX11RenderBackend>(std::move(dxContext));
    auto rendererOwned = std::make_unique<GDXECSRenderer>(std::move(backendOwned));
    GDXECSRenderer* renderer = rendererOwned.get();

    renderer->SetClearColor(0.04f, 0.04f, 0.10f);

    GDXEngine engine(std::move(windowOwned), std::move(rendererOwned), events);

    if (!engine.Initialize())
    {
        Debug::LogError("Engine Initialize fehlgeschlagen");
        return 4;
    }

    Registry& reg = renderer->GetRegistry();

    TextureHandle hAlbedo = renderer->LoadTexture(L"..//media/albedo.png");
    TextureHandle hNormal = renderer->LoadTexture(L"..//media/normal.png", false);
    TextureHandle hORM = renderer->LoadTexture(L"..//media/orm.png", false);

    MeshHandle hCube;
    {
        MeshAssetResource asset;
        asset.debugName = "BuilderCube_UV0_UV1";
        asset.AddSubmesh(BuildCubeUV0UV1(1.5f, 4.0f));
        hCube = renderer->UploadMesh(std::move(asset));
    }

    MaterialHandle hMat;
    {
        MaterialResource mat;
        mat.albedoTex = hAlbedo;
        mat.normalTex = hNormal;
        mat.ormTex = hORM;

        mat.data.baseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
        mat.data.specularColor = { 0.5f, 0.5f, 0.5f, 1.0f };
        mat.data.emissiveColor = { 0.0f, 0.0f, 0.0f, 1.0f };
        mat.data.uvTilingOffset = { 1.0f, 1.0f, 0.0f, 0.0f };
        mat.data.uvDetailTilingOffset = { 1.0f, 1.0f, 0.0f, 0.0f };

        mat.data.metallic = 0.0f;
        mat.data.roughness = 0.65f;
        mat.data.normalScale = 1.0f;
        mat.data.occlusionStrength = 1.0f;

        mat.data.shininess = 32.0f;
        mat.data.transparency = 0.0f;
        mat.data.alphaCutoff = 0.5f;
        mat.data.receiveShadows = 1.0f;

        mat.data.blendMode = 0.0f;
        mat.data.blendFactor = 0.0f;

        mat.data.flags = MF_USE_NORMAL_MAP | MF_USE_ORM_MAP | MF_SHADING_PBR;

        hMat = renderer->CreateMaterial(std::move(mat));
    }

    EntityID cube = reg.CreateEntity();
    reg.Add<TagComponent>(cube, "Cube");

    {
        TransformComponent tc;
        tc.localPosition = { 0.0f, 0.0f, 3.0f };
        tc.localScale = { 1.0f, 1.0f, 1.0f };
        reg.Add<TransformComponent>(cube, tc);
    }

    reg.Add<WorldTransformComponent>(cube);
    reg.Add<MeshRefComponent>(cube, hCube, 0u);
    reg.Add<MaterialRefComponent>(cube, hMat);
    reg.Add<VisibilityComponent>(cube);
    reg.Add<ShadowCasterTag>(cube);

    EntityID camera = reg.CreateEntity();
    reg.Add<TagComponent>(camera, "MainCamera");

    {
        TransformComponent tc;
        tc.localPosition = { 0.0f, 1.5f, -3.5f };
        tc.SetEulerDeg(12.0f, 0.0f, 0.0f);
        reg.Add<TransformComponent>(camera, tc);
    }

    reg.Add<WorldTransformComponent>(camera);

    {
        CameraComponent cam;
        cam.fovDeg = 60.0f;
        cam.nearPlane = 0.1f;
        cam.farPlane = 500.0f;
        cam.aspectRatio = 1280.0f / 720.0f;
        reg.Add<CameraComponent>(camera, cam);
    }

    reg.Add<ActiveCameraTag>(camera);

    EntityID sun = reg.CreateEntity();
    reg.Add<TagComponent>(sun, "Sun");

    {
        LightComponent lc;
        lc.kind = LightKind::Directional;
        lc.diffuseColor = { 1.0f, 0.95f, 0.90f, 1.0f };
        lc.intensity = 2.0f;
        lc.castShadows = true;
        lc.shadowOrthoSize = 20.0f;
        lc.shadowNear = 0.1f;
        lc.shadowFar = 100.0f;
        reg.Add<LightComponent>(sun, lc);
    }

    {
        TransformComponent tc;
        tc.localPosition = { -5.0f, 6.0f, -5.0f };
        tc.SetEulerDeg(35.0f, -45.0f, 0.0f);
        reg.Add<TransformComponent>(sun, tc);
    }

    reg.Add<WorldTransformComponent>(sun);

    renderer->SetSceneAmbient(0.30f, 0.30f, 0.36f);

    float cubeYaw = 0.0f;
    float cubePitch = 0.0f;

    renderer->SetTickCallback([&](float dt)
        {
            if (auto* tc = reg.Get<TransformComponent>(cube))
            {
                cubeYaw += 45.0f * dt;
                cubePitch += 20.0f * dt;

                if (cubeYaw >= 360.0f) cubeYaw -= 360.0f;
                if (cubePitch >= 360.0f) cubePitch -= 360.0f;

                tc->SetEulerDeg(cubePitch, cubeYaw, 0.0f);
            }
        });

    engine.Run();
    engine.Shutdown();
    return 0;
}