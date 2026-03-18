// parenting_showcase.cpp
//
// Sonnensystem-Demo für die GIDX ECS Engine.
//
// Hierarchie:
//   Sonne  (dreht sich um Y)
//     └─ Erde  (umkreist Sonne, dreht sich selbst)
//          └─ Mond  (umkreist Erde via lokaler Position)
//               └─ Flagge  (klebt starr am Mond, keine eigene Animation)
//     └─ Asteroid[0..7]  (Kinder der Sonne, erben Rotation, eigene Selbstrotation)
//
// Parenting über HierarchySystem::SetParent — nicht manuell über ParentComponent.
// Lokale Positionen pro Frame über TransformComponent::localPosition.
// TransformSystem::Update() propagiert dirty-Flags und berechnet Weltmatrizen.

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
#include "HierarchySystem.h"
#include "Events.h"

#include <memory>
#include <cmath>
#include <array>

static constexpr int NUM_ASTEROIDS = 8;
static constexpr float PI = 3.14159265358979f;

class SolarSystemDemo
{
public:
    explicit SolarSystemDemo(GDXECSRenderer& renderer)
        : m_renderer(renderer)
    {
    }

    void Init()
    {
        Registry& reg = m_renderer.GetRegistry();

        // -----------------------------------------------------------------
        // Meshes
        // -----------------------------------------------------------------
        {
            MeshAssetResource a;
            a.debugName = "Sphere";
            a.AddSubmesh(BuiltinMeshes::Sphere(0.5f, 24, 16));
            m_hSphere = m_renderer.UploadMesh(std::move(a));
        }
        {
            MeshAssetResource a;
            a.debugName = "Cube";
            a.AddSubmesh(BuiltinMeshes::Cube());
            m_hCube = m_renderer.UploadMesh(std::move(a));
        }

        // -----------------------------------------------------------------
        // Materialien
        // -----------------------------------------------------------------
        auto makePBR = [&](float r, float g, float b,
            float metallic, float roughness,
            bool  receiveShadows = true) -> MaterialHandle
            {
                MaterialResource mat = MaterialResource::FlatColor(r, g, b, 1.0f);
                mat.data.flags |= MF_SHADING_PBR;
                mat.data.metallic = metallic;
                mat.data.roughness = roughness;
                mat.data.receiveShadows = receiveShadows ? 1.0f : 0.0f;
                return m_renderer.CreateMaterial(mat);
            };

        // Boden
        const MaterialHandle hGround = makePBR(0.12f, 0.12f, 0.18f, 0.0f, 0.9f);

        // Sonne — emissiv + PBR
        MaterialResource sunMat = MaterialResource::FlatColor(1.0f, 0.85f, 0.10f, 1.0f);
        sunMat.data.flags |= MF_SHADING_PBR | MF_USE_EMISSIVE;
        sunMat.data.metallic = 0.0f;
        sunMat.data.roughness = 0.5f;
        sunMat.data.receiveShadows = 0.0f;
        sunMat.data.emissiveColor = { 1.0f, 0.7f, 0.0f, 1.2f };
        const MaterialHandle hSun = m_renderer.CreateMaterial(sunMat);

        const MaterialHandle hEarth = makePBR(0.15f, 0.40f, 0.85f, 0.0f, 0.6f);
        const MaterialHandle hMoon = makePBR(0.72f, 0.72f, 0.72f, 0.05f, 0.8f);

        // Flagge — emissiv weiß
        MaterialResource flagMat = MaterialResource::FlatColor(1.0f, 1.0f, 1.0f, 1.0f);
        flagMat.data.flags |= MF_SHADING_PBR | MF_USE_EMISSIVE;
        flagMat.data.metallic = 0.0f;
        flagMat.data.roughness = 0.3f;
        flagMat.data.emissiveColor = { 1.0f, 1.0f, 1.0f, 0.8f };
        const MaterialHandle hFlag = m_renderer.CreateMaterial(flagMat);

        const MaterialHandle hAsteroid = makePBR(0.55f, 0.42f, 0.30f, 0.1f, 0.85f);

        // -----------------------------------------------------------------
        // Kamera
        // -----------------------------------------------------------------
        m_camera = reg.CreateEntity();
        reg.Add<TagComponent>(m_camera, "Camera");
        {
            TransformComponent tc;
            tc.localPosition = { 0.0f, 18.0f, -26.0f };
            tc.SetEulerDeg(32.0f, 0.0f, 0.0f);
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

        // -----------------------------------------------------------------
        // Licht
        // -----------------------------------------------------------------
        m_light = reg.CreateEntity();
        reg.Add<TagComponent>(m_light, "Sun_Light");
        {
            LightComponent lc;
            lc.kind = LightKind::Directional;
            lc.diffuseColor = { 1.0f, 0.95f, 0.80f, 1.0f };
            lc.intensity = 5.0f;
            lc.castShadows = true;
            lc.shadowOrthoSize = 50.0f;
            lc.shadowNear = 1.0f;
            lc.shadowFar = 1000.0f;
            reg.Add<LightComponent>(m_light, lc);
        }
        {
            TransformComponent tc;
            tc.localPosition = { 0.0f, 0.0f, 0.0f };
            tc.SetEulerDeg(90.0f, 0.0f, 0.0f);
            reg.Add<TransformComponent>(m_light, tc);
        }
        reg.Add<WorldTransformComponent>(m_light);
        m_renderer.SetSceneAmbient(0.12f, 0.12f, 0.18f);

        // -----------------------------------------------------------------
        // Boden
        // -----------------------------------------------------------------
        MakeEntity("Ground", m_hCube, hGround,
            { 0.0f, -1.5f, 0.0f }, { 40.0f, 0.4f, 40.0f }, false);

        // -----------------------------------------------------------------
        // Sonne — Root-Entity
        // -----------------------------------------------------------------
        m_sun = MakeEntity("Sonne", m_hSphere, hSun,
            { 0.0f, 5.5f, 0.0f }, { 2.5f, 2.5f, 2.5f }, false);

        // -----------------------------------------------------------------
        // Erde — Kind der Sonne
        //
        // localPosition ist relativ zur Sonne: X=8 = 8 Einheiten Abstand.
        // Wenn die Sonne rotiert, wird die Erde automatisch mitgezogen.
        // -----------------------------------------------------------------
        m_earth = MakeEntity("Erde", m_hSphere, hEarth,
            { 8.0f, 0.0f, 0.0f }, { 1.2f, 1.2f, 1.2f }, true);
        HierarchySystem::SetParent(reg, m_earth, m_sun);

        // -----------------------------------------------------------------
        // Mond — Kind der Erde
        //
        // Eigene Umlaufbahn wird pro Frame über localPosition gesetzt.
        // -----------------------------------------------------------------
        m_moon = MakeEntity("Mond", m_hSphere, hMoon,
            { 2.8f, 0.0f, 0.0f }, { 0.5f, 0.5f, 0.5f }, true);
        HierarchySystem::SetParent(reg, m_moon, m_earth);

        // -----------------------------------------------------------------
        // Flagge — Kind des Mondes, keine eigene Animation
        // -----------------------------------------------------------------
        m_flag = MakeEntity("Flagge", m_hSphere, hFlag,
            { 0.0f, 0.5f, 0.0f }, { 0.18f, 0.18f, 0.18f }, false);
        HierarchySystem::SetParent(reg, m_flag, m_moon);

        // -----------------------------------------------------------------
        // Asteroidenring — 8 Kinder der Sonne
        //
        // Positionen gleichmäßig im Kreis (r=5 lokal zur Sonne).
        // Erben die Sonnen-Rotation automatisch.
        // -----------------------------------------------------------------
        for (int i = 0; i < NUM_ASTEROIDS; ++i)
        {
            const float a = 2.0f * PI * static_cast<float>(i) / static_cast<float>(NUM_ASTEROIDS);
            const float r = 5.0f;
            const float s = 0.22f + 0.10f * std::sin(static_cast<float>(i) * 1.3f);

            char name[32];
            std::snprintf(name, sizeof(name), "Asteroid_%d", i);

            m_asteroids[i] = MakeEntity(name, m_hSphere, hAsteroid,
                { r * std::cos(a),
                  0.3f * std::sin(static_cast<float>(i) * 0.9f),
                  r * std::sin(a) },
                { s, s, s }, true);

            HierarchySystem::SetParent(reg, m_asteroids[i], m_sun);
        }

        Debug::Log("SolarSystemDemo: Hierarchie aufgebaut - ESC beendet");
    }

    void Update(float dt)
    {
        Registry& reg = m_renderer.GetRegistry();

        m_moonAngle += 80.0f * dt;
        if (m_moonAngle >= 360.0f) m_moonAngle -= 360.0f;

        // Sonne dreht sich um Y — zieht alle Kinder (Erde, Asteroiden) mit
        if (auto* tc = reg.Get<TransformComponent>(m_sun))
        {
            m_sunYaw += 15.0f * dt;
            if (m_sunYaw >= 360.0f) m_sunYaw -= 360.0f;
            tc->SetEulerDeg(0.0f, m_sunYaw, 0.0f);
        }

        // Erde dreht sich eigenständig um sich selbst
        if (auto* tc = reg.Get<TransformComponent>(m_earth))
        {
            m_earthYaw += 40.0f * dt;
            if (m_earthYaw >= 360.0f) m_earthYaw -= 360.0f;
            tc->SetEulerDeg(0.0f, m_earthYaw, 0.0f);
        }

        // Mond umkreist die Erde — lokale Position pro Frame setzen.
        // Da Erde sich dreht, zieht sie den Mond ebenfalls mit.
        // Eigene Umlaufbahn durch manuelle Positionssteuerung im Erd-lokalen Raum.
        if (auto* tc = reg.Get<TransformComponent>(m_moon))
        {
            const float rad = m_moonAngle * PI / 180.0f;
            tc->localPosition = { 2.8f * std::cos(rad), 0.0f, 2.8f * std::sin(rad) };
            tc->dirty = true;
        }

        // Asteroiden — leichte Selbstrotation
        for (int i = 0; i < NUM_ASTEROIDS; ++i)
        {
            if (auto* tc = reg.Get<TransformComponent>(m_asteroids[i]))
            {
                m_asteroidYaw[i] += (30.0f + 10.0f * static_cast<float>(i)) * dt;
                if (m_asteroidYaw[i] >= 360.0f) m_asteroidYaw[i] -= 360.0f;
                tc->SetEulerDeg(
                    20.0f * dt * static_cast<float>(i),
                    m_asteroidYaw[i],
                    10.0f * dt * static_cast<float>(i));
            }
        }
    }

    void OnEvent(const Event& e, GDXEngine& engine)
    {
        std::visit([&](auto&& ev)
            {
                using T = std::decay_t<decltype(ev)>;
                if constexpr (std::is_same_v<T, KeyPressedEvent>)
                {
                    if (!ev.repeat && ev.key == Key::Escape)
                        engine.Shutdown();
                }
            }, e);
    }

private:
    // Erstellt eine vollständige Render-Entity ohne Parent.
    // Parent wird separat über HierarchySystem::SetParent gesetzt.
    EntityID MakeEntity(const char* name,
        MeshHandle                   mesh,
        MaterialHandle               mat,
        const Float3& pos,
        const Float3& scale,
        bool                         castShadows)
    {
        Registry& reg = m_renderer.GetRegistry();
        EntityID e = reg.CreateEntity();

        reg.Add<TagComponent>(e, name);

        {
            TransformComponent tc;
            tc.localPosition = pos;
            tc.localScale = scale;
            reg.Add<TransformComponent>(e, tc);
        }
        reg.Add<WorldTransformComponent>(e);
        reg.Add<RenderableComponent>(e, mesh, mat, 0u);

        {
            VisibilityComponent vis;
            vis.visible = true;
            vis.active = true;
            vis.castShadows = castShadows;
            reg.Add<VisibilityComponent>(e, vis);
        }
        return e;
    }

private:
    GDXECSRenderer& m_renderer;

    MeshHandle     m_hSphere;
    MeshHandle     m_hCube;

    EntityID m_camera = NULL_ENTITY;
    EntityID m_light = NULL_ENTITY;
    EntityID m_sun = NULL_ENTITY;
    EntityID m_earth = NULL_ENTITY;
    EntityID m_moon = NULL_ENTITY;
    EntityID m_flag = NULL_ENTITY;

    std::array<EntityID, NUM_ASTEROIDS> m_asteroids = {};

    // Animationswinkel (Grad)
    float m_sunYaw = 0.0f;
    float m_earthYaw = 0.0f;
    float m_moonAngle = 0.0f;
    std::array<float, NUM_ASTEROIDS> m_asteroidYaw = {};
};

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main()
{
    GDXEventQueue events;

    WindowDesc desc;
    desc.width = 1280;
    desc.height = 720;
    desc.title = "GIDX - Sonnensystem Parenting Demo";
    desc.resizable = true;

    auto windowOwned = std::make_unique<GDXWin32Window>(desc, events);
    if (!windowOwned->Create())
    {
        Debug::LogError("parenting_showcase: Fenster konnte nicht erstellt werden");
        return 1;
    }

    auto adapters = GDXWin32DX11ContextFactory::EnumerateAdapters();
    if (adapters.empty()) return 2;

    GDXWin32DX11ContextFactory dx11Factory;
    auto dxContext = dx11Factory.Create(
        *windowOwned,
        GDXWin32DX11ContextFactory::FindBestAdapter(adapters));
    if (!dxContext) return 3;

    auto backendOwned = std::make_unique<GDXDX11RenderBackend>(std::move(dxContext));
    auto rendererOwned = std::make_unique<GDXECSRenderer>(std::move(backendOwned));
    GDXECSRenderer* renderer = rendererOwned.get();

    renderer->SetClearColor(0.02f, 0.02f, 0.06f);

    GDXEngine engine(std::move(windowOwned), std::move(rendererOwned), events);
    if (!engine.Initialize())
    {
        Debug::LogError("parenting_showcase: Engine-Initialisierung fehlgeschlagen");
        return 4;
    }

    SolarSystemDemo app(*renderer);
    app.Init();

    renderer->SetTickCallback([&](float dt) { app.Update(dt); });
    engine.SetEventCallback([&](const Event& e) { app.OnEvent(e, engine); });

    engine.Run();
    engine.Shutdown();
    return 0;
}