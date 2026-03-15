#pragma once

#include "GDXECSRenderer.h"
#include "Components.h"
#include "SubmeshData.h"
#include "Events.h"

class GDXEngine; // forward  kein zirkulrer Include

class ECSGame
{
public:
    explicit ECSGame(GDXECSRenderer& renderer) : m_renderer(renderer) {}

    void Init();
    void Update(float deltaTime);

    // Wird von main.cpp ber engine.SetEventCallback() aufgerufen.
    // Das Spiel entscheidet was bei ESC, Fenster schliessen etc. passiert.
    void OnEvent(const Event& e, GDXEngine& engine);

    void ToggleCameraOrbit() { m_orbitCamera = !m_orbitCamera; }

private:
    GDXECSRenderer& m_renderer;

    // Entities
    EntityID m_sphere = NULL_ENTITY;
    EntityID m_cube = NULL_ENTITY;
    EntityID m_diamond = NULL_ENTITY;
    EntityID m_camera = NULL_ENTITY;
    EntityID m_sun = NULL_ENTITY;   // Directional Light
    EntityID m_spotlight = NULL_ENTITY;   // Spot Light

    // Mesh-Handles
    MeshHandle m_hSph;
    MeshHandle m_hCube;
    MeshHandle m_hDiamond;

    // Material-Handles
    MaterialHandle m_hMatRed;
    MaterialHandle m_hMatBlue;
    MaterialHandle m_hMatGreen;
    MaterialHandle m_hMatPBR;      // PBR + Normal Map + ORM (wenn Texturen vorhanden)

    // Animationsstate
    float m_triYaw = 0.0f;
    float m_cubeYaw = 0.0f;
    float m_cubePitch = 0.0f;
    float m_diamPitch = 0.0f;
    float m_camOrbitAngle = 0.0f;
    float m_time = 0.0f;

    bool m_orbitCamera = false;
};
