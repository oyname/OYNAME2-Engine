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
#include <vector>
#include <cmath>
#include <DirectXMath.h>

// -----------------------------------------------------------------------------
// Mesh-Parameter
// -----------------------------------------------------------------------------
static constexpr int   N_BONES = 6;
static constexpr float SEG_H = 1.5f;
static constexpr float HALF_W = 0.38f;
static constexpr float HALF_D = 0.22f;
static constexpr int   N_RINGS = 2 * N_BONES + 1;   // 13
static constexpr int   VERTS_RING = 4;

// -----------------------------------------------------------------------------
// BuildTailSubmesh
//
// Segmentiertes "Rohr" entlang +Y mit 4 Ecken pro Ring.
// Skinning-Daten werden direkt in SubmeshData geschrieben.
// -----------------------------------------------------------------------------
static SubmeshData BuildTailSubmesh()
{
    SubmeshData s;

    s.positions.reserve(N_RINGS * VERTS_RING);
    s.normals.reserve(N_RINGS * VERTS_RING);
    s.colors.reserve(N_RINGS * VERTS_RING);
    s.uv0.reserve(N_RINGS * VERTS_RING);
    s.boneIndices.reserve(N_RINGS * VERTS_RING);
    s.boneWeights.reserve(N_RINGS * VERTS_RING);
    s.indices.reserve((N_RINGS - 1) * VERTS_RING * 12);

    const float cx[VERTS_RING] = { -HALF_W,  HALF_W,  HALF_W, -HALF_W };
    const float cz[VERTS_RING] = { -HALF_D, -HALF_D,  HALF_D,  HALF_D };
    const float nx[VERTS_RING] = { -0.707f,  0.707f,  0.707f, -0.707f };
    const float nz[VERTS_RING] = { -0.707f, -0.707f,  0.707f,  0.707f };

    // ---- Vertices ----------------------------------------------------------
    for (int r = 0; r < N_RINGS; ++r)
    {
        const float t = static_cast<float>(r) * 0.5f;
        const float y = t * SEG_H;
        const float gradient = y / (2 * SEG_H);  // 0..1 — nur noch für Farbe

        const float cr = (20.0f + 30.0f * gradient) / 255.0f;
        const float cg = (60.0f + 195.0f * gradient) / 255.0f;
        const float cb = (140.0f + 115.0f * gradient) / 255.0f;

        for (int c = 0; c < VERTS_RING; ++c)
        {
            s.positions.push_back({ cx[c], y, cz[c] });
            s.normals.push_back({ nx[c], 0.0f, nz[c] });
            s.colors.push_back({ cr, cg, cb, 1.0f });

            const float u = static_cast<float>(c) / static_cast<float>(VERTS_RING - 1);
            s.uv0.push_back({ u, gradient });
        }
    }

    // ---- Bone-Weights ------------------------------------------------------
    for (int r = 0; r < N_RINGS; ++r)
    {
        const int  boneBase = r / 2;
        const bool isBoundary = ((r % 2) == 0);

        for (int c = 0; c < VERTS_RING; ++c)
        {
            if (isBoundary)
            {
                if (boneBase == 0)
                {
                    s.boneIndices.push_back({ 0, 0, 0, 0 });
                    s.boneWeights.push_back({ 1.0f, 0.0f, 0.0f, 0.0f });
                }
                else if (boneBase >= N_BONES)
                {
                    s.boneIndices.push_back({ N_BONES - 1, 0, 0, 0 });
                    s.boneWeights.push_back({ 1.0f, 0.0f, 0.0f, 0.0f });
                }
                else
                {
                    s.boneIndices.push_back({ static_cast<uint32_t>(boneBase - 1), static_cast<uint32_t>(boneBase), 0, 0 });
                    s.boneWeights.push_back({ 0.5f, 0.5f, 0.0f, 0.0f });
                }
            }
            else
            {
                s.boneIndices.push_back({ static_cast<uint32_t>(boneBase), 0, 0, 0 });
                s.boneWeights.push_back({ 1.0f, 0.0f, 0.0f, 0.0f });
            }
        }
    }

    // ---- Indices -----------------------------------------------------------
    for (int r = 0; r < N_RINGS - 1; ++r)
    {
        const int b = r * VERTS_RING;
        const int t = (r + 1) * VERTS_RING;

        for (int f = 0; f < VERTS_RING; ++f)
        {
            const int nf = (f + 1) % VERTS_RING;

            // außen
            s.indices.push_back(b + f);
            s.indices.push_back(t + f);
            s.indices.push_back(b + nf);

            s.indices.push_back(t + f);
            s.indices.push_back(t + nf);
            s.indices.push_back(b + nf);

            // innen / Rückseite, damit es von mehreren Blickrichtungen sichtbar bleibt
            s.indices.push_back(b + f);
            s.indices.push_back(b + nf);
            s.indices.push_back(t + f);

            s.indices.push_back(t + f);
            s.indices.push_back(b + nf);
            s.indices.push_back(t + nf);
        }
    }

    return s;
}

