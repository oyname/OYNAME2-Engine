#include "GIDXEngine.h"
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
#include "Events.h"

#include <memory>
#include <variant>
#include <cmath>

class StaticOpaqueTest
{
public:
    explicit StaticOpaqueTest(GDXECSRenderer& renderer) : m_renderer(renderer) {}

    void Init()
    {
        Registry& reg = m_renderer.GetRegistry();

        {
            MeshAssetResource asset;
            asset.debugName = "Cube";
            asset.AddSubmesh(BuiltinMeshes::Cube());
            m_hCube = m_renderer.UploadMesh(std::move(asset));
        }
        {
            MeshAssetResource asset;
            asset.debugName = "Sphere";
            asset.AddSubmesh(BuiltinMeshes::Sphere());
            m_hSphere = m_renderer.UploadMesh(std::move(asset));
        }

        m_hGray  = m_renderer.CreateMaterial(MaterialResource::FlatColor(0.45f, 0.45f, 0.48f));
        m_hRed   = m_renderer.CreateMaterial(MaterialResource::FlatColor(0.85f, 0.18f, 0.14f));
        m_hBlue  = m_renderer.CreateMaterial(MaterialResource::FlatColor(0.15f, 0.38f, 0.88f));
        m_hGreen = m_renderer.CreateMaterial(MaterialResource::FlatColor(0.18f, 0.75f, 0.28f));

        m_camera = reg.CreateEntity();
        reg.Add<TagComponent>(m_camera, "Camera");
        {
            TransformComponent tc;
            tc.localPosition = { 0.0f, 3.2f, -10.5f };
            tc.SetEulerDeg(12.0f, 0.0f, 0.0f);
            reg.Add<TransformComponent>(m_camera, tc);
        }
        reg.Add<WorldTransformComponent>(m_camera);
        {
            CameraComponent cam;
            cam.fovDeg = 60.0f;
            cam.nearPlane = 0.1f;
            cam.farPlane = 100.0f;
            cam.aspectRatio = 1280.0f / 720.0f;
            reg.Add<CameraComponent>(m_camera, cam);
        }
        reg.Add<ActiveCameraTag>(m_camera);

        m_light = reg.CreateEntity();
        reg.Add<TagComponent>(m_light, "Sun");
        {
            LightComponent lc;
            lc.kind = LightKind::Directional;
            lc.diffuseColor = { 1.0f, 0.97f, 0.92f, 1.0f };
            lc.intensity = 2.8f;
            lc.castShadows = true;
            lc.shadowOrthoSize = 25.0f;
            lc.shadowNear = 0.1f;
            lc.shadowFar = 1080.0f;
            reg.Add<LightComponent>(m_light, lc);
        }
        {
            TransformComponent tc;
            tc.localPosition = { 6.0f, 10.0f, -4.0f };
            tc.SetEulerDeg(55.0f, 40.0f, 0.0f);
            reg.Add<TransformComponent>(m_light, tc);
        }
        reg.Add<WorldTransformComponent>(m_light);

        m_renderer.SetSceneAmbient(0.10f, 0.10f, 0.12f);

        m_floor = MakeEntity("Floor", m_hCube, m_hGray,
            { 0.0f, -1.5f, 2.0f }, { 8.0f, 0.5f, 8.0f }, true);

        m_cubeA = MakeEntity("Cube A", m_hCube, m_hRed,
            { -2.2f, 0.0f, 2.0f }, { 1.5f, 1.5f, 1.5f }, true);

        m_cubeB = MakeEntity("Cube B", m_hCube, m_hBlue,
            { 2.3f, 0.0f, 3.2f }, { 1.3f, 1.3f, 1.3f }, true);

        m_sphere = MakeEntity("Sphere", m_hSphere, m_hGreen,
            { 0.0f, 0.2f, 5.5f }, { 1.2f, 1.2f, 1.2f }, true);

        Debug::Log("static_opaque_test: bereit - ESC beendet");
    }

    void Update(float dt)
    {
        Registry& reg = m_renderer.GetRegistry();

        m_yawA += 40.0f * dt;
        m_yawB += 28.0f * dt;
        m_pitchSphere += 55.0f * dt;
        Wrap(m_yawA);
        Wrap(m_yawB);
        Wrap(m_pitchSphere);

        if (auto* tc = reg.Get<TransformComponent>(m_cubeA))
            tc->SetEulerDeg(0.0f, m_yawA, 0.0f);

        if (auto* tc = reg.Get<TransformComponent>(m_cubeB))
            tc->SetEulerDeg(18.0f, m_yawB, 0.0f);

        if (auto* tc = reg.Get<TransformComponent>(m_sphere))
            tc->SetEulerDeg(m_pitchSphere, m_pitchSphere * 0.6f, 0.0f);
    }

    void OnEvent(const Event& e, GIDXEngine& engine)
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
    EntityID MakeEntity(const char* name, MeshHandle mesh, MaterialHandle mat,
                        const Float3& pos,
                        const Float3& scale,
                        bool castShadows)
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

    static void Wrap(float& v)
    {
        while (v >= 360.0f) v -= 360.0f;
        while (v < 0.0f) v += 360.0f;
    }

private:
    GDXECSRenderer& m_renderer;
    EntityID m_camera = NULL_ENTITY, m_light = NULL_ENTITY, m_floor = NULL_ENTITY;
    EntityID m_cubeA = NULL_ENTITY, m_cubeB = NULL_ENTITY, m_sphere = NULL_ENTITY;
    MeshHandle m_hCube, m_hSphere;
    MaterialHandle m_hGray, m_hRed, m_hBlue, m_hGreen;
    float m_yawA = 0.0f, m_yawB = 0.0f, m_pitchSphere = 0.0f;
};

int main()
{
    GDXEventQueue events;
    WindowDesc desc;
    desc.width = 1280;
    desc.height = 720;
    desc.title = "GIDX - Test Static Opaque";
    desc.resizable = true;

    auto windowOwned = std::make_unique<GDXWin32Window>(desc, events);
    if (!windowOwned->Create()) return 1;

    auto adapters = GDXWin32DX11ContextFactory::EnumerateAdapters();
    if (adapters.empty()) return 2;

    GDXWin32DX11ContextFactory dx11Factory;
    auto dxContext = dx11Factory.Create(*windowOwned, GDXWin32DX11ContextFactory::FindBestAdapter(adapters));
    if (!dxContext) return 3;

    auto backendOwned = std::make_unique<GDXDX11RenderBackend>(std::move(dxContext));
    auto rendererOwned = std::make_unique<GDXECSRenderer>(std::move(backendOwned));
    GDXECSRenderer* renderer = rendererOwned.get();
    renderer->SetClearColor(0.05f, 0.06f, 0.09f);

    GIDXEngine engine(std::move(windowOwned), std::move(rendererOwned), events);
    if (!engine.Initialize()) return 4;

    StaticOpaqueTest app(*renderer);
    app.Init();
    renderer->SetTickCallback([&](float dt){ app.Update(dt); });
    engine.SetEventCallback([&](const Event& e){ app.OnEvent(e, engine); });
    engine.Run();
    engine.Shutdown();
    return 0;
}
