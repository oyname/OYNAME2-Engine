#pragma once

#include "GDXECSRenderer.h"
#include "Components.h"
#include "SubmeshData.h"
#include "Events.h"

class KROMEngine;

class ECSGame
{
public:
    explicit ECSGame(GDXECSRenderer& renderer) : m_renderer(renderer) {}

    void Init();
    void Update(float deltaTime);
    void OnEvent(const Event& e, KROMEngine& engine);

    void ToggleCameraOrbit() { m_orbitCamera = !m_orbitCamera; }

private:
    GDXECSRenderer& m_renderer;

    EntityID m_camera = NULL_ENTITY;
    EntityID m_floor = NULL_ENTITY;
    EntityID m_leftCube = NULL_ENTITY;
    EntityID m_rightCube = NULL_ENTITY;
    EntityID m_sun = NULL_ENTITY;
    EntityID m_sun2 = NULL_ENTITY;
    EntityID m_fillLight = NULL_ENTITY;

    MeshHandle m_hCube = MeshHandle::Invalid();
    MeshHandle m_hGradientCube = MeshHandle::Invalid(); // Cube mit Y-Helligkeitsverlauf

    MaterialHandle m_hMatEngine = MaterialHandle::Invalid();
    MaterialHandle m_hMatStone  = MaterialHandle::Invalid();
    MaterialHandle m_hMatFloor  = MaterialHandle::Invalid();

    float m_camOrbitAngle = 0.0f;
    bool  m_orbitCamera   = false;
};
