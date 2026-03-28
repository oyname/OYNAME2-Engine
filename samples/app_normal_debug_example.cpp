#include "gidx.h"
#include "GDXECSRenderer.h"
#include "Components.h"

#include <vector>
#include <array>
#include <cmath>
#include <cstdlib>

namespace
{
    struct MovingLight
    {
        LPENTITY light = NULL_LPENTITY;
        LPENTITY marker = NULL_LPENTITY;

        float centerX = 0.0f;
        float centerY = 1.0f;
        float centerZ = 0.0f;

        float radiusXZ = 2.0f;
        float speed = 1.0f;
        float phase = 0.0f;
        float bobAmp = 0.35f;
        float bobSpeed = 1.5f;

        float colorR = 1.0f;
        float colorG = 1.0f;
        float colorB = 1.0f;
    };

    std::vector<MovingLight> g_lights;

    LPENTITY g_camera = NULL_LPENTITY;
    LPENTITY g_sun = NULL_LPENTITY;

    LPMESH g_cubeMesh = NULL_MESH;
    LPMESH g_sphereMesh = NULL_MESH;

    LPMATERIAL g_floorMat = NULL_MATERIAL;
    LPMATERIAL g_wallMat = NULL_MATERIAL;
    LPMATERIAL g_cubeMat = NULL_MATERIAL;
    LPMATERIAL g_sphereMatA = NULL_MATERIAL;
    LPMATERIAL g_sphereMatB = NULL_MATERIAL;

    constexpr float kRoomHalfW = 6.0f;
    constexpr float kRoomHalfH = 3.0f;
    constexpr float kRoomDepth = 12.0f;

    float Frand01()
    {
        return static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
    }

    float FrandRange(float a, float b)
    {
        return a + (b - a) * Frand01();
    }

    void MakeRenderable(
        LPENTITY& out,
        LPMESH mesh,
        LPMATERIAL mat,
        const char* tag,
        float px, float py, float pz,
        float sx, float sy, float sz,
        float pitchDeg = 0.0f,
        float yawDeg = 0.0f,
        float rollDeg = 0.0f,
        bool castShadows = true)
    {
        Engine::CreateMesh(&out, mesh, tag);
        Engine::AssignMaterial(out, mat);
        Engine::PositionEntity(out, px, py, pz);
        Engine::RotateEntity(out, pitchDeg, yawDeg, rollDeg);
        Engine::ScaleEntity(out, sx, sy, sz);

        auto& reg = Engine::_::renderer->GetRegistry();

        if (auto* vis = reg.Get<VisibilityComponent>(out))
        {
            vis->castShadows = castShadows;
            vis->receiveShadows = true;
        }
    }

    LPMATERIAL CreateColoredPBR(float r, float g, float b, float metallic, float roughness)
    {
        LPMATERIAL m = NULL_MATERIAL;
        Engine::CreateMaterial(&m, r, g, b, 1.0f);
        Engine::MaterialPBR(m, metallic, roughness);
        return m;
    }

    LPMATERIAL CreateEmissiveMaterial(float r, float g, float b, float emissiveIntensity)
    {
        LPMATERIAL m = NULL_MATERIAL;
        Engine::CreateMaterial(&m, r, g, b, 1.0f);
        Engine::MaterialPBR(m, 0.0f, 0.85f);
        Engine::MaterialEmissive(m, r, g, b, emissiveIntensity);
        return m;
    }

    void CreateRoom()
    {
        LPENTITY floor = NULL_LPENTITY;
        LPENTITY wallLeft = NULL_LPENTITY;
        LPENTITY wallRight = NULL_LPENTITY;
        LPENTITY wallBack = NULL_LPENTITY;

        MakeRenderable(floor, g_cubeMesh, g_floorMat, "Floor", 0.0f, -0.1f, 0.0f, kRoomHalfW * 2.0f, 0.2f, kRoomDepth, 0.0f, 0.0f, 0.0f, true);
        MakeRenderable(wallLeft, g_cubeMesh, g_wallMat, "WallLeft", -kRoomHalfW, kRoomHalfH * 0.5f, 0.0f, 0.2f, kRoomHalfH, kRoomDepth, 0.0f, 0.0f, 0.0f, true);
        MakeRenderable(wallRight, g_cubeMesh, g_wallMat, "WallRight", kRoomHalfW, kRoomHalfH * 0.5f, 0.0f, 0.2f, kRoomHalfH, kRoomDepth, 0.0f, 0.0f, 0.0f, true);
        MakeRenderable(wallBack, g_cubeMesh, g_wallMat, "WallBack", 0.0f, kRoomHalfH * 0.5f, kRoomDepth * 0.5f, kRoomHalfW * 2.0f, kRoomHalfH, 0.2f, 0.0f, 0.0f, 0.0f, true);
    }

