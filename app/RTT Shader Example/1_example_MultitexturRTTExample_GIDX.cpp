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
#include "GDXTextureSlots.h"
#include "GDXVertexFlags.h"
#include "GDXShaderResource.h"
#include "NeonTimeBuffer.h"
#include "Debug.h"

#include <memory>
#include <cmath>

static constexpr uint32_t LAYER_DEFAULT = 1u << 0;
static constexpr uint32_t LAYER_FX      = 1u << 1;

static void SetLookAt(
    TransformComponent& tc,
    const Float3& position,
    const Float3& target,
    const Float3& upVec = Float3{ 0.0f, 1.0f, 0.0f })
{
    Float3 forward = GIDX::Normalize3(GIDX::Subtract(target, position), { 0.0f, 0.0f, 1.0f });
    Float3 up = GIDX::Normalize3(upVec, { 0.0f, 1.0f, 0.0f });

    float dot = std::fabs(GIDX::Dot3(forward, up));
    if (dot > 0.9999f)
    {
        up = { 0.0f, 0.0f, 1.0f };
        dot = std::fabs(GIDX::Dot3(forward, up));
        if (dot > 0.9999f)
            up = { 0.0f, 1.0f, 0.0f };
    }

    Float3 right = GIDX::Normalize3(GIDX::Cross(up, forward), { 1.0f, 0.0f, 0.0f });
    up = GIDX::Cross(forward, right);

    tc.localRotation = GIDX::QuaternionFromBasis(right, up, forward);
    tc.localPosition = position;
    tc.dirty = true;
}

static EntityID CreateCubeEntity(
    Registry& reg,
    MeshHandle mesh,
    MaterialHandle mat,
    const char* tag,
    float px, float py, float pz,
    float sx = 1.0f, float sy = 1.0f, float sz = 1.0f,
    float pitchDeg = 0.0f, float yawDeg = 0.0f, float rollDeg = 0.0f)
{
    EntityID e = reg.CreateEntity();
    reg.Add<TagComponent>(e, tag);

    TransformComponent tc;
    tc.localPosition = { px, py, pz };
    tc.localScale = { sx, sy, sz };
    tc.SetEulerDeg(pitchDeg, yawDeg, rollDeg);
    reg.Add<TransformComponent>(e, tc);

    reg.Add<WorldTransformComponent>(e);
    reg.Add<RenderableComponent>(e, mesh, mat, 0u);

    VisibilityComponent vc;
    vc.visible = true;
    vc.active = true;
    vc.layerMask = LAYER_DEFAULT;
    vc.castShadows = true;
    reg.Add<VisibilityComponent>(e, vc);


    return e;
}

