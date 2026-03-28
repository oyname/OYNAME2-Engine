#include "scene3.h"
#include "KROMEngine.h"
#include "Core/Debug.h"

#include "MeshAssetResource.h"
#include "MaterialResource.h"
#include "PostProcessResource.h"

#include <filesystem>
#include <variant>
#include <cmath>
#include <system_error>

static void SetLookAt(
    TransformComponent& tc,
    const Float3& position,
    const Float3& target,
    const Float3& upVec = Float3{ 0.0f, 1.0f, 0.0f })
{
    Float3 forward = KROM::Normalize3(KROM::Subtract(target, position), { 0.0f, 0.0f, 1.0f });
    Float3 up = KROM::Normalize3(upVec, { 0.0f, 1.0f, 0.0f });

    float dot = std::fabs(KROM::Dot3(forward, up));
    if (dot > 0.9999f)
    {
        up = { 0.0f, 0.0f, 1.0f };
        dot = std::fabs(KROM::Dot3(forward, up));
        if (dot > 0.9999f)
            up = { 0.0f, 1.0f, 0.0f };
    }

    Float3 right = KROM::Normalize3(KROM::Cross(up, forward), { 1.0f, 0.0f, 0.0f });
    up = KROM::Cross(forward, right);

    tc.localRotation = KROM::QuaternionFromBasis(right, up, forward);
    tc.localPosition = position;
    tc.dirty = true;
}

namespace
{
    static bool FileExists(const std::wstring& path)
    {
        std::error_code ec;
        return std::filesystem::exists(path, ec);
    }

    MaterialHandle CreateFlatFallback(GDXECSRenderer& renderer, float r, float g, float b)
    {
        return renderer.CreateMaterial(MaterialResource::FlatColor(r, g, b, 1.0f));
    }

    MaterialHandle CreateEngineCubeMaterial(GDXECSRenderer& renderer)
    {
        const std::wstring enginePath = L"..\\media\\engine.png";
        if (!FileExists(enginePath))
            return CreateFlatFallback(renderer, 0.85f, 0.45f, 0.15f);

        TextureHandle hAlbedo = renderer.LoadTexture(enginePath, true);

        MaterialResource mat;
        mat.SetTexture(MaterialTextureSlot::Albedo, hAlbedo);
        mat.data.baseColor        = { 1.0f, 1.0f, 1.0f, 1.0f };
        mat.data.shininess        = 0.0f;
        mat.data.emissiveColor    = { 0.1f, 0.1f, 0.1f, 1.0f };
        mat.data.metallic         = 0.0f;
        mat.data.roughness        = 1.0f;
        mat.data.normalScale      = 1.0f;
        mat.data.occlusionStrength = 0.0f;
        mat.data.receiveShadows   = 1.0f;
        mat.data.uvTilingOffset   = { 1.0f, 1.0f, 0.0f, 0.0f };
        mat.data.flags = MF_SHADING_PBR;// | MaterialFlags::MF_USE_EMISSIVE;
        return renderer.CreateMaterial(mat);
    }

