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
#include "GDXRenderTargetResource.h"

#include <memory>
#include <filesystem>
#include <cmath>

// -----------------------------------------------------------------------------
// Layer-Bits für diese Demo
// -----------------------------------------------------------------------------
static constexpr uint32_t LAYER_RTT = 1u << 0;
static constexpr uint32_t LAYER_MAIN = 1u << 1;

static bool FileExists(const std::wstring& path)
{
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

class RTTDemo
{
public:
    explicit RTTDemo(GDXECSRenderer& renderer)
        : m_renderer(renderer)
    {
    }

    void Init()
    {
        Registry& reg = m_renderer.GetRegistry();

        // ================================================================
        // Mesh: Cube
        // ================================================================
        {
            MeshAssetResource asset;
            asset.debugName = "Cube";
            asset.AddSubmesh(BuiltinMeshes::Cube());
            m_hCube = m_renderer.UploadMesh(std::move(asset));
        }

        // ================================================================
        // Textur für linken Würfel
        // ================================================================
        TextureHandle hFaceTex = TextureHandle::Invalid();

        const std::wstring facePath = L"..//media//color1.png";
        if (FileExists(facePath))
            hFaceTex = m_renderer.LoadTexture(facePath, true);

        // ================================================================
        // Material links: normale Textur
        // ================================================================
        {
            MaterialResource mat;
            mat.data.baseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
            mat.data.receiveShadows = 1.0f;
            mat.data.flags = MF_NONE;

            if (hFaceTex.IsValid())
                mat.SetTexture(MaterialTextureSlot::Albedo, hFaceTex, MaterialTextureUVSet::UV1);

            m_hMatLeft = m_renderer.CreateMaterial(mat);
        }

        // ================================================================
        // RTT anlegen: 512x512
        // ================================================================
        m_hRTT = m_renderer.CreateRenderTarget(512, 512, L"RTT Demo Target");
        TextureHandle hRTTTexture = m_renderer.GetRenderTargetTexture(m_hRTT);

        // ================================================================
        // Material rechts: zeigt RTT-Textur
        // ================================================================
        {
            MaterialResource mat;
            mat.data.baseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
            mat.data.receiveShadows = 1.0f;
            mat.data.flags = MF_NONE;
            mat.SetTexture(MaterialTextureSlot::Albedo, hRTTTexture, MaterialTextureUVSet::UV1);

            m_hMatRight = m_renderer.CreateMaterial(mat);
        }

        // ================================================================
        // Hauptkamera
        // Sie sieht beide Layer.
        // ================================================================
        m_mainCamera = reg.CreateEntity();
        reg.Add<TagComponent>(m_mainCamera, "Main Camera");

        {
            TransformComponent tc;
            tc.localPosition = { 0.0f, 0.0f, -5.0f };
            reg.Add<TransformComponent>(m_mainCamera, tc);
        }
        reg.Add<WorldTransformComponent>(m_mainCamera);

        {
            CameraComponent cam;
            cam.fovDeg = 60.0f;
            cam.nearPlane = 0.1f;
            cam.farPlane = 200.0f;
            cam.aspectRatio = 16.0f / 9.0f;
            cam.cullMask = LAYER_RTT | LAYER_MAIN;
            reg.Add<CameraComponent>(m_mainCamera, cam);
        }
        reg.Add<ActiveCameraTag>(m_mainCamera);

        // ================================================================
        // RTT-Kamera
        // Sie sieht nur den RTT-Layer.
        // ================================================================
        m_rttCamera = reg.CreateEntity();
        reg.Add<TagComponent>(m_rttCamera, "RTT Camera");

        {
            TransformComponent tc;
            tc.localPosition = { -4.0f, 0.0f, 4.0f };
            tc.SetEulerDeg(0.0f, 0.0f, 0.0f);
            reg.Add<TransformComponent>(m_rttCamera, tc);
        }
        reg.Add<WorldTransformComponent>(m_rttCamera);

        {
            CameraComponent cam;
            cam.fovDeg = 35.0f;
            cam.nearPlane = 0.1f;
            cam.farPlane = 30.0f;
            cam.aspectRatio = 1.0f;
            cam.cullMask = LAYER_RTT;
            reg.Add<CameraComponent>(m_rttCamera, cam);
        }

        {
            RenderTargetCameraComponent rtc;
            rtc.target = m_hRTT;
            rtc.enabled = true;
            rtc.autoAspectFromTarget = true;

            rtc.clear.clearColorEnabled = true;
            rtc.clear.clearColor[0] = 0.6f;
            rtc.clear.clearColor[1] = 0.6f;
            rtc.clear.clearColor[2] = 0.8f;
            rtc.clear.clearColor[3] = 1.0f;

            rtc.clear.clearDepthEnabled = true;
            rtc.clear.clearDepthValue = 1.0f;

            rtc.clear.clearStencilEnabled = false;
            rtc.clear.clearStencilValue = 0;

            // optionaler Guard kann anbleiben
            rtc.skipSelfReferentialDraws = true;

            reg.Add<RenderTargetCameraComponent>(m_rttCamera, rtc);
        }

        // ================================================================
        // Directional Light 1
        // ================================================================
        m_light1 = reg.CreateEntity();
        reg.Add<TagComponent>(m_light1, "Sun 1");

        {
            LightComponent lc;
            lc.kind = LightKind::Directional;
            lc.diffuseColor = { 0.4f, 0.4f, 0.5f, 1.0f };
            lc.intensity = 1.5f;
            lc.castShadows = true;
            lc.shadowOrthoSize = 25.0f;
            lc.shadowNear = 0.1f;
            lc.shadowFar = 100.0f;
            reg.Add<LightComponent>(m_light1, lc);
        }

        {
            TransformComponent tc;
            tc.localPosition = { 0.0f, 20.0f, 10.0f };
            tc.SetEulerDeg(90.0f, 0.0f, 0.0f);
            reg.Add<TransformComponent>(m_light1, tc);
        }
        reg.Add<WorldTransformComponent>(m_light1);

        // ================================================================
        // Directional Light 2
        // ================================================================
        m_light2 = reg.CreateEntity();
        reg.Add<TagComponent>(m_light2, "Sun 2");

        {
            LightComponent lc;
            lc.kind = LightKind::Directional;
            lc.diffuseColor = { 0.5f, 0.5f, 0.5f, 1.0f };
            lc.intensity = 1.2f;
            lc.castShadows = false;
            reg.Add<LightComponent>(m_light2, lc);
        }

        {
            TransformComponent tc;
            tc.localPosition = { 0.0f, 10.0f, -10.0f };
            tc.SetEulerDeg(-45.0f, 0.0f, 0.0f);
            reg.Add<TransformComponent>(m_light2, tc);
        }
        reg.Add<WorldTransformComponent>(m_light2);

        m_renderer.SetSceneAmbient(0.9f, 0.9f, 0.9f);

        // ================================================================
        // Linker Würfel — wird in RTT aufgenommen
        // Layer: RTT
        // ================================================================
        m_cubeLeft = reg.CreateEntity();
        reg.Add<TagComponent>(m_cubeLeft, "Cube Left");

        {
            TransformComponent tc;
            tc.localPosition = { -4.0f, 0.0f, 10.0f };
            tc.localScale = { 2.0f, 2.0f, 2.0f };
            reg.Add<TransformComponent>(m_cubeLeft, tc);
        }
        reg.Add<WorldTransformComponent>(m_cubeLeft);
        reg.Add<MeshRefComponent>(m_cubeLeft, m_hCube, 0u);
        reg.Add<MaterialRefComponent>(m_cubeLeft, m_hMatLeft);

        {
            VisibilityComponent vis;
            vis.visible = true;
            vis.active = true;
            vis.layerMask = LAYER_RTT;
            vis.castShadows = true;
            reg.Add<VisibilityComponent>(m_cubeLeft, vis);
        }

        reg.Add<ShadowCasterTag>(m_cubeLeft);

        // ================================================================
        // Rechter Würfel — zeigt RTT-Textur
        // Layer: MAIN
        // -> RTT-Kamera sieht ihn nicht mehr
        // ================================================================
        m_cubeRight = reg.CreateEntity();
        reg.Add<TagComponent>(m_cubeRight, "Cube Right");

        {
            TransformComponent tc;
            tc.localPosition = { 4.0f, 0.0f, 5.0f };
            tc.localScale = { 2.0f, 2.0f, 2.0f };
            tc.SetEulerDeg(45.0f, 45.0f, 0.0f);
            reg.Add<TransformComponent>(m_cubeRight, tc);
        }
        reg.Add<WorldTransformComponent>(m_cubeRight);
        reg.Add<MeshRefComponent>(m_cubeRight, m_hCube, 0u);
        reg.Add<MaterialRefComponent>(m_cubeRight, m_hMatRight);

        {
            VisibilityComponent vis;
            vis.visible = true;
            vis.active = true;
            vis.layerMask = LAYER_MAIN;
            vis.castShadows = true;
            reg.Add<VisibilityComponent>(m_cubeRight, vis);
        }

        reg.Add<ShadowCasterTag>(m_cubeRight);

        Debug::Log("RTTDemo: Szene initialisiert.");
    }

    void Update(float dt)
    {
        Registry& reg = m_renderer.GetRegistry();

        if (auto* tc = reg.Get<TransformComponent>(m_cubeLeft))
        {
            m_leftPitch += 60.0f * dt;
            m_leftYaw += 80.0f * dt;

            if (m_leftPitch >= 360.0f) m_leftPitch -= 360.0f;
            if (m_leftYaw >= 360.0f) m_leftYaw -= 360.0f;

            tc->SetEulerDeg(m_leftPitch, m_leftYaw, 0.0f);
        }
    }

    void OnEvent(const Event& e, GDXEngine& engine)
    {
        std::visit([&](auto&& ev)
            {
                using T = std::decay_t<decltype(ev)>;

                if constexpr (std::is_same_v<T, KeyPressedEvent>)
                {
                    if (ev.key == Key::Escape)
                        engine.Shutdown();
                }
            }, e);
    }

private:
    GDXECSRenderer& m_renderer;

    EntityID m_mainCamera = NULL_ENTITY;
    EntityID m_rttCamera = NULL_ENTITY;
    EntityID m_light1 = NULL_ENTITY;
    EntityID m_light2 = NULL_ENTITY;
    EntityID m_cubeLeft = NULL_ENTITY;
    EntityID m_cubeRight = NULL_ENTITY;

    MeshHandle         m_hCube;
    MaterialHandle     m_hMatLeft;
    MaterialHandle     m_hMatRight;
    RenderTargetHandle m_hRTT;

    float m_leftPitch = 0.0f;
    float m_leftYaw = 0.0f;
};

int main()
{
    GDXEventQueue events;

    WindowDesc desc;
    desc.width = 1200;
    desc.height = 650;
    desc.title = "GIDX - ECS RTT Demo";
    desc.resizable = true;

    auto windowOwned = std::make_unique<GDXWin32Window>(desc, events);
    if (!windowOwned->Create())
    {
        Debug::LogError("main_rtt_ecs.cpp: Fenster konnte nicht erstellt werden");
        return 1;
    }

    GDXWin32Window* windowRaw = windowOwned.get();

    auto adapters = GDXWin32DX11ContextFactory::EnumerateAdapters();
    if (adapters.empty())
    {
        Debug::LogError("main_rtt_ecs.cpp: kein DX11-Adapter gefunden");
        return 2;
    }

    const unsigned int adapterIdx =
        GDXWin32DX11ContextFactory::FindBestAdapter(adapters);

    Debug::Log("main_rtt_ecs.cpp: DX11 adapter ", adapterIdx,
        " [", adapters[adapterIdx].name, "]");

    GDXWin32DX11ContextFactory dx11Factory;
    auto dxContext = dx11Factory.Create(*windowRaw, adapterIdx);
    if (!dxContext)
    {
        Debug::LogError("main_rtt_ecs.cpp: DX11 Context konnte nicht erstellt werden");
        return 3;
    }

    auto backendOwned = std::make_unique<GDXDX11RenderBackend>(std::move(dxContext));
    auto rendererOwned = std::make_unique<GDXECSRenderer>(std::move(backendOwned));
    GDXECSRenderer* renderer = rendererOwned.get();

    renderer->SetClearColor(0.8f, 0.8f, 0.8f);

    GDXEngine engine(std::move(windowOwned), std::move(rendererOwned), events);

    if (!engine.Initialize())
    {
        Debug::LogError("main_rtt_ecs.cpp: Engine-Initialisierung fehlgeschlagen");
        return 4;
    }

    RTTDemo game(*renderer);
    game.Init();

    renderer->SetTickCallback([&game](float dt)
        {
            game.Update(dt);
        });

    engine.SetEventCallback([&game, &engine](const Event& e)
        {
            game.OnEvent(e, engine);
        });

    engine.Run();
    engine.Shutdown();

    return 0;
}