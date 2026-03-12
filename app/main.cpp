#include "gidx.h"
#include <filesystem>
#include <cmath>

// ---------------------------------------------------------------------------
// Szenen-State
// ---------------------------------------------------------------------------
static LPENTITY  s_sphere, s_cube, s_diamond, s_camera, s_sun, s_spotlight;
static float s_triYaw = 0, s_cubeYaw = 0, s_cubePitch = 0;
static float s_diamPitch = 0, s_camAngle = 0, s_time = 0;
static bool  s_orbitCamera = false;

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------
static void Init()
{
    LPMESH hSph = Engine::Sphere();
    LPMESH hCube = Engine::Cube();
    LPMESH hDiamond = Engine::Octahedron(1.0f);

    LPMATERIAL hMatPBR;
    if (std::filesystem::exists(L"..//media/albedo.png"))
    {
        LPTEXTURE hAlbedo, hNormal, hORM;
        Engine::LoadTexture(&hAlbedo, L"..//media/albedo.png", true);
        Engine::LoadTexture(&hNormal, L"..//media/normal.png", false);
        Engine::LoadTexture(&hORM, L"..//media/orm.png", false);
        Engine::CreateMaterial(&hMatPBR, 1.0f, 1.0f, 1.0f);
        Engine::MaterialSetAlbedo(hMatPBR, hAlbedo);
        Engine::MaterialSetNormal(hMatPBR, hNormal);
        Engine::MaterialSetORM(hMatPBR, hORM);
        Engine::MaterialPBR(hMatPBR, 0.0f, 0.8f);
        Engine::MaterialReceiveShadows(hMatPBR, true);
    }
    else
    {
        Engine::CreateMaterial(&hMatPBR, 0.15f, 0.45f, 0.90f);
    }

    Engine::CreateMesh(&s_sphere, hSph, "Dreieck");
    Engine::PositionEntity(s_sphere, 2.5f, 0.0f, 3.0f);
    Engine::ScaleEntity(s_sphere, 3.2f, 3.2f, 3.2f);
    Engine::AssignMaterial(s_sphere, hMatPBR);

    Engine::CreateMesh(&s_cube, hCube, "Wuerfel");
    Engine::PositionEntity(s_cube, -0.5f, 0.0f, 3.0f);
    Engine::ScaleEntity(s_cube, 1.5f, 1.5f, 1.5f);
    Engine::AssignMaterial(s_cube, hMatPBR);

    Engine::CreateMesh(&s_diamond, hDiamond, "Oktaeder");
    Engine::PositionEntity(s_diamond, -3.0f, 0.0f, 3.0f);
    Engine::ScaleEntity(s_diamond, 1.0f, 2.0f, 1.0f);
    Engine::AssignMaterial(s_diamond, hMatPBR);

    Engine::CreateCamera(&s_camera, 60.0f, 0.1f, 500.0f, "Main Cam");
    Engine::PositionEntity(s_camera, 0.0f, 2.0f, -3.0f);
    Engine::RotateEntity(s_camera, 15.0f, 0.0f, 0.0f);

    Engine::SetAmbient(0.38f, 0.38f, 0.45f);

    Engine::CreateLight(&s_sun, LightKind::Directional, 0.8f, 0.8f, 1.0f, "Sonne");
    Engine::LightIntensity(s_sun, 2.0f);
    Engine::LightCastShadows(s_sun, true, 20.0f, 0.1f, 1000.0f);
    Engine::PositionEntity(s_sun, -5.0f, 0.0f, 0.0f);
    Engine::RotateEntity(s_sun, 0.0f, -90.0f, 0.0f);

    Engine::CreateLight(&s_spotlight, LightKind::Spot, 0.2f, 1.6f, 1.0f, "Spotlight");
    Engine::LightIntensity(s_spotlight, 25.0f);
    Engine::LightRadius(s_spotlight, 25.0f);
    Engine::LightSpotCone(s_spotlight, 2.0f, 10.0f);
    Engine::PositionEntity(s_spotlight, 0.0f, 20.0f, 3.0f);
    Engine::RotateEntity(s_spotlight, -90.0f, 0.0f, 0.0f);
}

// ---------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------
static void Update(float dt)
{
    s_time += dt;
    Registry& reg = Engine::_::renderer->GetRegistry();

    if (auto* tc = reg.Get<TransformComponent>(s_sphere))
    {
        s_triYaw += 80.0f * dt;
        if (s_triYaw >= 360.0f) s_triYaw -= 360.0f;
        tc->SetEulerDeg(0.0f, s_triYaw, 0.0f);
    }

    if (auto* tc = reg.Get<TransformComponent>(s_cube))
    {
        s_cubeYaw += 45.0f * dt;
        s_cubePitch += 30.0f * dt;
        if (s_cubeYaw >= 360.0f) s_cubeYaw -= 360.0f;
        if (s_cubePitch >= 360.0f) s_cubePitch -= 360.0f;
        tc->SetEulerDeg(s_cubePitch, s_cubeYaw, 0.0f);
    }

    if (auto* tc = reg.Get<TransformComponent>(s_diamond))
    {
        s_diamPitch += 55.0f * dt;
        if (s_diamPitch >= 360.0f) s_diamPitch -= 360.0f;
        tc->localPosition.y = sinf(s_time * 1.5f) * 0.4f;
        tc->SetEulerDeg(s_diamPitch, s_time * 30.0f, 0.0f);
        tc->dirty = true;
    }

    if (s_orbitCamera)
    {
        s_camAngle += 18.0f * dt;
        if (s_camAngle >= 360.0f) s_camAngle -= 360.0f;
        const float rad = s_camAngle * (3.14159265f / 180.0f);
        Engine::PositionEntity(s_camera, sinf(rad) * 9.0f, 2.0f, cosf(rad) * -9.0f);
        Engine::RotateEntity(s_camera, 10.0f, s_camAngle, 0.0f);
    }
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main()
{
    if (!Engine::Graphics(Engine::Renderer::DX11, 1280, 720, "GIDX | ESC: Beenden | C: Kamera-Orbit"))
        return 1;

    Init();

    while (Engine::Frame())
    {
        const float dt = Engine::DeltaTime();

        if (Engine::Input::KeyHit(Key::Escape))
            Engine::Quit();

        if (Engine::Input::KeyHit(Key::C))
            s_orbitCamera = !s_orbitCamera;

        Update(dt);
    }

    Engine::Quit();
    return 0;
}