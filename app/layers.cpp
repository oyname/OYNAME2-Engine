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
#include <filesystem>
#include <system_error>

// -----------------------------------------------------------------------------
// Layer-Bits passend zu deinem jetzigen System
// -----------------------------------------------------------------------------
static constexpr uint32_t LAYER_DEFAULT = 1u << 0;
static constexpr uint32_t LAYER_FX = 1u << 1;
static constexpr uint32_t LAYER_REFLECTION = 1u << 2;
static constexpr uint32_t LAYER_ALL = 0xFFFFFFFFu;

static bool FileExists(const std::wstring& path)
{
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

class LayersDemo
{
public:
    explicit LayersDemo(GDXECSRenderer& renderer)
        : m_renderer(renderer)
    {
    }

    void Init()
    {
        Registry& reg = m_renderer.GetRegistry();

        // ============================================================
        // Mesh: nur Cube verwenden, weil BuiltinMeshes::Cube() sicher da ist
        // ============================================================
        {
            MeshAssetResource asset;
            asset.debugName = "Cube";
            asset.AddSubmesh(BuiltinMeshes::Cube());
            m_hCube = m_renderer.UploadMesh(std::move(asset));
        }

        // ============================================================
        // Texturen laden wenn vorhanden
        // ============================================================
        TextureHandle hTexCube1 = TextureHandle::Invalid();
        TextureHandle hTexCube2 = TextureHandle::Invalid();
        TextureHandle hTexFloor = TextureHandle::Invalid();

        if (FileExists(L"..//media//color1.png"))
            hTexCube1 = m_renderer.LoadTexture(L"..//media//color1.png");

        if (FileExists(L"..//media//color3.png"))
            hTexCube2 = m_renderer.LoadTexture(L"..//media//color3.png");

        if (FileExists(L"..//media//bricks.bmp"))
            hTexFloor = m_renderer.LoadTexture(L"..//media//bricks.bmp");

        // ============================================================
        // Materialien
        // ============================================================
        {
            MaterialResource mat = MaterialResource::FlatColor(1.0f, 1.0f, 1.0f);
            if (hTexCube1.IsValid())
                mat.SetTexture(MaterialTextureSlot::Albedo, hTexCube1);
            m_hMatCube1 = m_renderer.CreateMaterial(mat);
        }

        {
            MaterialResource mat = MaterialResource::FlatColor(1.0f, 1.0f, 1.0f);
            if (hTexCube2.IsValid())
                mat.SetTexture(MaterialTextureSlot::Albedo, hTexCube2);
            m_hMatCube2 = m_renderer.CreateMaterial(mat);
        }

        {
            MaterialResource mat = MaterialResource::FlatColor(1.0f, 1.0f, 1.0f);
            if (hTexFloor.IsValid())
                mat.SetTexture(MaterialTextureSlot::Albedo, hTexFloor);
            m_hMatFloor = m_renderer.CreateMaterial(mat);
        }

        // ============================================================
        // Kamera
        // ============================================================
        m_camera = reg.CreateEntity();
        reg.Add<TagComponent>(m_camera, "Main Camera");

        {
            TransformComponent tc;
            tc.localPosition = { 0.0f, 30.0f, -50.0f };
            tc.SetEulerDeg(10.0f, 0.0f, 0.0f);
            reg.Add<TransformComponent>(m_camera, tc);
        }
        reg.Add<WorldTransformComponent>(m_camera);

        {
            CameraComponent cam;
            cam.fovDeg = 60.0f;
            cam.nearPlane = 0.1f;
            cam.farPlane = 500.0f;
            cam.aspectRatio = 1024.0f / 768.0f;
            cam.cullMask = LAYER_ALL;
            reg.Add<CameraComponent>(m_camera, cam);
        }
        reg.Add<ActiveCameraTag>(m_camera);

        // ============================================================
        // Cube 1 — DEFAULT layer
        // ============================================================
        m_cube1 = reg.CreateEntity();
        reg.Add<TagComponent>(m_cube1, "Cube 1");

        {
            TransformComponent tc;
            tc.localPosition = { 0.0f, 15.0f, 0.0f };
            tc.localScale = { 8.0f, 8.0f, 8.0f };
            reg.Add<TransformComponent>(m_cube1, tc);
        }
        reg.Add<WorldTransformComponent>(m_cube1);
        reg.Add<MeshRefComponent>(m_cube1, m_hCube, 0u);
        reg.Add<MaterialRefComponent>(m_cube1, m_hMatCube1);

        {
            VisibilityComponent vis;
            vis.visible = true;
            vis.active = true;
            vis.layerMask = LAYER_DEFAULT;
            vis.castShadows = true;
            reg.Add<VisibilityComponent>(m_cube1, vis);
        }

        reg.Add<ShadowCasterTag>(m_cube1);

        // ============================================================
        // Cube 2 — FX layer
        // ============================================================
        m_cube2 = reg.CreateEntity();
        reg.Add<TagComponent>(m_cube2, "Cube 2");

        {
            TransformComponent tc;
            tc.localPosition = { 0.0f, 3.0f, 0.0f };
            tc.localScale = { 5.0f, 5.0f, 5.0f };
            reg.Add<TransformComponent>(m_cube2, tc);
        }
        reg.Add<WorldTransformComponent>(m_cube2);
        reg.Add<MeshRefComponent>(m_cube2, m_hCube, 0u);
        reg.Add<MaterialRefComponent>(m_cube2, m_hMatCube2);

        {
            VisibilityComponent vis;
            vis.visible = true;
            vis.active = true;
            vis.layerMask = LAYER_FX;
            vis.castShadows = true;
            reg.Add<VisibilityComponent>(m_cube2, vis);
        }

        reg.Add<ShadowCasterTag>(m_cube2);

        // ============================================================
        // Boden — DEFAULT | REFLECTION
        // ============================================================
        m_floor = reg.CreateEntity();
        reg.Add<TagComponent>(m_floor, "Floor");

        {
            TransformComponent tc;
            tc.localPosition = { 0.0f, 0.0f, 0.0f };
            tc.localScale = { 100.0f, 1.0f, 100.0f };
            reg.Add<TransformComponent>(m_floor, tc);
        }
        reg.Add<WorldTransformComponent>(m_floor);
        reg.Add<MeshRefComponent>(m_floor, m_hCube, 0u);
        reg.Add<MaterialRefComponent>(m_floor, m_hMatFloor);

        {
            VisibilityComponent vis;
            vis.visible = true;
            vis.active = true;
            vis.layerMask = LAYER_DEFAULT | LAYER_REFLECTION;
            vis.castShadows = true;
            reg.Add<VisibilityComponent>(m_floor, vis);
        }

        reg.Add<ShadowCasterTag>(m_floor);

        // ============================================================
        // Directional Light
        // ============================================================
        m_sun = reg.CreateEntity();
        reg.Add<TagComponent>(m_sun, "Sun");

        {
            LightComponent lc;
            lc.kind = LightKind::Directional;
            lc.diffuseColor = { 0.1f, 0.1f, 0.1f, 1.0f };
            lc.intensity = 2.0f;
            lc.castShadows = true;
            lc.shadowOrthoSize = 150.0f;
            lc.shadowNear = 0.1f;
            lc.shadowFar = 400.0f;
            reg.Add<LightComponent>(m_sun, lc);
        }
        {
            TransformComponent tc;
            tc.localPosition = { 20.0f, 60.0f, -20.0f };
            tc.SetEulerDeg(-45.0f, -45.0f, 0.0f);
            reg.Add<TransformComponent>(m_sun, tc);
        }
        reg.Add<WorldTransformComponent>(m_sun);

        // ============================================================
        // Red Point Light
        // ============================================================
        m_redLight = reg.CreateEntity();
        reg.Add<TagComponent>(m_redLight, "Red Light");

        {
            LightComponent lc;
            lc.kind = LightKind::Point;
            lc.diffuseColor = { 1.0f, 0.3f, 0.3f, 1.0f };
            lc.intensity = 1.0f;
            lc.radius = 120.0f;
            reg.Add<LightComponent>(m_redLight, lc);
        }
        {
            TransformComponent tc;
            tc.localPosition = { 20.0f, 15.0f, 0.0f };
            reg.Add<TransformComponent>(m_redLight, tc);
        }
        reg.Add<WorldTransformComponent>(m_redLight);

        // ============================================================
        // Blue Point Light
        // ============================================================
        m_blueLight = reg.CreateEntity();
        reg.Add<TagComponent>(m_blueLight, "Blue Light");

        {
            LightComponent lc;
            lc.kind = LightKind::Point;
            lc.diffuseColor = { 0.3f, 0.3f, 1.0f, 1.0f };
            lc.intensity = 1.0f;
            lc.radius = 120.0f;
            reg.Add<LightComponent>(m_blueLight, lc);
        }
        {
            TransformComponent tc;
            tc.localPosition = { -20.0f, 15.0f, 0.0f };
            reg.Add<TransformComponent>(m_blueLight, tc);
        }
        reg.Add<WorldTransformComponent>(m_blueLight);

        m_renderer.SetSceneAmbient(0.08f, 0.08f, 0.09f);

        Debug::Log("Controls:");
        Debug::Log("A = toggle DEFAULT layer in camera cull mask");
        Debug::Log("S = toggle FX layer in camera cull mask");
        Debug::Log("D = toggle visibility of cube1");
        Debug::Log("F = toggle active state of cube2");
        Debug::Log("Arrow keys = move red point light");
        Debug::Log("ESC = quit");
    }

    void Update(float dt)
    {
        Registry& reg = m_renderer.GetRegistry();

        // Red light move
        if (auto* tc = reg.Get<TransformComponent>(m_redLight))
        {
            const float speed = 50.0f;
            if (m_keyUp)    tc->localPosition.z += speed * dt;
            if (m_keyDown)  tc->localPosition.z -= speed * dt;
            if (m_keyRight) tc->localPosition.x += speed * dt;
            if (m_keyLeft)  tc->localPosition.x -= speed * dt;
            tc->dirty = true;
        }

        // Cube1: soll weiter rotieren, auch wenn unsichtbar
        if (auto* vis = reg.Get<VisibilityComponent>(m_cube1))
        {
            if (vis->active)
            {
                if (auto* tc = reg.Get<TransformComponent>(m_cube1))
                {
                    m_cube1Pitch += 100.0f * dt;
                    m_cube1Yaw += 100.0f * dt;
                    WrapAngle(m_cube1Pitch);
                    WrapAngle(m_cube1Yaw);
                    tc->SetEulerDeg(m_cube1Pitch, m_cube1Yaw, 0.0f);
                }
            }
        }

        // Cube2: active=false soll Update + Render stoppen
        if (auto* vis = reg.Get<VisibilityComponent>(m_cube2))
        {
            if (vis->active)
            {
                if (auto* tc = reg.Get<TransformComponent>(m_cube2))
                {
                    m_cube2Yaw += 40.0f * dt;
                    WrapAngle(m_cube2Yaw);
                    tc->SetEulerDeg(0.0f, m_cube2Yaw, 0.0f);
                }
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
                    if (ev.repeat)
                        return;

                    switch (ev.key)
                    {
                    case Key::Escape:
                        engine.Shutdown();
                        break;

                    case Key::Up:    m_keyUp = true; break;
                    case Key::Down:  m_keyDown = true; break;
                    case Key::Left:  m_keyLeft = true; break;
                    case Key::Right: m_keyRight = true; break;

                    case Key::A:
                        ToggleCameraLayer(LAYER_DEFAULT);
                        break;

                    case Key::S:
                        ToggleCameraLayer(LAYER_FX);
                        break;

                    case Key::D:
                        ToggleVisibility(m_cube1);
                        break;

                    case Key::F:
                        ToggleActive(m_cube2);
                        break;

                    case Key::G:
                        ToggleActive(m_cube1);
                        break;
                    
                    default:
                        break;
                    }
                }
                else if constexpr (std::is_same_v<T, KeyReleasedEvent>)
                {
                    switch (ev.key)
                    {
                    case Key::Up:    m_keyUp = false; break;
                    case Key::Down:  m_keyDown = false; break;
                    case Key::Left:  m_keyLeft = false; break;
                    case Key::Right: m_keyRight = false; break;
                    default: break;
                    }
                }
            }, e);
    }

private:
    void ToggleCameraLayer(uint32_t bit)
    {
        Registry& reg = m_renderer.GetRegistry();
        if (auto* cam = reg.Get<CameraComponent>(m_camera))
        {
            cam->cullMask ^= bit;
            Debug::Log("camera cullMask = ", cam->cullMask);
        }
    }

    void ToggleVisibility(EntityID e)
    {
        Registry& reg = m_renderer.GetRegistry();
        if (auto* vis = reg.Get<VisibilityComponent>(e))
        {
            vis->visible = !vis->visible;
            Debug::Log("cube1 visible = ", vis->visible ? "true" : "false");
        }
    }

    void ToggleActive(EntityID e)
    {
        Registry& reg = m_renderer.GetRegistry();
        if (auto* vis = reg.Get<VisibilityComponent>(e))
        {
            vis->active = !vis->active;
            Debug::Log("cube2 active = ", vis->active ? "true" : "false");
        }
    }

    static void WrapAngle(float& a)
    {
        while (a >= 360.0f) a -= 360.0f;
        while (a < 0.0f)    a += 360.0f;
    }

private:
    GDXECSRenderer& m_renderer;

    EntityID m_camera = NULL_ENTITY;
    EntityID m_cube1 = NULL_ENTITY;
    EntityID m_cube2 = NULL_ENTITY;
    EntityID m_floor = NULL_ENTITY;
    EntityID m_sun = NULL_ENTITY;
    EntityID m_redLight = NULL_ENTITY;
    EntityID m_blueLight = NULL_ENTITY;

    MeshHandle m_hCube;
    MaterialHandle m_hMatCube1;
    MaterialHandle m_hMatCube2;
    MaterialHandle m_hMatFloor;

    float m_cube1Pitch = 0.0f;
    float m_cube1Yaw = 0.0f;
    float m_cube2Yaw = 0.0f;

    bool m_keyUp = false;
    bool m_keyDown = false;
    bool m_keyLeft = false;
    bool m_keyRight = false;
};