    MaterialHandle CreateStoneMaterial(GDXECSRenderer& renderer,
                                       float uvTileX, float uvTileY,
                                       float uvTileXNormal = 1.0f, float uvTileYNormal = 1.0f)
    {
        const std::wstring albedoPath = L"..\\media\\albedo.png";
        const std::wstring normalPath = L"..\\media\\normal.png";
        const std::wstring ormPath    = L"..\\media\\orm.png";

        if (!FileExists(albedoPath))
            return CreateFlatFallback(renderer, 0.55f, 0.55f, 0.58f);

        TextureHandle hAlbedo = renderer.LoadTexture(albedoPath, true);
        TextureHandle hNormal = FileExists(normalPath) ? renderer.LoadTexture(normalPath, false) : TextureHandle::Invalid();
        TextureHandle hORM    = FileExists(ormPath)    ? renderer.LoadTexture(ormPath,    false) : TextureHandle::Invalid();

        MaterialResource mat;
        mat.SetTexture(MaterialTextureSlot::Albedo, hAlbedo);

        uint32_t flags = MF_SHADING_PBR;
        if (hNormal.IsValid()) { mat.SetTexture(MaterialTextureSlot::Normal, hNormal); flags |= MF_USE_NORMAL_MAP; }
        if (hORM.IsValid())    { mat.SetTexture(MaterialTextureSlot::ORM,    hORM); }

        mat.data.baseColor             = { 0.95f, 0.72f, 0.45f, 1.0f };
        mat.data.metallic              = 0.0f;
        mat.data.roughness             = 0.9f;
        mat.data.normalScale           = 3.0f;
        mat.data.occlusionStrength     = 1.0f;
        mat.data.receiveShadows        = 1.0f;
        mat.data.uvTilingOffset        = { uvTileX,       uvTileY,       0.0f, 0.0f };
        mat.data.uvNormalTilingOffset  = { uvTileXNormal, uvTileYNormal, 0.0f, 0.0f };
        mat.data.flags                 = flags;
        return renderer.CreateMaterial(mat);
    }

    MaterialHandle CreateFloorMaterial(GDXECSRenderer& renderer,
                                       float uvTileX, float uvTileY,
                                       float uvTileXNormal = 1.0f, float uvTileYNormal = 1.0f)
    {
        const std::wstring albedoPath = L"..\\media\\albedo.png";
        const std::wstring normalPath = L"..\\media\\normal_bricks.png";
        const std::wstring ormPath    = L"..\\media\\orm.png";

        if (!FileExists(albedoPath))
            return CreateFlatFallback(renderer, 0.55f, 0.55f, 0.58f);

        TextureHandle hAlbedo = renderer.LoadTexture(albedoPath, true);
        TextureHandle hNormal = FileExists(normalPath) ? renderer.LoadTexture(normalPath, false) : TextureHandle::Invalid();
        TextureHandle hORM    = FileExists(ormPath)    ? renderer.LoadTexture(ormPath,    false) : TextureHandle::Invalid();

        MaterialResource mat;
        mat.SetTexture(MaterialTextureSlot::Albedo, hAlbedo);

        uint32_t flags = MF_SHADING_PBR;
        if (hNormal.IsValid()) { mat.SetTexture(MaterialTextureSlot::Normal, hNormal); flags |= MF_USE_NORMAL_MAP; }
        if (hORM.IsValid())    { mat.SetTexture(MaterialTextureSlot::ORM,    hORM);    flags |= MF_USE_ORM_MAP; }

        mat.data.baseColor              = { 1.0f, 1.0f, 1.0f, 1.0f };
        mat.data.metallic               = 0.0f;
        mat.data.roughness              = 0.92f;
        mat.data.normalScale            = 2.2f;
        mat.data.occlusionStrength      = 1.0f;
        mat.data.receiveShadows         = 1.0f;
        mat.data.uvTilingOffset         = { uvTileX,       uvTileY,       0.0f, 0.0f };
        mat.data.uvDetailTilingOffset   = { uvTileX,       uvTileY,       0.0f, 0.0f };
        mat.data.uvNormalTilingOffset   = { uvTileXNormal, uvTileYNormal, 0.0f, 0.0f };
        mat.data.flags                  = flags;
        return renderer.CreateMaterial(mat);
    }

    EntityID MakeRenderableCube(
        Registry& reg,
        MeshHandle mesh,
        MaterialHandle material,
        const char* tag,
        const KROM::Float3& pos,
        const KROM::Float3& scale)
    {
        EntityID e = reg.CreateEntity();
        reg.Add<TagComponent>(e, tag);

        TransformComponent tc;
        tc.localPosition = pos;
        tc.localScale    = scale;
        reg.Add<TransformComponent>(e, tc);

        reg.Add<WorldTransformComponent>(e);
        reg.Add<RenderableComponent>(e, mesh, material, 0u);
        reg.Add<VisibilityComponent>(e);
        return e;
    }

