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

#include <memory>
#include <cmath>

class TransparencyDemo
{
public:
    explicit TransparencyDemo(GDXECSRenderer& renderer)
        : m_renderer(renderer)
    {
    }

    void Init()
    {
        Registry& reg = m_renderer.GetRegistry();

        Debug::Log("transparent_example.cpp: Init() gestartet");

        // ============================================================
        // Mesh: Cube
        // ============================================================
        {
            MeshAssetResource asset;
            asset.debugName = "Cube";
            asset.AddSubmesh(BuiltinMeshes::Cube());
            m_hCube = m_renderer.UploadMesh(std::move(asset));
        }

        // ============================================================
        // Kamera
        // ============================================================
        m_camera = reg.CreateEntity();
        reg.Add<TagComponent>(m_camera, "Main Camera");

        {
            TransformComponent tc;
            tc.localPosition = { 0.0f, 1.0f, -7.0f };
            reg.Add<TransformComponent>(m_camera, tc);
        }
        reg.Add<WorldTransformComponent>(m_camera);

        {
            CameraComponent cam;
            cam.fovDeg = 60.0f;
            cam.nearPlane = 0.1f;
            cam.farPlane = 200.0f;
            cam.aspectRatio = 1280.0f / 720.0f;
            reg.Add<CameraComponent>(m_camera, cam);
        }
        reg.Add<ActiveCameraTag>(m_camera);

        // ============================================================
        // Licht
        // ============================================================
        m_light = reg.CreateEntity();
        reg.Add<TagComponent>(m_light, "Sun");

        {
            LightComponent lc;
            lc.kind = LightKind::Directional;
            lc.diffuseColor = { 0.90f, 0.90f, 1.0f, 1.0f };
            lc.intensity = 2.2f;
            lc.castShadows = true;
            lc.shadowOrthoSize = 30.0f;
            lc.shadowNear = 0.1f;
            lc.shadowFar = 1000.0f;
            reg.Add<LightComponent>(m_light, lc);
        }

        {
            TransformComponent tc;
            tc.localPosition = { 0.0f, 0.0f, 0.0f };
            tc.SetEulerDeg(45.0f, 35.0f, 0.0f);
            reg.Add<TransformComponent>(m_light, tc);
        }
        reg.Add<WorldTransformComponent>(m_light);

        m_renderer.SetSceneAmbient(0.32f, 0.32f, 0.32f);

        // ============================================================
        // Materialien
        // ============================================================

        // Boden: opak
        {
            MaterialResource mat = MaterialResource::FlatColor(0.8f, 0.8f, 0.8f, 1.0f);
            mat.data.receiveShadows = 1.0f;
            mat.data.transparency = 0.0f;
            mat.data.flags = MF_NONE;
            m_hMatFloor = m_renderer.CreateMaterial(mat);
        }

        // Opaker Referenzwrfel: rot
        {
            MaterialResource mat = MaterialResource::FlatColor(0.85f, 0.15f, 0.15f, 1.0f);
            mat.data.receiveShadows = 1.0f;
            mat.data.transparency = 0.0f;
            mat.data.flags = MF_NONE;
            m_hMatSolid = m_renderer.CreateMaterial(mat);
        }

        // Transparent weit: blau
        {
            MaterialResource mat = MaterialResource::FlatColor(0.2f, 0.4f, 1.0f, 0.45f);
            mat.data.receiveShadows = 1.0f;
            mat.data.transparency = 1.0f - 0.45f; // 0=opak, 1=voll transparent
            mat.data.flags = MF_TRANSPARENT;
            m_hMatFar = m_renderer.CreateMaterial(mat);
        }

        // Transparent mitte: grn
        {
            MaterialResource mat = MaterialResource::FlatColor(0.2f, 0.9f, 0.3f, 0.50f);
            mat.data.receiveShadows = 1.0f;
            mat.data.transparency = 1.0f - 0.50f;
            mat.data.flags = MF_TRANSPARENT;
            m_hMatMid = m_renderer.CreateMaterial(mat);
        }

        // Transparent nah: gelb
        {
            MaterialResource mat = MaterialResource::FlatColor(1.0f, 0.85f, 0.1f, 0.55f);
            mat.data.receiveShadows = 1.0f;
            mat.data.transparency = 1.0f - 0.55f;
            mat.data.flags = MF_TRANSPARENT;
            m_hMatNear = m_renderer.CreateMaterial(mat);
        }

        // ============================================================
        // Boden (flacher Cube statt Plate)
        // ============================================================
        m_floor = reg.CreateEntity();
        reg.Add<TagComponent>(m_floor, "Floor");

        {
            TransformComponent tc;
            tc.localPosition = { 0.0f, -2.0f, 0.0f };
            tc.localScale = { 8.0f, 1.0f, 8.0f };
            reg.Add<TransformComponent>(m_floor, tc);
        }
        reg.Add<WorldTransformComponent>(m_floor);
        reg.Add<MeshRefComponent>(m_floor, m_hCube, 0u);
        reg.Add<MaterialRefComponent>(m_floor, m_hMatFloor);

        {
            VisibilityComponent vis;
            vis.visible = true;
            vis.active = true;
            vis.castShadows = true;
            reg.Add<VisibilityComponent>(m_floor, vis);
        }

        reg.Add<ShadowCasterTag>(m_floor);

        // ============================================================
        // Opaker Referenzwrfel hinten
        // ============================================================
        m_solidCube = reg.CreateEntity();
        reg.Add<TagComponent>(m_solidCube, "Opaque Red Cube");

        {
            TransformComponent tc;
            tc.localPosition = { 0.0f, 0.0f, 5.0f };
            tc.localScale = { 1.2f, 1.2f, 1.2f };
            reg.Add<TransformComponent>(m_solidCube, tc);
        }
        reg.Add<WorldTransformComponent>(m_solidCube);
        reg.Add<MeshRefComponent>(m_solidCube, m_hCube, 0u);
        reg.Add<MaterialRefComponent>(m_solidCube, m_hMatSolid);

        {
            VisibilityComponent vis;
            vis.visible = true;
            vis.active = true;
            vis.castShadows = true;
            reg.Add<VisibilityComponent>(m_solidCube, vis);
        }

        reg.Add<ShadowCasterTag>(m_solidCube);

        // ============================================================
        // Transparente Wrfel
        // ============================================================

        // weit
        m_cubeFar = reg.CreateEntity();
        reg.Add<TagComponent>(m_cubeFar, "Transparent Far");

        {
            TransformComponent tc;
            tc.localPosition = { 0.0f, 0.0f, 3.0f };
            reg.Add<TransformComponent>(m_cubeFar, tc);
        }
        reg.Add<WorldTransformComponent>(m_cubeFar);
        reg.Add<MeshRefComponent>(m_cubeFar, m_hCube, 0u);
        reg.Add<MaterialRefComponent>(m_cubeFar, m_hMatFar);

        {
            VisibilityComponent vis;
            vis.visible = true;
            vis.active = true;
            vis.castShadows = true;
            reg.Add<VisibilityComponent>(m_cubeFar, vis);
        }

        reg.Add<ShadowCasterTag>(m_cubeFar);

        // mitte
        m_cubeMid = reg.CreateEntity();
        reg.Add<TagComponent>(m_cubeMid, "Transparent Mid");

        {
            TransformComponent tc;
            tc.localPosition = { 0.0f, 0.0f, 0.0f };
            reg.Add<TransformComponent>(m_cubeMid, tc);
        }
        reg.Add<WorldTransformComponent>(m_cubeMid);
        reg.Add<MeshRefComponent>(m_cubeMid, m_hCube, 0u);
        reg.Add<MaterialRefComponent>(m_cubeMid, m_hMatMid);

        {
            VisibilityComponent vis;
            vis.visible = true;
            vis.active = true;
            vis.castShadows = true;
            reg.Add<VisibilityComponent>(m_cubeMid, vis);
        }

        reg.Add<ShadowCasterTag>(m_cubeMid);

        // nah
        m_cubeNear = reg.CreateEntity();
        reg.Add<TagComponent>(m_cubeNear, "Transparent Near");

        {
            TransformComponent tc;
            tc.localPosition = { 0.0f, 0.0f, -3.0f };
            reg.Add<TransformComponent>(m_cubeNear, tc);
        }
        reg.Add<WorldTransformComponent>(m_cubeNear);
        reg.Add<MeshRefComponent>(m_cubeNear, m_hCube, 0u);
        reg.Add<MaterialRefComponent>(m_cubeNear, m_hMatNear);

        {
            VisibilityComponent vis;
            vis.visible = true;
            vis.active = true;
            vis.castShadows = true;
            reg.Add<VisibilityComponent>(m_cubeNear, vis);
        }

        reg.Add<ShadowCasterTag>(m_cubeNear);

        Debug::Log("transparent_example.cpp: Szene aufgebaut - 1 opaker Referenzwuerfel, 3 transparente Wuerfel");
    }