class BoneAnimationShowcase
{
public:
    explicit BoneAnimationShowcase(GDXECSRenderer& renderer)
        : m_renderer(renderer)
    {
    }

    void Init()
    {
        Registry& reg = m_renderer.GetRegistry();

        Debug::Log("bone_animation_showcase.cpp: Init() gestartet");

        // ---------------------------------------------------------------------
        // Meshes
        // ---------------------------------------------------------------------
        {
            MeshAssetResource asset;
            asset.debugName = "GroundCube";
            asset.AddSubmesh(BuiltinMeshes::Cube());
            m_hCube = m_renderer.UploadMesh(std::move(asset));
        }
        {
            MeshAssetResource asset;
            asset.debugName = "TailSkinned";
            asset.AddSubmesh(BuildTailSubmesh());
            m_hTail = m_renderer.UploadMesh(std::move(asset));
        }

        // ---------------------------------------------------------------------
        // Texturen (optional)
        // ---------------------------------------------------------------------
        TextureHandle albedoTex = TextureHandle::Invalid();
        TextureHandle normalTex = TextureHandle::Invalid();
        TextureHandle ormTex = TextureHandle::Invalid();

        albedoTex = m_renderer.LoadTexture(L"..//media//albedo.png", true);
        normalTex = m_renderer.LoadTexture(L"..//media//normal.png", false);
        ormTex = m_renderer.LoadTexture(L"..//media//orm.png", false);

        // ---------------------------------------------------------------------
        // Materialien
        // ---------------------------------------------------------------------
        {
            MaterialResource mat = MaterialResource::FlatColor(0.18f, 0.18f, 0.26f, 1.0f);
            mat.SetTexture(MaterialTextureSlot::Albedo, albedoTex);
            mat.SetTexture(MaterialTextureSlot::Normal, normalTex);
            mat.SetTexture(MaterialTextureSlot::ORM, ormTex);

            mat.data.flags =
                MF_SHADING_PBR |
                MF_USE_NORMAL_MAP |
                MF_USE_ORM_MAP;

            mat.data.metallic = 0.0f;
            mat.data.roughness = 0.85f;
            mat.data.receiveShadows = 1.0f;
            m_hGroundMat = m_renderer.CreateMaterial(mat);
        }

        {
            MaterialResource mat = MaterialResource::FlatColor(1.0f, 1.0f, 1.0f, 1.0f);
            mat.SetTexture(MaterialTextureSlot::Albedo, albedoTex);
            mat.SetTexture(MaterialTextureSlot::Normal, normalTex);
            mat.SetTexture(MaterialTextureSlot::ORM, ormTex);

            mat.data.metallic = 0.0f;
            mat.data.roughness = 0.55f;
            mat.data.receiveShadows = 1.0f;

            mat.data.baseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
            mat.data.metallic = 0.0f;   // Fallback wenn keine ORM
            mat.data.roughness = 0.8f;   // Fallback wenn keine ORM
            mat.data.normalScale = 1.0f;
            mat.data.occlusionStrength = 1.0f;
            mat.data.receiveShadows = 1.0f;
            mat.data.uvTilingOffset = { 0.5f, 0.5f, 0.0f, 0.0f };

            mat.data.flags =
                MF_SHADING_PBR |
                MF_USE_NORMAL_MAP |
                MF_USE_ORM_MAP;

            m_hTailMat = m_renderer.CreateMaterial(mat);
        }

        // ---------------------------------------------------------------------
        // Kamera
        // ---------------------------------------------------------------------
        m_camera = reg.CreateEntity();
        reg.Add<TagComponent>(m_camera, "Camera");

        {
            TransformComponent tc;
            tc.localPosition = { 0.0f, 5.0f, -12.0f };
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
        // Directional Light
        // ---------------------------------------------------------------------
        m_light = reg.CreateEntity();
        reg.Add<TagComponent>(m_light, "Sun");

        {
            LightComponent lc;
            lc.kind = LightKind::Directional;
            lc.diffuseColor = { 1.0f, 0.95f, 0.85f, 1.0f };
            lc.intensity = 2.2f;
            lc.castShadows = true;
            lc.shadowOrthoSize = 16.0f;
            lc.shadowNear = 0.5f;
            lc.shadowFar = 1000.0f;
            reg.Add<LightComponent>(m_light, lc);
        }
        {
            TransformComponent tc;
            tc.localPosition = { 0.0f, 0.0f, 0.0f };
            tc.SetEulerDeg(55.0f, 0.0f, 0.0f);
            reg.Add<TransformComponent>(m_light, tc);
        }
        reg.Add<WorldTransformComponent>(m_light);

        m_renderer.SetSceneAmbient(0.62f, 0.65f, 0.72f);

        // ---------------------------------------------------------------------
        // Boden
        // ---------------------------------------------------------------------
        m_ground = MakeEntity(
            "Ground",
            m_hCube,
            m_hGroundMat,
            { 0.0f, -0.5f, 0.0f },
            { 12.0f, 0.4f, 12.0f },
            false);

        // ---------------------------------------------------------------------
        // Skinned Tail
        // ---------------------------------------------------------------------
        m_tail = MakeEntity(
            "TailMesh",
            m_hTail,
            m_hTailMat,
            { 0.0f, 0.0f, 0.0f },
            { 1.0f, 1.0f, 1.0f },
            true);

        // Skin-Komponente
        {
            SkinComponent skin;
            skin.enabled = true;
            skin.finalBoneMatrices.resize(N_BONES);

            for (int i = 0; i < N_BONES; ++i)
                DirectX::XMStoreFloat4x4(&skin.finalBoneMatrices[i], DirectX::XMMatrixIdentity());

            reg.Add<SkinComponent>(m_tail, std::move(skin));
        }

        // Inverse Bind Poses
        m_invBind.resize(N_BONES);
        for (int i = 0; i < N_BONES; ++i)
        {
            m_invBind[i] = DirectX::XMMatrixTranslation(
                0.0f, -static_cast<float>(i) * SEG_H, 0.0f);
        }

        Debug::Log("bone_animation_showcase.cpp: Szene aufgebaut");
    }

    void Update(float dt)
    {
        Registry& reg = m_renderer.GetRegistry();

        m_timeAcc += dt;
        m_camAngle += 6.0f * dt;

        // ---------------------------------------------------------------------
        // Kamera kreist
        // ---------------------------------------------------------------------
        if (auto* tc = reg.Get<TransformComponent>(m_camera))
        {
            const float camRad = m_camAngle * DirectX::XM_PI / 180.0f;
            tc->localPosition.x = 10.0f * std::sin(camRad);
            tc->localPosition.y = 5.0f;
            tc->localPosition.z = -10.0f * std::cos(camRad);

            // grob zur Mitte schauen
            tc->SetEulerDeg(15.0f, -m_camAngle, 0.0f);
        }

        // ---------------------------------------------------------------------
        // Bone Animation
        // ---------------------------------------------------------------------
        auto* skin = reg.Get<SkinComponent>(m_tail);
        if (!skin || skin->finalBoneMatrices.size() < N_BONES)
            return;

        DirectX::XMMATRIX boneWorld[N_BONES];
        DirectX::XMMATRIX finalMats[N_BONES];

        constexpr float WAVE_AMP = 0.30f;
        constexpr float WAVE_FREQ = 1.8f;
        constexpr float WAVE_AMP_Z = 0.20f;
        constexpr float WAVE_FREQ_Z = 1.3f;
        const float phaseStep = DirectX::XM_PI / static_cast<float>(N_BONES);

        const float ax0 = WAVE_AMP * std::sin(m_timeAcc * WAVE_FREQ);
        const float az0 = WAVE_AMP_Z * std::sin(m_timeAcc * WAVE_FREQ_Z + DirectX::XM_PIDIV2);

        boneWorld[0] =
            DirectX::XMMatrixRotationX(ax0) *
            DirectX::XMMatrixRotationZ(az0);

        for (int i = 1; i < N_BONES; ++i)
        {
            const float phi = i * phaseStep;
            const float axi = WAVE_AMP * std::sin(m_timeAcc * WAVE_FREQ + phi);
            const float azi = WAVE_AMP_Z * std::sin(m_timeAcc * WAVE_FREQ_Z + phi + DirectX::XM_PIDIV2);

            boneWorld[i] =
                DirectX::XMMatrixTranslation(0.0f, SEG_H, 0.0f) *
                DirectX::XMMatrixRotationX(axi) *
                DirectX::XMMatrixRotationZ(azi) *
                boneWorld[i - 1];
        }

        for (int i = 0; i < N_BONES; ++i)
        {
            finalMats[i] = m_invBind[i] * boneWorld[i];
            DirectX::XMStoreFloat4x4(&skin->finalBoneMatrices[i], finalMats[i]);
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
    EntityID MakeEntity(
        const char* name,
        MeshHandle mesh,
        MaterialHandle mat,
        const DirectX::XMFLOAT3& pos,
        const DirectX::XMFLOAT3& scale,
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
            vis.visible = true;
            vis.active = true;
            vis.castShadows = castShadows;
            reg.Add<VisibilityComponent>(e, vis);
        }

        if (castShadows)
            reg.Add<ShadowCasterTag>(e);

        return e;
    }

private:
    GDXECSRenderer& m_renderer;

    EntityID m_camera = NULL_ENTITY;
    EntityID m_light = NULL_ENTITY;
    EntityID m_ground = NULL_ENTITY;
    EntityID m_tail = NULL_ENTITY;

    MeshHandle m_hCube;
    MeshHandle m_hTail;

    MaterialHandle m_hGroundMat;
    MaterialHandle m_hTailMat;

    std::vector<DirectX::XMMATRIX> m_invBind;

    float m_timeAcc = 0.0f;
    float m_camAngle = 0.0f;
};

int main()
{
    GDXEventQueue events;

    WindowDesc desc;
    desc.width = 1280;
    desc.height = 720;
    desc.title = "GIDX - Bone Animation Showcase";
    desc.resizable = true;

    auto windowOwned = std::make_unique<GDXWin32Window>(desc, events);
    if (!windowOwned->Create())
    {
        Debug::LogError("bone_animation_showcase.cpp: Fenster konnte nicht erstellt werden");
        return 1;
    }

    auto adapters = GDXWin32DX11ContextFactory::EnumerateAdapters();
    if (adapters.empty())
    {
        Debug::LogError("bone_animation_showcase.cpp: kein DX11-Adapter gefunden");
        return 2;
    }

    const unsigned int adapterIdx =
        GDXWin32DX11ContextFactory::FindBestAdapter(adapters);

    GDXWin32DX11ContextFactory dx11Factory;
    auto dxContext = dx11Factory.Create(*windowOwned, adapterIdx);
    if (!dxContext)
    {
        Debug::LogError("bone_animation_showcase.cpp: DX11 Context konnte nicht erstellt werden");
        return 3;
    }

    auto backendOwned = std::make_unique<GDXDX11RenderBackend>(std::move(dxContext));
    auto rendererOwned = std::make_unique<GDXECSRenderer>(std::move(backendOwned));
    GDXECSRenderer* renderer = rendererOwned.get();

    renderer->SetClearColor(0.02f, 0.03f, 0.06f);

    GDXEngine engine(std::move(windowOwned), std::move(rendererOwned), events);

    if (!engine.Initialize())
    {
        Debug::LogError("bone_animation_showcase.cpp: Engine-Initialisierung fehlgeschlagen");
        return 4;
    }

    BoneAnimationShowcase app(*renderer);
    app.Init();

    renderer->SetTickCallback([&](float dt)
        {
            app.Update(dt);
        });

    engine.SetEventCallback([&](const Event& e)
        {
            app.OnEvent(e, engine);
        });

    engine.Run();
    engine.Shutdown();

    Debug::Log("bone_animation_showcase.cpp: main() beendet");
    return 0;
}