    // Cube with Y-brightness gradient — dark at bottom (contact shadow), bright at top.
    inline SubmeshData GradientCube(float bottomBrightness = 0.08f, float topBrightness = 1.25f)
    {
        SubmeshData s = BuiltinMeshes::Cube();
        s.colors.reserve(s.positions.size());
        for (const auto& pos : s.positions)
        {
            const float t   = pos.y + 0.5f;
            const float lum = bottomBrightness + t * (topBrightness - bottomBrightness);
            s.colors.push_back({ lum, lum, lum, 1.0f });
        }
        return s;
    }
}

void ECSGame::Init()
{
    Registry& reg = m_renderer.GetRegistry();

    {
        MeshAssetResource asset;
        asset.debugName = "StudioCube";
        asset.AddSubmesh(BuiltinMeshes::Cube());
        m_hCube = m_renderer.UploadMesh(std::move(asset));
    }
    {
        MeshAssetResource asset;
        asset.debugName = "GradientCube";
        asset.AddSubmesh(GradientCube(0.08f, 1.25f));
        m_hGradientCube = m_renderer.UploadMesh(std::move(asset));
    }

    if (FileExists(L"..\\media\\studio.hdr"))
        m_renderer.LoadIBL(L"..\\media\\studio.hdr");

    m_renderer.SetSceneAmbient(0.02f, 0.02f, 0.02f);

    m_hMatEngine = CreateEngineCubeMaterial(m_renderer);
    m_hMatStone  = CreateStoneMaterial(m_renderer, 2.0f, 2.0f, 2.0f, 2.0f);
    m_hMatFloor  = CreateFloorMaterial(m_renderer, 2.0f, 2.0f, 20.0f, 20.0f);

    m_floor = MakeRenderableCube(reg, m_hCube, m_hMatFloor, "Floor",
        { 0.0f, -1.00f, 4.0f }, { 56.0f, 0.25f, 56.0f });

    m_leftCube = MakeRenderableCube(reg, m_hGradientCube, m_hMatEngine, "LeftCube",
        { -2.0f, 0.15f, 4.2f }, { 2.1f, 2.1f, 2.1f });

    m_rightCube = MakeRenderableCube(reg, m_hGradientCube, m_hMatStone, "RightCube",
        { 1.95f, -0.05f, 2.8f }, { 1.65f, 1.65f, 1.65f });

    if (auto* tc = reg.Get<TransformComponent>(m_rightCube))
        tc->SetEulerDeg(0.0f, -5.0f, 0.0f);

    // Camera
    m_camera = reg.CreateEntity();
    reg.Add<TagComponent>(m_camera, "MainCamera");
    {
        TransformComponent tc;
        tc.localPosition = { -5.15f, 3.0f, -5.8f };
        SetLookAt(tc, tc.localPosition, Float3{ -2.0f, 0.15f, 4.2f });
        reg.Add<TransformComponent>(m_camera, tc);
    }
    reg.Add<WorldTransformComponent>(m_camera);
    {
        CameraComponent cam;
        cam.fovDeg      = 42.0f;
        cam.nearPlane   = 0.1f;
        cam.farPlane    = 250.0f;
        cam.aspectRatio = 16.0f / 9.0f;
        reg.Add<CameraComponent>(m_camera, cam);
    }
    reg.Add<ActiveCameraTag>(m_camera);

    // Key light
    m_sun = reg.CreateEntity();
    reg.Add<TagComponent>(m_sun, "KeyDirectional");
    {
        LightComponent lc;
        lc.kind            = LightKind::Directional;
        lc.diffuseColor    = { 0.5f, 0.7f, 0.8f, 1.0f };
        lc.intensity       = 1.5f;
        lc.castShadows     = true;
        lc.shadowOrthoSize = 50.0f;
        lc.shadowNear      = 0.1f;
        lc.shadowFar       = 120.0f;
        reg.Add<LightComponent>(m_sun, lc);
    }
    {
        TransformComponent tc;
        tc.SetEulerDeg(60.0f, -30.0f, 0.0f);
        reg.Add<TransformComponent>(m_sun, tc);
    }
    reg.Add<WorldTransformComponent>(m_sun);

    // Fill light
    m_sun2 = reg.CreateEntity();
    reg.Add<TagComponent>(m_sun2, "KeyDirectional2");
    {
        LightComponent lc;
        lc.kind             = LightKind::Directional;
        lc.diffuseColor     = { 0.6f, 0.5f, 0.4f, 1.0f };
        lc.intensity        = 0.15f;
        lc.castShadows      = false;
        lc.shadowOrthoSize  = 10.0f;
        lc.shadowNear       = 0.1f;
        lc.shadowFar        = 80.0f;
        reg.Add<LightComponent>(m_sun2, lc);
    }
    {
        TransformComponent tc;
        tc.SetEulerDeg(-30.0f, 40.0f, 0.0f);
        reg.Add<TransformComponent>(m_sun2, tc);
    }
    reg.Add<WorldTransformComponent>(m_sun2);

    // Spot
    m_fillLight = reg.CreateEntity();
    reg.Add<TagComponent>(m_fillLight, "FillSpot");
    {
        LightComponent lc;
        lc.kind           = LightKind::Spot;
        lc.diffuseColor   = { 1.0f, 0.45f, 0.05f, 1.0f };
        lc.intensity      = 10.0f;
        lc.radius         = 20.0f;
        lc.innerConeAngle = 1.5f;
        lc.outerConeAngle = 10.0f;
        lc.castShadows    = false;
        reg.Add<LightComponent>(m_fillLight, lc);
    }
    {
        TransformComponent tc;
        tc.localPosition = { 5.0f, 8.0f, 10.0f };
        SetLookAt(tc, tc.localPosition, Float3{ 0.0f, 0.0f, 4.0f });
        reg.Add<TransformComponent>(m_fillLight, tc);
    }
    reg.Add<WorldTransformComponent>(m_fillLight);

    // Post-processing
    if (m_renderer.SupportsTextureFormat(GDXTextureFormat::RGBA16_FLOAT))
    {
        m_renderer.SetBloom(1280, 706,
                            1.25f,              // threshold
                            1.0f,               // intensity
                            0.28f);             // strength
        m_renderer.SetToneMapping(ToneMappingMode::ACES, 0.6f);
        m_renderer.SetFXAA(1130, 706);
        m_renderer.SetNormalDebugView(false);
        //m_renderer.DisableBloom();
        //m_renderer.SetEdgeDebugView(true, 1280, 720, 250.0f, 4.0f, false, true);
        m_renderer.SetGTAO(1280, 720, 0.1f, 100.0f, 18.0f, 1.5f, 1.0f, 1.5f);
    }
}

