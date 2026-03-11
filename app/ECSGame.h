#pragma once

#include "GDXECSRenderer.h"
#include "Components.h"
#include "SubmeshData.h"

class ECSGame
{
public:
    explicit ECSGame(GDXECSRenderer& renderer) : m_renderer(renderer) {}

    void Init();
    void Update(float deltaTime);
    void ToggleCameraOrbit() { m_orbitCamera = !m_orbitCamera; }

private:
    GDXECSRenderer& m_renderer;

    EntityID m_triangle = NULL_ENTITY;
    EntityID m_cube = NULL_ENTITY;
    EntityID m_diamond = NULL_ENTITY;
    EntityID m_camera = NULL_ENTITY;

    MeshHandle m_hTri;
    MeshHandle m_hCube;
    MeshHandle m_hDiamond;

    MaterialHandle m_hMatRed;
    MaterialHandle m_hMatBlue;
    MaterialHandle m_hMatGreen;

    float m_triYaw = 0.0f;
    float m_cubeYaw = 0.0f;
    float m_cubePitch = 0.0f;
    float m_diamPitch = 0.0f;
    float m_camOrbitAngle = 0.0f;
    float m_time = 0.0f;

    bool m_orbitCamera = false;
};
