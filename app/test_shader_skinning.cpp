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
#include <DirectXMath.h>

static SubmeshData MakeSkinnedStrip()
{
    SubmeshData s;
    s.positions = {
        { -0.35f, -1.0f, 0.0f },
        {  0.35f, -1.0f, 0.0f },
        { -0.35f,  0.0f, 0.0f },
        {  0.35f,  0.0f, 0.0f },
        { -0.35f,  1.0f, 0.0f },
        {  0.35f,  1.0f, 0.0f },
    };
    s.normals = {
        {0,0,-1},{0,0,-1},{0,0,-1},{0,0,-1},{0,0,-1},{0,0,-1}
    };
    s.uv0 = {
        {0,1},{1,1},{0,0.5f},{1,0.5f},{0,0},{1,0}
    };
    s.indices = {
        0,2,1, 1,2,3,
        2,4,3, 3,4,5
    };
    s.boneIndices = {
        {0,0,0,0}, {0,0,0,0},
        {0,1,0,0}, {0,1,0,0},
        {1,0,0,0}, {1,0,0,0}
    };
    s.boneWeights = {
        {1,0,0,0}, {1,0,0,0},
        {0.5f,0.5f,0,0}, {0.5f,0.5f,0,0},
        {1,0,0,0}, {1,0,0,0}
    };
    return s;
}

class SkinningTest
{
public:
    explicit SkinningTest(GDXECSRenderer& renderer) : m_renderer(renderer) {}

    void Init()
    {
        Registry& reg = m_renderer.GetRegistry();

        {
            MeshAssetResource asset;
            asset.debugName = "SkinnedStrip";
            asset.AddSubmesh(MakeSkinnedStrip());
            m_hStrip = m_renderer.UploadMesh(std::move(asset));
        }
        {
            MeshAssetResource asset;
            asset.debugName = "Cube";
            asset.AddSubmesh(BuiltinMeshes::Cube());
            m_hCube = m_renderer.UploadMesh(std::move(asset));
        }

        m_hSkin = m_renderer.CreateMaterial(MaterialResource::FlatColor(0.25f, 0.85f, 0.35f, 1.0f));
        m_hFloor = m_renderer.CreateMaterial(MaterialResource::FlatColor(0.42f, 0.42f, 0.45f, 1.0f));
        m_hCubeMat = m_renderer.CreateMaterial(MaterialResource::FlatColor(0.85f, 0.20f, 0.15f, 1.0f));

        m_camera = reg.CreateEntity();
        reg.Add<TagComponent>(m_camera, "Camera");
        {
            TransformComponent tc;
            tc.localPosition = { 0.0f, 2.8f, -8.5f };
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
            lc.diffuseColor = { 1,1,1,1 };
            lc.intensity = 2.5f;
            lc.castShadows = true;
            lc.shadowOrthoSize = 24.0f;
            lc.shadowNear = 0.1f;
            lc.shadowFar = 1000.0f;
            reg.Add<LightComponent>(m_light, lc);
        }
        {
            TransformComponent tc;
            tc.localPosition = { 0.0f, 0.0f, 0.0f };
            tc.SetEulerDeg(45.0f,0.0f, 0.0f);
            reg.Add<TransformComponent>(m_light, tc);
        }
        reg.Add<WorldTransformComponent>(m_light);
        m_renderer.SetSceneAmbient(0.08f, 0.08f, 0.10f);

        m_floor = MakeEntity("Floor", m_hCube, m_hFloor,
            { 0.0f, -1.5f, 2.0f }, { 8.0f, 0.4f, 8.0f }, true);

        m_refCube = MakeEntity("ReferenceCube", m_hCube, m_hCubeMat,
            { 2.2f, 0.0f, 2.8f }, { 1.2f, 1.2f, 1.2f }, true);

        m_skinned = MakeEntity("SkinnedStrip", m_hStrip, m_hSkin,
            { -1.5f, 0.0f, 2.4f }, { 1.5f, 1.5f, 1.5f }, true);

        {
            SkinComponent skin;
            skin.finalBoneMatrices.resize(2);
            skin.finalBoneMatrices[0] = GIDX::Identity4x4();
            skin.finalBoneMatrices[1] = GIDX::Identity4x4();
            reg.Add<SkinComponent>(m_skinned, std::move(skin));
        }

        Debug::Log("skinning_test: gruenes Band muss sich biegen; ESC beendet");
    }

    void Update(float dt)
    {
        Registry& reg = m_renderer.GetRegistry();
        m_time += dt;

        if (auto* tc = reg.Get<TransformComponent>(m_refCube))
            tc->SetEulerDeg(0.0f, m_time * 35.0f, 0.0f);

        auto* skin = reg.Get<SkinComponent>(m_skinned);
        if (!skin || skin->finalBoneMatrices.size() < 2)
            return;

        skin->finalBoneMatrices[0] = GIDX::Identity4x4();

        const float angle = std::sinf(m_time * 1.8f) * 0.85f;
        const DirectX::XMMATRIX bone1 =
            DirectX::XMMatrixRotationZ(angle);
        GDXMathHelpers::StoreFloat4x4(skin->finalBoneMatrices[1], bone1);
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
        reg.Add<RenderableComponent>(e, mesh, mat, 0u);
        {
            VisibilityComponent vis;
            vis.castShadows = castShadows;
            reg.Add<VisibilityComponent>(e, vis);
        }
        return e;
    }

private:
    GDXECSRenderer& m_renderer;
    EntityID m_camera = NULL_ENTITY, m_light = NULL_ENTITY;
    EntityID m_floor = NULL_ENTITY, m_refCube = NULL_ENTITY, m_skinned = NULL_ENTITY;
    MeshHandle m_hStrip, m_hCube;
    MaterialHandle m_hSkin, m_hFloor, m_hCubeMat;
    float m_time = 0.0f;
};

int main()
{
    GDXEventQueue events;
    WindowDesc desc;
    desc.width = 1280;
    desc.height = 720;
    desc.title = "GIDX - Test Skinning";
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

    SkinningTest app(*renderer);
    app.Init();
    renderer->SetTickCallback([&](float dt){ app.Update(dt); });
    engine.SetEventCallback([&](const Event& e){ app.OnEvent(e, engine); });
    engine.Run();
    engine.Shutdown();
    return 0;
}