void ECSGame::Update(float dt)
{
    Registry& reg = m_renderer.GetRegistry();

    if (m_orbitCamera)
    {
        m_camOrbitAngle += 18.0f * dt;
        if (m_camOrbitAngle >= 360.0f)
            m_camOrbitAngle -= 360.0f;

        if (auto* tc = reg.Get<TransformComponent>(m_camera))
        {
            const float rad = m_camOrbitAngle * (3.14159265f / 180.0f);
            tc->localPosition.x = std::sinf(rad) * 8.0f;
            tc->localPosition.z = std::cosf(rad) * -8.0f + 4.0f;
            tc->localPosition.y = 1.8f;
            tc->SetEulerDeg(10.0f, m_camOrbitAngle, 0.0f);
        }
    }
}

void ECSGame::OnEvent(const Event& e, KROMEngine& engine)
{
    std::visit([&](auto&& ev)
    {
        using T = std::decay_t<decltype(ev)>;

        if constexpr (std::is_same_v<T, KeyPressedEvent>)
        {
            if (ev.key == Key::Escape)
            {
                Debug::Log("ECSGame: ESC — beende Anwendung");
                engine.Shutdown();
            }
            else if (ev.key == Key::C)
            {
                ToggleCameraOrbit();
            }
        }
        else if constexpr (std::is_same_v<T, QuitEvent>)
        {
            engine.Shutdown();
        }
    }, e);
}
