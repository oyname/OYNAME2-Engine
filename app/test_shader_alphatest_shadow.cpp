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

#include <memory>
#include <variant>
#include <filesystem>

static bool FileExists(const std::wstring& path)
{
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

static SubmeshData MakeCutoutQuad()
{
    SubmeshData s;
    s.positions = {
        { -1.5f, 0.0f, 0.0f },
        { -1.5f, 3.0f, 0.0f },
        {  1.5f, 0.0f, 0.0f },
        {  1.5f, 3.0f, 0.0f },
    };

    s.normals = {
        { 0.0f, 0.0f, 1.0f },
        { 0.0f, 0.0f, 1.0f },
        { 0.0f, 0.0f, 1.0f },
        { 0.0f, 0.0f, 1.0f },
    };

    s.uv0 = {
        { 0.0f, 1.0f },
        { 0.0f, 0.0f },
        { 1.0f, 1.0f },
        { 1.0f, 0.0f },
    };

    s.indices = { 0, 2, 1, 2, 3, 1 };
    return s;
}

class AlphaTestShadowTest
{
public:
    explicit AlphaTestShadowTest(GDXECSRenderer& renderer) : m_renderer(renderer) {}

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
            asset.debugName = "CutoutQuad";
            asset.AddSubmesh(MakeCutoutQuad());
            m_hQuad = m_renderer.UploadMesh(std::move(asset));
        }

        m_hFloor = m_renderer.CreateMaterial(MaterialResource::FlatColor(0.42f, 0.42f, 0.45f));
        m_hRed = m_renderer.CreateMaterial(MaterialResource::FlatColor(0.82f, 0.18f, 0.14f));

        MaterialResource cutout = MaterialResource::FlatColor(1.0f, 1.0f, 1.0f, 1.0f);
        cutout.data.flags = MF_ALPHA_TEST ;
        cutout.data.alphaCutoff = 0.5f;
        cutout.data.receiveShadows = 1.0f;

        const std::wstring maskPath = L"..//media//alpha_mask.png";
        if (FileExists(maskPath))
            cutout.SetTexture(MaterialTextureSlot::Albedo, m_renderer.LoadTexture(maskPath, true));
        m_hCutout = m_renderer.CreateMaterial(cutout);

        m_camera = reg.CreateEntity();
        reg.Add<TagComponent>(m_camera, "Camera");
        {
            TransformComponent tc;
            tc.localPosition = { 0.0f, 3.5f, -10.0f };
            tc.SetEulerDeg(12.0f, 0.0f, 0.0f);
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
            lc.diffuseColor = { 1.0f, 0.98f, 0.94f, 1.0f };
            lc.intensity = 2.0f;
            lc.castShadows = true;
            lc.shadowOrthoSize = 28.0f;
            lc.shadowNear = 0.1f;
            lc.shadowFar = 1000.0f;
            reg.Add<LightComponent>(m_light, lc);
        }
        {
            TransformComponent tc;
            tc.localPosition = { 0.0f, 0.0f, 0.0f };
            tc.SetEulerDeg(45.0f, 0.0f, 0.0f);
            reg.Add<TransformComponent>(m_light, tc);
        }
        reg.Add<WorldTransformComponent>(m_light);
        m_renderer.SetSceneAmbient(0.08f, 0.08f, 0.10f);

        m_floor = MakeEntity("Floor", m_hCube, m_hFloor,
            { 0.0f, -1.5f, 2.0f }, { 10.0f, 0.4f, 10.0f }, true);

        m_cutout = MakeEntity("CutoutQuad", m_hQuad, m_hCutout,
            { 0.0f, -0.5f, 1.3f }, { 1.0f, 1.0f, 1.0f }, true);

        m_cube = MakeEntity("OpaqueCube", m_hCube, m_hRed,
            { 0.0f, 2.0f, 4.2f }, { 3.5f, 3.5f, 3.5f }, true);

        Debug::Log("alphatest_shadow_test: alpha_mask.png in ..//media legen; ESC beendet");

        m_spin = 180.0f;

        if (auto* tc = reg.Get<TransformComponent>(m_cutout))
            tc->SetEulerDeg(0.0f, m_spin, 0.0f);

    }

    void Update(float dt)
    {
        Registry& reg = m_renderer.GetRegistry();
        m_spin += 20.0f * dt;
        Wrap(m_spin);
        if (auto* tc = reg.Get<TransformComponent>(m_cutout))
            tc->SetEulerDeg(0.0f, m_spin, 0.0f);
    }

    void OnEvent(const Event& e, GDXEngine& engine)
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
        const Float3& pos, const Float3& scale,
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
        reg.Add<MeshRefComponent>(e, mesh, 0u);
        reg.Add<MaterialRefComponent>(e, mat);
        {
            VisibilityComponent vis;
            vis.castShadows = castShadows;
            reg.Add<VisibilityComponent>(e, vis);
        }
        if (castShadows)
            reg.Add<ShadowCasterTag>(e);
        return e;
    }

    static void Wrap(float& v)
    {
        while (v >= 360.0f) v -= 360.0f;
        while (v < 0.0f) v += 360.0f;
    }

private:
    GDXECSRenderer& m_renderer;
    EntityID m_camera = NULL_ENTITY, m_light = NULL_ENTITY;
    EntityID m_floor = NULL_ENTITY, m_cutout = NULL_ENTITY, m_cube = NULL_ENTITY;
    MeshHandle m_hCube, m_hQuad;
    MaterialHandle m_hFloor, m_hRed, m_hCutout;
    float m_spin = 0.0f;
};

int main()
{
    GDXEventQueue events;
    WindowDesc desc;
    desc.width = 1280;
    desc.height = 720;
    desc.title = "GIDX - Test AlphaTest + Shadow";
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

    GDXEngine engine(std::move(windowOwned), std::move(rendererOwned), events);
    if (!engine.Initialize()) return 4;

    AlphaTestShadowTest app(*renderer);
    app.Init();
    renderer->SetTickCallback([&](float dt) { app.Update(dt); });
    engine.SetEventCallback([&](const Event& e) { app.OnEvent(e, engine); });
    engine.Run();
    engine.Shutdown();
    return 0;
}