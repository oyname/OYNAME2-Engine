#include "gidx.h"
#include "NeonTimeBuffer.h"
#include "GDXShaderResource.h"
#include "GDXVertexFlags.h"
#include "MaterialResource.h"

// GIDX-Port des alten Multishader-Beispiels.
// Links: Standardmaterial mit Textur.
// Rechts: Custom-Neon-Shader.

static NeonTimeBuffer g_neonTime;

int main()
{
    if (!Engine::Graphics(Engine::Renderer::DX11, 1200, 650, "GIDX - Multi Shader Example", 0.0f, 0.25f, 0.5f))
        return 1;

    auto* renderer = Engine::_::renderer;
    if (!renderer)
        return 1;

    renderer->SetSceneAmbient(0.18f, 0.18f, 0.22f);

    ShaderHandle neonShader = renderer->CreateShader(
        L"../shader/VertexShaderNeon_GIDX.hlsl",
        L"../shader/PixelShaderNeon_GIDX.hlsl",
        GDX_VERTEX_POSITION);

    LPTEXTURE face = NULL_TEXTURE;
    Engine::LoadTexture(&face, L"..\\media\\engine.png");

    LPENTITY camera = NULL_LPENTITY;
    Engine::CreateCamera(&camera);
    Engine::PositionEntity(camera, 0.0f, 0.0f, -3.0f);
    Engine::RotateEntity(camera, 0.0f, 0.0f, 0.0f);

    LPMATERIAL material1 = NULL_MATERIAL;
    Engine::CreateMaterial(&material1);
    Engine::MaterialSetAlbedo(material1, face);

    MaterialResource neonMatRes;
    neonMatRes.shader = neonShader;
    neonMatRes.data.baseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    neonMatRes.data.emissiveColor = { 0.0f, 0.0f, 0.0f, 1.0f };
    neonMatRes.SetFlag(MF_UNLIT, true);
    LPMATERIAL material2 = renderer->CreateMaterial(std::move(neonMatRes));

    if (!g_neonTime.Initialize(*renderer, material2))
    {
        Debug::LogError("0_example_Multishaderexample_GIDX.cpp: NeonTimeBuffer init failed");
    }

    LPMESH cube = Engine::Cube();

    LPENTITY mesh1 = NULL_LPENTITY;
    Engine::CreateMesh(&mesh1, cube, "TexturedCube");
    Engine::AssignMaterial(mesh1, material1);
    Engine::PositionEntity(mesh1, -1.0f, 0.0f, 0.0f);
    Engine::ScaleEntity(mesh1, 1.0f, 1.0f, 1.0f);

    LPENTITY mesh2 = NULL_LPENTITY;
    Engine::CreateMesh(&mesh2, cube, "NeonCube");
    Engine::AssignMaterial(mesh2, material2);
    Engine::PositionEntity(mesh2, 1.0f, 0.0f, 0.0f);

    LPENTITY pointLight = NULL_LPENTITY;
    Engine::CreateLight(&pointLight, LightKind::Point, 1.0f, 1.0f, 1.0f);
    Engine::PositionEntity(pointLight, 0.0f, 0.0f, -3.0f);
    Engine::LightRadius(pointLight, 25.0f);
    Engine::LightIntensity(pointLight, 2.0f);

    Engine::OnUpdate([&](float dt)
    {
        if (Engine::Input::KeyDown(Key::Up))    Engine::PositionEntity(mesh1, Engine::EntityX(mesh1), Engine::EntityY(mesh1), Engine::EntityZ(mesh1) + 5.0f * dt);
        if (Engine::Input::KeyDown(Key::Down))  Engine::PositionEntity(mesh1, Engine::EntityX(mesh1), Engine::EntityY(mesh1), Engine::EntityZ(mesh1) - 5.0f * dt);
        if (Engine::Input::KeyDown(Key::Right)) Engine::PositionEntity(mesh1, Engine::EntityX(mesh1) + 5.0f * dt, Engine::EntityY(mesh1), Engine::EntityZ(mesh1));
        if (Engine::Input::KeyDown(Key::Left))  Engine::PositionEntity(mesh1, Engine::EntityX(mesh1) - 5.0f * dt, Engine::EntityY(mesh1), Engine::EntityZ(mesh1));

        g_neonTime.Update(dt);

        Engine::TurnEntity(mesh1, -50.0f * dt, -50.0f * dt, 0.0f);
        Engine::TurnEntity(mesh2,  50.0f * dt,  50.0f * dt, 0.0f);
    });

    Engine::Run();
    g_neonTime.Shutdown();
    return 0;
}