    void CreateSceneObjects()
    {
        LPENTITY centerCube = NULL_LPENTITY;
        MakeRenderable(centerCube, g_cubeMesh, g_cubeMat, "CenterCube", 0.0f, 3.0f, 2.0f, 1.5f, 1.5f, 1.5f, 0.0f, 25.0f, 0.0f, true);

        const std::array<std::array<float, 3>, 8> spherePos =
        {{
            { -2.5f, 0.7f,  1.0f }, {  2.4f, 0.7f,  1.3f }, { -3.2f, 0.7f,  4.2f }, {  3.0f, 0.7f,  4.6f },
            { -1.2f, 0.7f,  6.0f }, {  1.4f, 0.7f,  6.4f }, { -4.4f, 0.7f,  7.8f }, {  4.2f, 0.7f,  8.0f }
        }};

        for (size_t i = 0; i < spherePos.size(); ++i)
        {
            LPENTITY s = NULL_LPENTITY;
            MakeRenderable(s, g_sphereMesh, (i % 2 == 0) ? g_sphereMatA : g_sphereMatB, "SceneSphere", spherePos[i][0], spherePos[i][1], spherePos[i][2], 1.1f, 1.1f, 1.1f, 0.0f, 0.0f, 0.0f, true);
        }
    }

    void CreateMainCamera()
    {
        Engine::CreateCamera(&g_camera, 60.0f, 0.1f, 100.0f, "MainCamera");
        Engine::PositionEntity(g_camera, 0.0f, 2.8f, -8.5f);
        Engine::LookAt(g_camera, 0.0f, 1.3f, 3.5f);
    }

    void CreateShadowLight()
    {
        Engine::CreateLight(&g_sun, LightKind::Directional, 0.85f, 0.88f, 1.0f, "Sun");
        auto& reg = Engine::_::renderer->GetRegistry();
        Engine::PositionEntity(g_sun, -4.0f, 7.0f, -4.0f);
        Engine::LookAt(g_sun, 0.0f, 0.8f, 4.5f);

        if (auto* lc = reg.Get<LightComponent>(g_sun))
        {
            lc->intensity = 1.2f;
            lc->castShadows = true;
            lc->shadowOrthoSize = 12.0f;
            lc->shadowNear = 0.1f;
            lc->shadowFar = 30.0f;
        }
    }

    void CreateMovingPointLights()
    {
        g_lights.reserve(50);
        static const std::array<std::array<float, 3>, 6> palette =
        {{
            {1.00f, 0.25f, 0.25f}, {0.25f, 0.95f, 0.35f}, {0.25f, 0.55f, 1.00f},
            {1.00f, 0.80f, 0.20f}, {0.85f, 0.25f, 1.00f}, {0.20f, 0.95f, 0.95f}
        }};

        for (int i = 0; i < 50; ++i)
        {
            const auto& c = palette[static_cast<size_t>(i) % palette.size()];
            MovingLight ml;
            ml.centerX = FrandRange(-4.8f, 4.8f);
            ml.centerY = FrandRange(0.9f, 2.0f);
            ml.centerZ = FrandRange(0.8f, 10.5f);
            ml.radiusXZ = FrandRange(0.35f, 1.65f);
            ml.speed = FrandRange(0.6f, 2.0f);
            ml.phase = FrandRange(0.0f, 6.2831853f);
            ml.bobAmp = FrandRange(0.10f, 0.45f);
            ml.bobSpeed = FrandRange(0.9f, 2.8f);
            ml.colorR = c[0]; ml.colorG = c[1]; ml.colorB = c[2];

            Engine::CreateLight(&ml.light, LightKind::Point, ml.colorR, ml.colorG, ml.colorB, "PointLight");
            {
                auto& reg = Engine::_::renderer->GetRegistry();
                if (auto* lc = reg.Get<LightComponent>(ml.light))
                {
                    lc->radius = FrandRange(2.2f, 4.5f);
                    lc->intensity = FrandRange(1.8f, 4.2f);
                    lc->castShadows = false;
                }
            }

            {
                LPMATERIAL markerMat = CreateEmissiveMaterial(ml.colorR, ml.colorG, ml.colorB, 2.0f);
                MakeRenderable(ml.marker, g_sphereMesh, markerMat, "LightMarker", ml.centerX, ml.centerY, ml.centerZ, 0.12f, 0.12f, 0.12f, 0.0f, 0.0f, 0.0f, false);
                auto& reg = Engine::_::renderer->GetRegistry();
                if (auto* vis = reg.Get<VisibilityComponent>(ml.marker))
                {
                    vis->receiveShadows = false;
                    vis->castShadows = false;
                }
            }
            g_lights.push_back(ml);
        }
    }

