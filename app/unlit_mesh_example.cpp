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
#include "Events.h"
#include "GDXTextureLoader.h"

#include <memory>
#include <variant>
#include <filesystem>

class UnlitMeshTest
{
public:
    explicit UnlitMeshTest(GDXECSRenderer& renderer) : m_renderer(renderer) {}

    void Init()
    {
        Registry& reg = m_renderer.GetRegistry();

        // Mesh
        {
            MeshAssetResource asset;
            asset.debugName = "UnlitCube";
            asset.AddSubmesh(BuiltinMeshes::Cube());
            m_hCube = m_renderer.UploadMesh(std::move(asset));
        }

        // Material: kein eigener Unlit-Shader noetig.
        // Die Engine nutzt ECSVertexShader.hlsl + ECSPixelShader.hlsl.
        // Unlit wird ueber MF_UNLIT im Pixelshader aktiviert.
        MaterialResource mat = MaterialResource::FlatColor(1.0f, 1.0f, 1.0f, 1.0f);
        mat.SetFlag(MF_UNLIT, true);
        mat.data.emissiveColor = { 0.0f, 0.0f, 0.0f, 1.0f };
        mat.data.receiveShadows = 0.0f; // fuer klaren Unlit-Test

        // Optional: Albedo-Textur laden.
        // Pfad ggf. an dein Projekt anpassen.
        const std::filesystem::path texPath = std::filesystem::path("../media/engine.png");
        if (std::filesystem::exists(texPath))
        {

            TextureHandle albedo = m_renderer.LoadTexture(texPath.wstring(), true);
            if (albedo.IsValid())
                mat.SetTexture(MaterialTextureSlot::Albedo, albedo);
        }

        m_hUnlit = m_renderer.CreateMaterial(std::move(mat));

        // Kamera
        m_camera = reg.CreateEntity();
        reg.Add<TagComponent>(m_camera, "Camera");
        {
            TransformComponent tc;
            tc.localPosition = { 0.0f, 0.0f, -4.0f };
            tc.SetEulerDeg(0.0f, 0.0f, 0.0f);
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

        // Licht absichtlich vorhanden.
        // Unlit-Material darf davon NICHT sichtbar beeinflusst werden.
        m_light = reg.CreateEntity();
        reg.Add<TagComponent>(m_light, "Sun");
        {
            LightComponent lc;
            lc.kind = LightKind::Directional;
            lc.diffuseColor = { 1.0f, 0.9f, 0.8f, 1.0f };
            lc.intensity = 4.0f;
            lc.castShadows = true;
            lc.shadowOrthoSize = 20.0f;
            lc.shadowNear = 0.1f;
            lc.shadowFar = 100.0f;
            reg.Add<LightComponent>(m_light, lc);
        }
        {
            TransformComponent tc;
            tc.localPosition = { 5.0f, 8.0f, -3.0f };
            tc.SetEulerDeg(50.0f, -35.0f, 0.0f);
            reg.Add<TransformComponent>(m_light, tc);
        }
        reg.Add<WorldTransformComponent>(m_light);

        // Unlit Mesh
        m_cube = reg.CreateEntity();
        reg.Add<TagComponent>(m_cube, "UnlitCube");
        {
            TransformComponent tc;
            tc.localPosition = { 0.0f, 0.0f, 2.0f };
            tc.localScale = { 1.5f, 1.5f, 1.5f };
            reg.Add<TransformComponent>(m_cube, tc);
        }
        reg.Add<WorldTransformComponent>(m_cube);
        reg.Add<RenderableComponent>(m_cube, m_hCube, m_hUnlit, 0u);
        {
            VisibilityComponent vis;
            vis.visible = true;
            vis.active = true;
            vis.castShadows = false;     // fuer Minimaltest aus
            vis.receiveShadows = false;  // darf fuer Unlit egal sein, aber klarer Test
            reg.Add<VisibilityComponent>(m_cube, vis);
        }

        m_renderer.SetClearColor(0.05f, 0.06f, 0.09f);
        m_renderer.SetSceneAmbient(0.02f, 0.02f, 0.02f);

        Debug::Log("unlit_mesh_test: Cube muss konstant hell erscheinen, auch wenn Licht rotiert");
    }

    void Update(float dt)
    {
        Registry& reg = m_renderer.GetRegistry();

        m_cubeYaw += 40.0f * dt;
        if (m_cubeYaw >= 360.0f) m_cubeYaw -= 360.0f;

        m_lightYaw += 25.0f * dt;
        if (m_lightYaw >= 360.0f) m_lightYaw -= 360.0f;

        if (auto* tc = reg.Get<TransformComponent>(m_cube))
            tc->SetEulerDeg(20.0f, m_cubeYaw, 0.0f);

        if (auto* tc = reg.Get<TransformComponent>(m_light))
            tc->SetEulerDeg(50.0f, m_lightYaw, 0.0f);
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
    GDXECSRenderer& m_renderer;
    EntityID m_camera = NULL_ENTITY;
    EntityID m_light  = NULL_ENTITY;
    EntityID m_cube   = NULL_ENTITY;
    MeshHandle m_hCube;
    MaterialHandle m_hUnlit;
    float m_cubeYaw = 0.0f;
    float m_lightYaw = 0.0f;
};

int main()
{
    GDXEventQueue events;

    WindowDesc desc;
    desc.width = 1280;
    desc.height = 720;
    desc.title = "GIDX - Test Unlit Mesh";
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

    GDXEngine engine(std::move(windowOwned), std::move(rendererOwned), events);
    if (!engine.Initialize()) return 4;

    UnlitMeshTest app(*renderer);
    app.Init();
    renderer->SetTickCallback([&](float dt){ app.Update(dt); });
    engine.SetEventCallback([&](const Event& e){ app.OnEvent(e, engine); });

    engine.Run();
    engine.Shutdown();
    return 0;
}
