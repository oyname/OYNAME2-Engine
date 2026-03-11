#include "ECSGame.h"

void ECSGame::Init()
{
    Registry& reg = m_renderer.GetRegistry();

    // ====================================================================
    // Meshes
    // ====================================================================
    {
        MeshAssetResource asset;
        asset.debugName = "Dreieck";
        asset.AddSubmesh(BuiltinMeshes::Triangle());
        m_hTri = m_renderer.UploadMesh(std::move(asset));
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
        // Oktaeder mit per-face Vertices (kein Vertex-Sharing).
        // Jedes Dreieck hat 3 eigene Vertices mit der Flachen-Normalen.
        asset.AddSubmesh(BuiltinMeshes::Octahedron(1.0f));
        m_hDiamond = m_renderer.UploadMesh(std::move(asset));
    }

    // ====================================================================
    // Materialien
    // ====================================================================
    m_hMatRed = m_renderer.CreateMaterial(MaterialResource::FlatColor(0.90f, 0.20f, 0.12f));
    m_hMatBlue = m_renderer.CreateMaterial(MaterialResource::FlatColor(0.15f, 0.45f, 0.90f));
    m_hMatGreen = m_renderer.CreateMaterial(MaterialResource::FlatColor(0.20f, 0.82f, 0.38f));

    // ====================================================================
    // Entity: Rotes Dreieck — Mitte, leicht vorne
    // ====================================================================
    m_triangle = reg.CreateEntity();
    reg.Add<TagComponent>(m_triangle, "Dreieck");
    {
        TransformComponent tc;
        tc.localPosition = { 0.0f, 0.0f, 0.0f };
        tc.localScale = { 1.2f, 1.2f, 1.2f };
        reg.Add<TransformComponent>(m_triangle, tc);
    }
    reg.Add<WorldTransformComponent>(m_triangle);
    reg.Add<MeshRefComponent>(m_triangle, m_hTri, 0u);
    reg.Add<MaterialRefComponent>(m_triangle, m_hMatRed);
    reg.Add<VisibilityComponent>(m_triangle);

    // ====================================================================
    // Entity: Blauer Würfel — rechts versetzt, weiter hinten
    // Cube-Geometrie ist 2x2x2, Scale 0.5 → 1x1x1 in Weltkoordinaten
    // ====================================================================
    m_cube = reg.CreateEntity();
    reg.Add<TagComponent>(m_cube, "Wuerfel");
    {
        TransformComponent tc;
        tc.localPosition = { 3.0f, 0.0f, 3.0f };
        tc.localScale = { 1.0f, 1.0f, 1.0f };
        reg.Add<TransformComponent>(m_cube, tc);
    }
    reg.Add<WorldTransformComponent>(m_cube);
    reg.Add<MeshRefComponent>(m_cube, m_hCube, 0u);
    reg.Add<MaterialRefComponent>(m_cube, m_hMatBlue);
    reg.Add<VisibilityComponent>(m_cube);

    // ====================================================================
    // Entity: Grüner Oktaeder — links versetzt
    // ====================================================================
    m_diamond = reg.CreateEntity();
    reg.Add<TagComponent>(m_diamond, "Oktaeder");
    {
        TransformComponent tc;
        tc.localPosition = { -3.0f, 0.0f, 3.0f };
        tc.localScale = { 1.0f, 1.0f, 1.0f };
        reg.Add<TransformComponent>(m_diamond, tc);
    }
    reg.Add<WorldTransformComponent>(m_diamond);
    reg.Add<MeshRefComponent>(m_diamond, m_hDiamond, 0u);
    reg.Add<MaterialRefComponent>(m_diamond, m_hMatGreen);
    reg.Add<VisibilityComponent>(m_diamond);

    // ====================================================================
    // Entity: Kamera — weiter hinten, leicht nach unten geneigt
    // ====================================================================
    m_camera = reg.CreateEntity();
    reg.Add<TagComponent>(m_camera, "Main Cam");
    {
        TransformComponent tc;
        tc.localPosition = { 0.0f, 2.0f, -3.0f };
        tc.SetEulerDeg(15.0f, 0.0f, 0.0f);   // 10° nach unten
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
}

void ECSGame::Update(float dt)
{
    m_time += dt;

    Registry& reg = m_renderer.GetRegistry();

    // Dreieck: Y-Achse (80°/s)
    if (auto* tc = reg.Get<TransformComponent>(m_triangle))
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

    // Oktaeder: schwebend + Rotation
    if (auto* tc = reg.Get<TransformComponent>(m_diamond))
    {
        m_diamPitch += 55.0f * dt;
        if (m_diamPitch >= 360.0f) m_diamPitch -= 360.0f;
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