int main()
{
    GDXEventQueue events;

    WindowDesc desc;
    desc.width = 1024;
    desc.height = 768;
    desc.title = "GIDX - Layer / Visibility Demo";
    desc.resizable = true;

    auto windowOwned = std::make_unique<GDXWin32Window>(desc, events);
    if (!windowOwned->Create())
    {
        Debug::LogError("layers.cpp: Fenster konnte nicht erstellt werden");
        return 1;
    }

    GDXWin32Window* windowRaw = windowOwned.get();

    auto adapters = GDXWin32DX11ContextFactory::EnumerateAdapters();
    if (adapters.empty())
    {
        Debug::LogError("layers.cpp: kein DX11-Adapter gefunden");
        return 2;
    }

    const unsigned int adapterIdx =
        GDXWin32DX11ContextFactory::FindBestAdapter(adapters);

    GDXWin32DX11ContextFactory dx11Factory;
    auto dxContext = dx11Factory.Create(*windowRaw, adapterIdx);
    if (!dxContext)
    {
        Debug::LogError("layers.cpp: DX11 Context konnte nicht erstellt werden");
        return 3;
    }

    auto backendOwned = std::make_unique<GDXDX11RenderBackend>(std::move(dxContext));
    auto rendererOwned = std::make_unique<GDXECSRenderer>(std::move(backendOwned));
    GDXECSRenderer* renderer = rendererOwned.get();

    renderer->SetClearColor(0.0f, 0.25f, 0.5f);

    GDXEngine engine(std::move(windowOwned), std::move(rendererOwned), events);

    if (!engine.Initialize())
    {
        Debug::LogError("layers.cpp: Engine-Initialisierung fehlgeschlagen");
        return 4;
    }

    LayersDemo game(*renderer);
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