int main()
{
    GDXEventQueue events;

    WindowDesc desc;
    desc.width = 1200;
    desc.height = 650;
    desc.title = "GIDX - RTT + Neon";
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

    renderer->SetClearColor(0.0f, 0.25f, 0.5f);
    renderer->SetSceneAmbient(0.22f, 0.2f, 0.26f);

    GDXEngine engine(std::move(window), std::move(rendererOwned), events);
    if (!engine.Initialize())
        return 1;

    Registry& reg = renderer->GetRegistry();

    MeshAssetResource cubeAsset;
    cubeAsset.debugName = "Cube";
    cubeAsset.AddSubmesh(BuiltinMeshes::Cube());
    MeshHandle cubeMesh = renderer->UploadMesh(std::move(cubeAsset));

    ShaderHandle neonShader = renderer->CreateShader(
        L"VertexShaderNeon_GIDX.hlsl",
        L"PixelShaderNeon_GIDX.hlsl",
        GDX_VERTEX_POSITION);

    RenderTargetHandle rtt = renderer->CreateRenderTarget(512, 512, L"NeonRTT");
    TextureHandle rttTex = renderer->GetRenderTargetTexture(rtt);

    MaterialResource neonMatRes;
    neonMatRes.shader = neonShader;
    neonMatRes.SetFlag(MF_UNLIT, true);
    neonMatRes.data.baseColor = { 1, 1, 1, 1 };
    MaterialHandle neonMaterial = renderer->CreateMaterial(std::move(neonMatRes));

    NeonTimeBuffer neonTime;
    neonTime.Initialize(*renderer, neonMaterial);

    MaterialResource rttMatRes;
    rttMatRes.SetTexture(MaterialTextureSlot::Albedo, rttTex, MaterialTextureUVSet::UV0);
    MaterialHandle materialRTT = renderer->CreateMaterial(std::move(rttMatRes));

    TextureHandle faceTex = renderer->LoadTexture(L"..\\media\\engine.png", true);
    MaterialResource faceMatRes;
    faceMatRes.SetTexture(MaterialTextureSlot::Albedo, faceTex, MaterialTextureUVSet::UV0);
    MaterialHandle materialFace = renderer->CreateMaterial(std::move(faceMatRes));

    const Float3 sharedPos    = { 15.0f, 0.0f, 10.0f };
    const Float3 sharedTarget = { -20.0f, 0.0f, 0.0f };
    //-10.0f, 0.0f, 13.0f

    EntityID camera = reg.CreateEntity();
    reg.Add<TagComponent>(camera, "MainCamera");
    {
        TransformComponent tc;
        reg.Add<TransformComponent>(camera, tc);
    }
    reg.Add<WorldTransformComponent>(camera);

    CameraComponent mainCam;
    mainCam.fovDeg = 60.0f;
    mainCam.nearPlane = 0.1f;
    mainCam.farPlane = 1000.0f;
    mainCam.aspectRatio = 1200.0f / 650.0f;
    reg.Add<CameraComponent>(camera, mainCam);
    reg.Add<ActiveCameraTag>(camera);

    if (auto* tc = reg.Get<TransformComponent>(camera))
        SetLookAt(*tc, sharedPos, sharedTarget);

    EntityID rttCamera = reg.CreateEntity();
    reg.Add<TagComponent>(rttCamera, "RTTCamera");
    {
        TransformComponent tc;
        reg.Add<TransformComponent>(rttCamera, tc);
    }
    reg.Add<WorldTransformComponent>(rttCamera);

    CameraComponent rttCam;
    rttCam.fovDeg = 60.0f;
    rttCam.nearPlane = 0.1f;
    rttCam.farPlane = 1000.0f;
    rttCam.aspectRatio = 1.0f;
    rttCam.cullMask = LAYER_DEFAULT;
    reg.Add<CameraComponent>(rttCamera, rttCam);

    RenderTargetCameraComponent rttCamComp(rtt);
    rttCamComp.enabled = true;
    rttCamComp.autoAspectFromTarget = true;
    rttCamComp.renderOpaque = true;
    rttCamComp.renderTransparent = true;
    rttCamComp.renderShadows = true;
    rttCamComp.skipSelfReferentialDraws = true;
    rttCamComp.clear.clearColorEnabled = true;
    rttCamComp.clear.clearColor[0] = 0.0f;
    rttCamComp.clear.clearColor[1] = 0.25f;
    rttCamComp.clear.clearColor[2] = 0.5f;
    rttCamComp.clear.clearColor[3] = 1.0f;
    reg.Add<RenderTargetCameraComponent>(rttCamera, rttCamComp);

    if (auto* tc = reg.Get<TransformComponent>(rttCamera))
    {
        SetLookAt(
            *tc,
            { 7.0f, 0.0f, 3.0f },
            { 2.0f, 0.0f, 0.0f });
    }

    EntityID mesh1 = CreateCubeEntity(
        reg, cubeMesh, materialRTT, "MeshRTT",
        -5.0f, 0.0f, 13.0f, 8.0f, 8.0f, 8.0f, 45.0f, 45.0f, 0.0f);

    EntityID mesh2 = CreateCubeEntity(
        reg, cubeMesh, neonMaterial, "MeshNeon",
        2.0f, 0.0f, 0.0f, 3.5f, 3.5f, 3.5f);

    EntityID mesh3 = CreateCubeEntity(
        reg, cubeMesh, materialFace, "MeshWall",
        -15.0f, 0.0f, 0.0f, 5.0f, 40.0f, 40.0f);

    if (auto* vc = reg.Get<VisibilityComponent>(mesh1))
        vc->layerMask = LAYER_FX;

    EntityID light = reg.CreateEntity();
    reg.Add<TagComponent>(light, "DirectionalLight");
    {
        LightComponent lc;
        lc.kind = LightKind::Directional;
        lc.diffuseColor = { 1.0f, 0.8f, 1.0f, 1.0f };
        lc.intensity = 1.0f;
        lc.castShadows = true;
        lc.shadowOrthoSize = 20.0f;
        lc.shadowNear = 0.1f;
        lc.shadowFar = 1000.0f;
        reg.Add<LightComponent>(light, lc);
    }
    {
        TransformComponent tc;
        tc.localPosition = { 0.0f, 0.0f, 0.0f };
        tc.SetEulerDeg(0.0f, -90.0f, 0.0f);
        reg.Add<TransformComponent>(light, tc);
    }
    reg.Add<WorldTransformComponent>(light);

    //if (auto* tc = reg.Get<TransformComponent>(light))
    //    SetLookAt(*tc, sharedPos, sharedTarget);

    renderer->SetTickCallback([&](float dt)
    {
        neonTime.Update(dt);        if (auto* tc = reg.Get<TransformComponent>(mesh2))
        {
            const Float4 delta = GIDX::QuaternionFromEulerDeg(50.0f * dt, 50.0f * dt, 0.0f);
            tc->localRotation = GIDX::QuaternionMultiply(tc->localRotation, delta);
            tc->dirty = true;
        }
    });

    engine.Run();
    neonTime.Shutdown();
    engine.Shutdown();
    return 0;
}
