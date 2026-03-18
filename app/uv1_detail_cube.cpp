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

static bool FileExists(const std::wstring& path)
{
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

// -----------------------------------------------------------------------------
// Wrfel mit uv0 + uv1
//
// uv0: normale Face-UVs 0..1
// uv1: Detail-UVs strker gekachelt
// -----------------------------------------------------------------------------
static SubmeshData BuildCubeUV0UV1(float detailTile = 1.0f)
{
    SubmeshData s;

    auto addFace = [&](Float3 bl,
        Float3 tl,
        Float3 tr,
        Float3 br,
        Float3 n)
        {
            const uint32_t base = static_cast<uint32_t>(s.positions.size());

            s.positions.push_back(bl);
            s.positions.push_back(tl);
            s.positions.push_back(tr);
            s.positions.push_back(br);

            for (int i = 0; i < 4; ++i)
                s.normals.push_back(n);

            // uv0  Basis-Textur (0..1 pro Face)
            s.uv0.push_back({ 0.0f, 1.0f });
            s.uv0.push_back({ 0.0f, 0.0f });
            s.uv0.push_back({ 1.0f, 0.0f });
            s.uv0.push_back({ 1.0f, 1.0f });

            // uv1  Detail-Textur (strker gekachelt)
            s.uv1.push_back({ 0.0f,       detailTile });
            s.uv1.push_back({ 0.0f,       0.0f });
            s.uv1.push_back({ detailTile, 0.0f });
            s.uv1.push_back({ detailTile, detailTile });

            s.indices.push_back(base + 0);
            s.indices.push_back(base + 2);
            s.indices.push_back(base + 1);

            s.indices.push_back(base + 0);
            s.indices.push_back(base + 3);
            s.indices.push_back(base + 2);
        };

    const float h = 0.5f;

    addFace({ -h, -h,  h }, { -h,  h,  h }, { h,  h,  h }, { h, -h,  h }, { 0,  0,  1 }); // Front +Z
    addFace({ h, -h, -h }, { h,  h, -h }, { -h,  h, -h }, { -h, -h, -h }, { 0,  0, -1 }); // Back  -Z
    addFace({ -h, -h, -h }, { -h,  h, -h }, { -h,  h,  h }, { -h, -h,  h }, { -1,  0,  0 }); // Left  -X
    addFace({ h, -h,  h }, { h,  h,  h }, { h,  h, -h }, { h, -h, -h }, { 1,  0,  0 }); // Right +X
    addFace({ -h,  h,  h }, { -h,  h, -h }, { h,  h, -h }, { h,  h,  h }, { 0,  1,  0 }); // Top   +Y
    addFace({ -h, -h, -h }, { -h, -h,  h }, { h, -h,  h }, { h, -h, -h }, { 0, -1,  0 }); // Bot   -Y

    return s;
}

class UV1DetailDemo
{
public:
    explicit UV1DetailDemo(GDXECSRenderer& renderer)
        : m_renderer(renderer)
    {
    }

    void Init()
    {
        Registry& reg = m_renderer.GetRegistry();

        // ---------------------------------------------------------------------
        // Mesh  uv0 + uv1, detailTile=4 (UV1 ist 4 strker gekachelt)
        // ---------------------------------------------------------------------
        {
            MeshAssetResource asset;
            asset.debugName = "Cube_UV0_UV1";
            asset.AddSubmesh(BuildCubeUV0UV1(1.0f));
            m_hCube = m_renderer.UploadMesh(std::move(asset));
        }

        // ---------------------------------------------------------------------
        // Texturen
        // ---------------------------------------------------------------------
        TextureHandle hAlbedo = TextureHandle::Invalid();
        if (FileExists(L"..//media//albedo.png"))
            hAlbedo = m_renderer.LoadTexture(L"..//media//albedo.png", true);

        m_hDetailA = TextureHandle::Invalid();
        m_hDetailB = TextureHandle::Invalid();

        if (FileExists(L"..//media//orm.png"))
            m_hDetailA = m_renderer.LoadTexture(L"..//media//orm.png", true);

        if (FileExists(L"..//media//bricks.bmp"))
            m_hDetailB = m_renderer.LoadTexture(L"..//media//bricks.bmp", true);

        // ---------------------------------------------------------------------
        // Material
        //
        // MF_USE_DETAIL_MAP aktiviert den UV1-Pfad im Pixel Shader.
        // detailTex landet auf t4, wird mit UV1 abgetastet (2x Multiply).
        // Kein echtes Detail-Bild vorhanden? Flag einfach weglassen 
        // UV1 wird dann vom Executor still auf UV0 aliasiert, kein Crash.
        // ---------------------------------------------------------------------
        {
            MaterialResource mat = MaterialResource::FlatColor(1, 1, 1, 1);
            mat.SetTexture(MaterialTextureSlot::Albedo, hAlbedo, MaterialTextureUVSet::UV0);

            // Detail-Textur auf dem richtigen Slot setzen
            const TextureHandle firstDetail = m_hDetailA.IsValid() ? m_hDetailA : m_hDetailB;
            if (firstDetail.IsValid())
            {
                mat.SetTexture(MaterialTextureSlot::Detail, firstDetail, MaterialTextureUVSet::UV1);
                mat.SetFlag(MF_USE_DETAIL_MAP, true);
                // UV1-Tiling wird hier ber den cbuffer unabhngig von den
                // Vertex-UVs zustzlich skaliert (1:1 = kein Extra-Tiling).
                mat.SetDetailTiling(1.0f, 1.0f);
            }

            mat.data.receiveShadows = 1.0f;

            m_hMat = m_renderer.CreateMaterial(mat);
        }

        // ---------------------------------------------------------------------
        // Kamera
        // ---------------------------------------------------------------------
        m_camera = reg.CreateEntity();
        reg.Add<TagComponent>(m_camera, "Camera");

        {
            TransformComponent tc;
            tc.localPosition = { 0.0f, 1.8f, -4.5f };
            tc.SetEulerDeg(10.0f, 0.0f, 0.0f);
            reg.Add<TransformComponent>(m_camera, tc);
        }
        reg.Add<WorldTransformComponent>(m_camera);

        {
            CameraComponent cam;
            cam.aspectRatio = 1280.0f / 720.0f;
            cam.nearPlane = 0.1f;
            cam.farPlane = 100.0f;
            cam.fovDeg = 60.0f;
            reg.Add<CameraComponent>(m_camera, cam);
        }
        reg.Add<ActiveCameraTag>(m_camera);

        // ---------------------------------------------------------------------
        // Licht
        // ---------------------------------------------------------------------
        m_light = reg.CreateEntity();
        reg.Add<TagComponent>(m_light, "Sun");

        {
            LightComponent lc;
            lc.kind = LightKind::Directional;
            lc.diffuseColor = { 1.0f, 0.98f, 0.94f, 1.0f };
            lc.intensity = 2.0f;
            lc.castShadows = true;
            lc.shadowOrthoSize = 20.0f;
            lc.shadowNear = 0.1f;
            lc.shadowFar = 100.0f;
            reg.Add<LightComponent>(m_light, lc);
        }

        {
            TransformComponent tc;
            tc.localPosition = { 0.0f, 0.0f, 0.0f };
            tc.SetEulerDeg(-45.0f,0.0f, 0.0f);
            reg.Add<TransformComponent>(m_light, tc);
        }
        reg.Add<WorldTransformComponent>(m_light);

        m_renderer.SetSceneAmbient(0.08f, 0.08f, 0.10f);

        // ---------------------------------------------------------------------
        // Wrfel
        // ---------------------------------------------------------------------
        m_cube = reg.CreateEntity();
        reg.Add<TagComponent>(m_cube, "UV1 Detail Cube");

        {
            TransformComponent tc;
            tc.localPosition = { 0.0f, 0.0f, 0.0f };
            tc.localScale = { 2.0f, 2.0f, 2.0f };
            reg.Add<TransformComponent>(m_cube, tc);
        }
        reg.Add<WorldTransformComponent>(m_cube);
        reg.Add<RenderableComponent>(m_cube, m_hCube, m_hMat, 0u);

        {
            VisibilityComponent vis;
            vis.visible = true;
            vis.active = true;
            vis.castShadows = true;
            reg.Add<VisibilityComponent>(m_cube, vis);
        }

        Debug::Log("UV1DetailDemo: Space = Detail-Textur wechseln");
    }

    void Update(float dt)
    {
        Registry& reg = m_renderer.GetRegistry();

        m_yaw += 35.0f * dt;
        if (m_yaw >= 360.0f) m_yaw -= 360.0f;

        if (auto* tc = reg.Get<TransformComponent>(m_cube))
            tc->SetEulerDeg(20.0f, m_yaw, 0.0f);
    }

    void OnEvent(const Event& e, GDXEngine& engine)
    {
        std::visit([&](auto&& ev)
            {
                using T = std::decay_t<decltype(ev)>;

                if constexpr (std::is_same_v<T, KeyPressedEvent>)
                {
                    if (ev.repeat) return;

                    if (ev.key == Key::Escape)
                        engine.Shutdown();
                    else if (ev.key == Key::Space)
                        ToggleDetailTexture();
                }
            }, e);
    }

private:
    void ToggleDetailTexture()
    {
        if (!m_hDetailA.IsValid() || !m_hDetailB.IsValid()) return;

        MaterialResource* mat = m_renderer.GetMatStore().Get(m_hMat);
        if (!mat) return;

        m_useDetailA = !m_useDetailA;
        const TextureHandle nextDetail = m_useDetailA ? m_hDetailA : m_hDetailB;
        mat->SetTexture(MaterialTextureSlot::Detail, nextDetail, MaterialTextureUVSet::UV1);

        Debug::Log("UV1DetailDemo: detail = ",
            m_useDetailA ? "orm.png" : "bricks.bmp");
    }

private:
    GDXECSRenderer& m_renderer;

    EntityID m_camera = NULL_ENTITY;
    EntityID m_light = NULL_ENTITY;
    EntityID m_cube = NULL_ENTITY;

    MeshHandle     m_hCube;
    MaterialHandle m_hMat;
    TextureHandle  m_hDetailA;
    TextureHandle  m_hDetailB;
    bool           m_useDetailA = true;

    float m_yaw = 0.0f;
};

int main()
{
    GDXEventQueue events;

    WindowDesc desc;
    desc.width = 1280;
    desc.height = 720;
    desc.title = "GIDX - UV1 Detail Texture Example";
    desc.resizable = true;

    auto windowOwned = std::make_unique<GDXWin32Window>(desc, events);
    if (!windowOwned->Create())
    {
        Debug::LogError("uv1_detail_cube.cpp: Fenster konnte nicht erstellt werden");
        return 1;
    }

    auto adapters = GDXWin32DX11ContextFactory::EnumerateAdapters();
    if (adapters.empty())
    {
        Debug::LogError("uv1_detail_cube.cpp: kein DX11-Adapter gefunden");
        return 2;
    }

    const unsigned int adapterIdx =
        GDXWin32DX11ContextFactory::FindBestAdapter(adapters);

    GDXWin32DX11ContextFactory dx11Factory;
    auto dxContext = dx11Factory.Create(*windowOwned, adapterIdx);
    if (!dxContext)
    {
        Debug::LogError("uv1_detail_cube.cpp: DX11 Context konnte nicht erstellt werden");
        return 3;
    }

    auto backendOwned = std::make_unique<GDXDX11RenderBackend>(std::move(dxContext));
    auto rendererOwned = std::make_unique<GDXECSRenderer>(std::move(backendOwned));
    GDXECSRenderer* renderer = rendererOwned.get();

    renderer->SetClearColor(0.04f, 0.05f, 0.08f);

    GDXEngine engine(std::move(windowOwned), std::move(rendererOwned), events);
    if (!engine.Initialize())
    {
        Debug::LogError("uv1_detail_cube.cpp: Engine-Initialisierung fehlgeschlagen");
        return 4;
    }

    UV1DetailDemo app(*renderer);
    app.Init();

    renderer->SetTickCallback([&](float dt) { app.Update(dt); });
    engine.SetEventCallback([&](const Event& e) { app.OnEvent(e, engine); });

    engine.Run();
    engine.Shutdown();
    return 0;
}