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
#include <array>

static SubmeshData MakeVertexColorCube()
{
    SubmeshData s = BuiltinMeshes::Cube();
    s.colors.resize(s.positions.size());

    static const std::array<Float4, 6> faceColors = {
        Float4{1,0,0,1},
        Float4{0,1,0,1},
        Float4{0,0,1,1},
        Float4{1,1,0,1},
        Float4{1,0,1,1},
        Float4{0,1,1,1}
    };

    for (size_t face = 0; face < 6; ++face)
    {
        const auto c = faceColors[face];
        const size_t base = face * 4;
        for (size_t i = 0; i < 4; ++i)
            s.colors[base + i] = c;
    }
    return s;
}

class VertexColorTest
{
public:
    explicit VertexColorTest(GDXECSRenderer& renderer) : m_renderer(renderer) {}

    void Init()
    {
        Registry& reg = m_renderer.GetRegistry();

        {
            MeshAssetResource asset;
            asset.debugName = "VertexColorCube";
            asset.AddSubmesh(MakeVertexColorCube());
            m_hColorCube = m_renderer.UploadMesh(std::move(asset));
        }
        {
            MeshAssetResource asset;
            asset.debugName = "PlainCube";
            asset.AddSubmesh(BuiltinMeshes::Cube());
            m_hPlainCube = m_renderer.UploadMesh(std::move(asset));
        }

        m_hWhite = m_renderer.CreateMaterial(MaterialResource::FlatColor(1.0f, 1.0f, 1.0f, 1.0f));
        m_hGray  = m_renderer.CreateMaterial(MaterialResource::FlatColor(0.35f, 0.35f, 0.38f, 1.0f));
        m_hRed   = m_renderer.CreateMaterial(MaterialResource::FlatColor(0.8f, 0.2f, 0.2f, 1.0f));

        m_camera = reg.CreateEntity();
        reg.Add<TagComponent>(m_camera, "Camera");
        {
            TransformComponent tc;
            tc.localPosition = { 0.0f, 2.6f, -9.5f };
            tc.SetEulerDeg(10.0f, 0.0f, 0.0f);
            reg.Add<TransformComponent>(m_camera, tc);
        }
        reg.Add<WorldTransformComponent>(m_camera);
        {
            CameraComponent cam;
            cam.aspectRatio = 1280.0f / 720.0f;
            cam.farPlane = 100.0f;
            reg.Add<CameraComponent>(m_camera, cam);
        }
        reg.Add<ActiveCameraTag>(m_camera);

        m_light = reg.CreateEntity();
        reg.Add<TagComponent>(m_light, "Sun");
        {
            LightComponent lc;
            lc.kind = LightKind::Directional;
            lc.diffuseColor = { 1, 1, 1, 1 };
            lc.intensity = 2.4f;
            lc.castShadows = true;
            lc.shadowOrthoSize = 22.0f;
            lc.shadowNear = 0.1f;
            lc.shadowFar = 60.0f;
            reg.Add<LightComponent>(m_light, lc);
        }
        {
            TransformComponent tc;
            tc.localPosition = { 5.0f, 8.0f, -4.0f };
            tc.SetEulerDeg(55.0f, -35.0f, 0.0f);
            reg.Add<TransformComponent>(m_light, tc);
        }
        reg.Add<WorldTransformComponent>(m_light);

        m_renderer.SetSceneAmbient(0.10f, 0.10f, 0.12f);

        m_floor = MakeEntity("Floor", m_hPlainCube, m_hGray,
            { 0.0f, -1.5f, 2.0f }, { 8.0f, 0.5f, 8.0f });

        m_colorCube = MakeEntity("VertexColorCube", m_hColorCube, m_hWhite,
            { -1.8f, 0.0f, 2.0f }, { 1.8f, 1.8f, 1.8f });

        m_plainCube = MakeEntity("PlainCube", m_hPlainCube, m_hRed,
            { 2.2f, 0.0f, 3.0f }, { 1.5f, 1.5f, 1.5f });

        Debug::Log("vertexcolor_test: linker Wuerfel muss bunte Faces zeigen; ESC beendet");
    }

    void Update(float dt)
    {
        Registry& reg = m_renderer.GetRegistry();
        m_yawA += 40.0f * dt;
        m_yawB -= 33.0f * dt;
        Wrap(m_yawA);
        Wrap(m_yawB);

        if (auto* tc = reg.Get<TransformComponent>(m_colorCube))
            tc->SetEulerDeg(18.0f, m_yawA, 0.0f);
        if (auto* tc = reg.Get<TransformComponent>(m_plainCube))
            tc->SetEulerDeg(0.0f, m_yawB, 0.0f);
    }

    void OnEvent(const Event& e, GIDXEngine& engine)
    {
        std::visit([&](auto&& ev)
        {
            using T = std::decay_t<decltype(ev)>;
            if constexpr (std::is_same_v<T, KeyPressedEvent>)
                if (!ev.repeat && ev.key == Key::Escape)
                    engine.Shutdown();
        }, e);
    }

private:
    EntityID MakeEntity(const char* name, MeshHandle mesh, MaterialHandle mat,
                        const Float3& pos, const Float3& scale)
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
        reg.Add<VisibilityComponent>(e);
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
    EntityID m_colorCube = NULL_ENTITY, m_plainCube = NULL_ENTITY;
    MeshHandle m_hColorCube, m_hPlainCube;
    MaterialHandle m_hWhite, m_hGray, m_hRed;
    float m_yawA = 0.0f, m_yawB = 0.0f;
};

int main()
{
    GDXEventQueue events;
    WindowDesc desc;
    desc.width = 1280;
    desc.height = 720;
    desc.title = "GIDX - Test VertexColor";
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

    VertexColorTest app(*renderer);
    app.Init();
    renderer->SetTickCallback([&](float dt){ app.Update(dt); });
    engine.SetEventCallback([&](const Event& e){ app.OnEvent(e, engine); });
    engine.Run();
    engine.Shutdown();
    return 0;
}
