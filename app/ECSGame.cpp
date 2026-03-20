#include "ECSGame.h"
#include "GIDXEngine.h"
#include "Debug.h"
#include <filesystem>
#include <variant>

// ---------------------------------------------------------------------------
// Hilfsfunktion: prüft ob eine Datei existiert
// ---------------------------------------------------------------------------
static bool FileExists(const std::wstring& path)
{
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

void ECSGame::Init()
{
    Registry& reg = m_renderer.GetRegistry();

    // ====================================================================
    // Meshes
    // ====================================================================
    {
        MeshAssetResource asset;
        asset.debugName = "Sphere";
        asset.AddSubmesh(BuiltinMeshes::Sphere());
        m_hSph = m_renderer.UploadMesh(std::move(asset));
    }
    {
        MeshAssetResource asset;
        asset.debugName = "Wuerfel";
        asset.AddSubmesh(BuiltinMeshes::Cube());
        m_hCube = m_renderer.UploadMesh(std::move(asset));
    }
    {
        MeshAssetResource asset;
        asset.debugName = "Oktaeder";
        asset.AddSubmesh(BuiltinMeshes::Octahedron(1.0f));
        m_hDiamond = m_renderer.UploadMesh(std::move(asset));
    }

    // ====================================================================
    // Materialien
    //
    // Flat-Color Materialien (Phong, keine Textur).
    // Für den Würfel: PBR mit Texturen wenn vorhanden, sonst Phong-Fallback.
    // ====================================================================
    m_hMatRed = m_renderer.CreateMaterial(MaterialResource::FlatColor(0.90f, 0.20f, 0.12f));
    m_hMatGreen = m_renderer.CreateMaterial(MaterialResource::FlatColor(0.20f, 0.82f, 0.38f));

    {
        const std::wstring albedoPath = L"..//media/albedo.png";
        const std::wstring normalPath = L"..//media/normal.png";
        const std::wstring ormPath = L"..//media/orm.png";

        if (FileExists(albedoPath))
        {
            // Albedo: sRGB=true  (Farbe, gamma-kodiert)
            // Normal: sRGB=false (lineare Daten, kein Gamma-Encoding)
            // ORM:    sRGB=false (R=Occlusion, G=Roughness, B=Metallic, linear)
            TextureHandle hAlbedo = m_renderer.LoadTexture(albedoPath);
            TextureHandle hNormal = m_renderer.LoadTexture(normalPath, false);
            TextureHandle hORM = m_renderer.LoadTexture(ormPath, false);

            MaterialResource mat;

            mat.SetTexture(MaterialTextureSlot::Albedo, hAlbedo);
            mat.SetTexture(MaterialTextureSlot::Normal, hNormal);
            mat.SetTexture(MaterialTextureSlot::ORM, hORM);

            mat.data.baseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
            mat.data.metallic = 0.0f;   // Fallback wenn keine ORM
            mat.data.roughness = 0.8f;   // Fallback wenn keine ORM
            mat.data.normalScale = 1.0f;
            mat.data.occlusionStrength = 1.0f;
            mat.data.receiveShadows = 1.0f;
            mat.data.uvTilingOffset = { 1.0f, 1.0f, 0.0f, 0.0f };

            mat.data.flags =
                MF_USE_NORMAL_MAP |   // t1: Normal Map aktiv
                MF_USE_ORM_MAP |   // t2: ORM Map aktiv
                MF_SHADING_PBR;       // Cook-Torrance BRDF

            m_hMatPBR = m_renderer.CreateMaterial(mat);
        }
        else
        {
            // Kein Textur-Ordner → einfaches Blau als Fallback
            m_hMatPBR = m_renderer.CreateMaterial(
                MaterialResource::FlatColor(0.15f, 0.45f, 0.90f));
        }
    }

    // ====================================================================
    // Entity: Kugel
    // ====================================================================
    m_sphere = reg.CreateEntity();
    reg.Add<TagComponent>(m_sphere, "Dreieck");
    {
        TransformComponent tc;
        tc.localPosition = { 2.5f, 0.0f, 3.0f };
        tc.localScale = { 3.2f, 3.2f, 3.2f };
        reg.Add<TransformComponent>(m_sphere, tc);
    }
    reg.Add<WorldTransformComponent>(m_sphere);
    reg.Add<RenderableComponent>(m_sphere, m_hSph, m_hMatPBR, 0u);
    reg.Add<VisibilityComponent>(m_sphere);

    // ====================================================================
    // Entity: Würfel — PBR Material (oder Fallback), wirft Schatten
    // ====================================================================
    m_cube = reg.CreateEntity();
    reg.Add<TagComponent>(m_cube, "Wuerfel");
    {
        TransformComponent tc;
        tc.localPosition = { -0.5f, 0.0f, 3.0f };
        tc.localScale = { 1.5, 1.5f, 1.5f };
        reg.Add<TransformComponent>(m_cube, tc);
    }
    reg.Add<WorldTransformComponent>(m_cube);
    reg.Add<RenderableComponent>(m_cube, m_hCube, m_hMatPBR, 0u);
    reg.Add<VisibilityComponent>(m_cube);

    // ====================================================================
    // Entity: Grüner Oktaeder — wirft Schatten, schwebt
    // ====================================================================
    m_diamond = reg.CreateEntity();
    reg.Add<TagComponent>(m_diamond, "Oktaeder");
    {
        TransformComponent tc;
        tc.localPosition = { -3.0f, 0.0f, 3.0f };
        tc.localScale = { 1.0f, 2.0f, 1.0f };
        reg.Add<TransformComponent>(m_diamond, tc);
    }
    reg.Add<WorldTransformComponent>(m_diamond);
    reg.Add<RenderableComponent>(m_diamond, m_hDiamond, m_hMatPBR, 0u);
    reg.Add<VisibilityComponent>(m_diamond);

    // ====================================================================
    // Entity: Kamera
    // ====================================================================
    m_camera = reg.CreateEntity();
    reg.Add<TagComponent>(m_camera, "Main Cam");
    {
        TransformComponent tc;
        tc.localPosition = { 0.0f, 2.0f, -3.0f };
        tc.SetEulerDeg(15.0f, 0.0f, 0.0f);
        reg.Add<TransformComponent>(m_camera, tc);
    }
    reg.Add<WorldTransformComponent>(m_camera);
    {
        CameraComponent cam;
        cam.fovDeg = 60.0f;
        cam.nearPlane = 0.1f;
        cam.farPlane = 500.0f;
        cam.aspectRatio = 1.0f;
        reg.Add<CameraComponent>(m_camera, cam);
    }
    reg.Add<ActiveCameraTag>(m_camera);

    // Szenen-Ambient: einmal global setzen — gilt für alle Objekte
    m_renderer.SetSceneAmbient(0.38f, 0.38f, 0.45f);  // kühles Nacht-Ambient
    //
    // GDXDX11LightSystem braucht LightComponent + WorldTransformComponent.
    // Lichtrichtung = -Z der World-Matrix (durch SetEulerDeg gesetzt).
    // castShadows = true → GDXDX11LightSystem berechnet shadowViewProjMatrix,
    //                      GDXShadowMap rendert den Depth-Pass automatisch.
    // ====================================================================
    m_sun = reg.CreateEntity();
    reg.Add<TagComponent>(m_sun, "Sonne");
    {
        LightComponent lc;
        lc.kind = LightKind::Directional;
        lc.diffuseColor = { 0.8f, 0.8f, 1.0f, 1.0f };
        lc.intensity = 2.0f;
        lc.castShadows = true;
        lc.shadowOrthoSize = 20.0f;
        lc.shadowNear = 0.1f;
        lc.shadowFar = 1000.0f;
        reg.Add<LightComponent>(m_sun, lc);
    }
    {
        TransformComponent tc;
        tc.localPosition = { -5.0f, 0.0f, 0.0f };
        tc.SetEulerDeg(0.0f, 90.0f, 0.0f);
        reg.Add<TransformComponent>(m_sun, tc);
    }
    reg.Add<WorldTransformComponent>(m_sun);

    // ====================================================================
    // Entity: Spot-Licht — leuchtet von oben auf den Würfel
    // ====================================================================
    m_spotlight = reg.CreateEntity();
    reg.Add<TagComponent>(m_spotlight, "Spotlight");
    {
        LightComponent lc;
        lc.kind = LightKind::Spot;
        lc.diffuseColor = { 0.2f, 1.6f, 1.0f, 1.0f };
        lc.intensity = 25.0f;
        lc.radius = 25.0f;    // Reichweite in Welteinheiten
        lc.innerConeAngle = 2.0f;    // volle Helligkeit bis 12°
        lc.outerConeAngle = 10.0f;    // Penumbra bis 25°
        lc.castShadows = false;
        reg.Add<LightComponent>(m_spotlight, lc);
    }
    {
        // Spotlight: Position über dem Würfel, zeigt nach unten
        TransformComponent tc;
        tc.localPosition = { 0.0f, 20.0f, 3.0f };  // über dem Würfel
        tc.SetEulerDeg(90.0f, 0.0f, 0.0f);       // zeigt nach unten (-Z → Y mit 90° Pitch)
        reg.Add<TransformComponent>(m_spotlight, tc);
    }
    reg.Add<WorldTransformComponent>(m_spotlight);


    // ====================================================================
// Entity: Spot-Licht — leuchtet von unten auf den Würfel
// ====================================================================
    m_spotlight = reg.CreateEntity();
    reg.Add<TagComponent>(m_spotlight, "Spotlight");
    {
        LightComponent lc;
        lc.kind = LightKind::Spot;
        lc.diffuseColor = { 1.2f, 0.6f, 0.0f, 1.0f };
        lc.intensity = 25.0f;
        lc.radius = 25.0f;    // Reichweite in Welteinheiten
        lc.innerConeAngle = 2.0f;    // volle Helligkeit bis 12°
        lc.outerConeAngle = 10.0f;    // Penumbra bis 25°
        lc.castShadows = false;
        reg.Add<LightComponent>(m_spotlight, lc);
    }
    {
        // Spotlight: Position über dem Würfel, zeigt nach unten
        TransformComponent tc;
        tc.localPosition = { 0.0f, -20.0f, 3.0f };  // über dem Würfel
        tc.SetEulerDeg(-90.0f, 0.0f, 0.0f);       // zeigt nach unten (-Z → Y mit 90° Pitch)
        reg.Add<TransformComponent>(m_spotlight, tc);
    }
    reg.Add<WorldTransformComponent>(m_spotlight);
}

void ECSGame::Update(float dt)
{
    m_time += dt;

    Registry& reg = m_renderer.GetRegistry();

    // Kugel: Y-Achse (80°/s)
    if (auto* tc = reg.Get<TransformComponent>(m_sphere))
    {
        m_triYaw += 80.0f * dt;
        if (m_triYaw >= 360.0f) m_triYaw -= 360.0f;
        tc->SetEulerDeg(0.0f, m_triYaw, 0.0f);
    }

    // Würfel: Y (45°/s) + X (30°/s)
    if (auto* tc = reg.Get<TransformComponent>(m_cube))
    {
        m_cubeYaw += 45.0f * dt;
        m_cubePitch += 30.0f * dt;
        if (m_cubeYaw >= 360.0f) m_cubeYaw -= 360.0f;
        if (m_cubePitch >= 360.0f) m_cubePitch -= 360.0f;
        tc->SetEulerDeg(m_cubePitch, m_cubeYaw, 0.0f);
    }

    // Oktaeder: Rotation + Schweben
    if (auto* tc = reg.Get<TransformComponent>(m_diamond))
    {
        m_diamPitch += 55.0f * dt;
        if (m_diamPitch >= 360.0f) m_diamPitch -= 360.0f;
        tc->localPosition.y = sinf(m_time * 1.5f) * 0.4f;
        tc->SetEulerDeg(m_diamPitch, m_time * 30.0f, 0.0f);
        tc->dirty = true;
    }

    // Kamera-Orbit
    if (m_orbitCamera)
    {
        m_camOrbitAngle += 18.0f * dt;
        if (m_camOrbitAngle >= 360.0f) m_camOrbitAngle -= 360.0f;

        if (auto* tc = reg.Get<TransformComponent>(m_camera))
        {
            const float rad = m_camOrbitAngle * (3.14159265f / 180.0f);
            tc->localPosition.x = sinf(rad) * 9.0f;
            tc->localPosition.z = cosf(rad) * -9.0f;
            tc->localPosition.y = 2.0f;
            tc->SetEulerDeg(10.0f, m_camOrbitAngle, 0.0f);
        }
    }
}

// ---------------------------------------------------------------------------
// OnEvent — das Spiel reagiert auf Engine-Events.
// Analog zu OYNAME wo ESC über WindowProc → Windows::MainLoop(false) lief,
// ruft das Spiel hier direkt engine.Shutdown() auf.
// ---------------------------------------------------------------------------
void ECSGame::OnEvent(const Event& e, GIDXEngine& engine)
{
    std::visit([&](auto&& ev)
        {
            using T = std::decay_t<decltype(ev)>;

            if constexpr (std::is_same_v<T, KeyPressedEvent>)
            {
                if (ev.key == Key::Escape)
                {
                    Debug::Log("ECSGame: ESC gedrueckt - beende Anwendung");
                    engine.Shutdown();
                }
                if (ev.key == Key::C)
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