    void Update(float dt)
    {
        Registry& reg = m_renderer.GetRegistry();

        m_rotFar += 35.0f * dt;
        m_rotMid += 24.5f * dt;
        m_rotNear += 45.5f * dt;

        WrapAngle(m_rotFar);
        WrapAngle(m_rotMid);
        WrapAngle(m_rotNear);

        if (auto* tc = reg.Get<TransformComponent>(m_cubeFar))
            tc->SetEulerDeg(0.0f, m_rotFar, 0.0f);

        if (auto* tc = reg.Get<TransformComponent>(m_cubeMid))
            tc->SetEulerDeg(0.0f, m_rotMid, 0.0f);

        if (auto* tc = reg.Get<TransformComponent>(m_cubeNear))
            tc->SetEulerDeg(0.0f, m_rotNear, 0.0f);
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
    static void WrapAngle(float& a)
    {
        while (a >= 360.0f) a -= 360.0f;
        while (a < 0.0f)    a += 360.0f;
    }

private:
    GDXECSRenderer& m_renderer;

    EntityID m_camera = NULL_ENTITY;
    EntityID m_light = NULL_ENTITY;
    EntityID m_floor = NULL_ENTITY;
    EntityID m_solidCube = NULL_ENTITY;
    EntityID m_cubeFar = NULL_ENTITY;
    EntityID m_cubeMid = NULL_ENTITY;
    EntityID m_cubeNear = NULL_ENTITY;

    MeshHandle m_hCube;

    MaterialHandle m_hMatFloor;
    MaterialHandle m_hMatSolid;
    MaterialHandle m_hMatFar;
    MaterialHandle m_hMatMid;
    MaterialHandle m_hMatNear;

    float m_rotFar = 0.0f;
    float m_rotMid = 0.0f;
    float m_rotNear = 0.0f;
};

int main()
{
    GDXEventQueue events;

    WindowDesc desc;
    desc.width = 1280;
    desc.height = 720;
    desc.title = "GIDX - Transparency Example";
    desc.resizable = true;
    desc.borderless = false;

    auto windowOwned = std::make_unique<GDXWin32Window>(desc, events);
    if (!windowOwned->Create())
    {
        Debug::LogError("transparent_example.cpp: Fenster konnte nicht erstellt werden");
        return 1;
    }

    GDXWin32Window* windowRaw = windowOwned.get();


    auto adapters = GDXWin32DX11ContextFactory::EnumerateAdapters();
    if (adapters.empty())
    {
        Debug::LogError("transparent_example.cpp: kein DX11-Adapter gefunden");
        return 2;
    }

    const unsigned int adapterIdx =
        GDXWin32DX11ContextFactory::FindBestAdapter(adapters);

    GDXWin32DX11ContextFactory dx11Factory;
    auto dxContext = dx11Factory.Create(*windowRaw, adapterIdx);
    if (!dxContext)
    {
        Debug::LogError("transparent_example.cpp: DX11 Context konnte nicht erstellt werden");
        return 3;
    }

    auto backendOwned = std::make_unique<GDXDX11RenderBackend>(std::move(dxContext));
    auto rendererOwned = std::make_unique<GDXECSRenderer>(std::move(backendOwned));
    GDXECSRenderer* renderer = rendererOwned.get();

    renderer->SetClearColor(0.12f, 0.12f, 0.16f);

    GDXEngine engine(std::move(windowOwned), std::move(rendererOwned), events);

    if (!engine.Initialize())
    {
        Debug::LogError("transparent_example.cpp: Engine-Initialisierung fehlgeschlagen");
        return 4;
    }

    TransparencyDemo game(*renderer);
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

    Debug::Log("transparent_example.cpp: main() beendet");
    return 0;
}