    void InitScene()
    {
        std::srand(1337);
        Engine::_::renderer->SetClearColor(0.02f, 0.02f, 0.04f, 1.0f);
        Engine::_::renderer->SetSceneAmbient(1.03f, 0.03f, 0.04f);
        Engine::_::renderer->SetShadowMapSize(2048);

        // Nur Normal-Debug aktivieren. Bloom / Tonemapping / FXAA aus, damit das Bild unverfälscht bleibt.
        Engine::_::renderer->DisableBloom();
        Engine::_::renderer->DisableToneMapping();
        Engine::_::renderer->DisableFXAA();
        Engine::_::renderer->SetDepthDebugView(false);
        Engine::_::renderer->SetDepthFogTest(false);
        Engine::_::renderer->SetNormalDebugView(true);

        g_cubeMesh = Engine::Cube();
        g_sphereMesh = Engine::Sphere(0.5f, 96, 64);
        g_floorMat = CreateColoredPBR(0.34f, 0.34f, 0.36f, 0.00f, 0.92f);
        g_wallMat = CreateColoredPBR(0.56f, 0.58f, 0.62f, 0.00f, 0.88f);
        g_cubeMat = CreateColoredPBR(0.92f, 0.77f, 0.68f, 0.05f, 0.35f);
        g_sphereMatA = CreateColoredPBR(0.72f, 0.25f, 0.18f, 0.10f, 0.30f);
        g_sphereMatB = CreateColoredPBR(0.18f, 0.42f, 0.78f, 0.15f, 0.28f);

        CreateRoom();
        CreateSceneObjects();
        CreateMainCamera();
        CreateShadowLight();
        CreateMovingPointLights();
    }

    void UpdateScene(float dt)
    {
        const float t = Engine::_::engine ? Engine::_::engine->GetTotalTime() : 0.0f;
        {
            auto& reg = Engine::_::renderer->GetRegistry();
            reg.View<TagComponent, TransformComponent>([&](EntityID e, TagComponent& tag, TransformComponent&)
            {
                if (tag.name == "CenterCube")
                    Engine::TurnEntity(e, 12.0f * dt, 24.0f * dt, 0.0f);
            });
        }

        for (auto& ml : g_lights)
        {
            const float a = t * ml.speed + ml.phase;
            const float x = ml.centerX + std::cos(a) * ml.radiusXZ;
            const float z = ml.centerZ + std::sin(a) * ml.radiusXZ;
            const float y = ml.centerY + std::sin(t * ml.bobSpeed + ml.phase * 1.7f) * ml.bobAmp;
            Engine::PositionEntity(ml.light, x, y, z);
            Engine::PositionEntity(ml.marker, x, y, z);
        }
    }
}

int main()
{
    if (!Engine::Graphics(Engine::Renderer::DX11, 1280, 720, "Room - Normal Debug", 0.02f, 0.02f, 0.04f, true))
        return 1;

    InitScene();
    Engine::OnUpdate(UpdateScene);
    Engine::Run();
    return 0;
}
