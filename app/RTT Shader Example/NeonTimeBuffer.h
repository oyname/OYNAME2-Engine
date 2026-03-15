#pragma once

#include "GDXECSRenderer.h"
#include "Handle.h"

// GIDX-Version des alten NeonTimeBuffer.
// Kein eigener D3D11-Constant-Buffer mehr.
// Stattdessen wird die Zeit in MaterialData.uvTilingOffset.x geschrieben,
// damit der Neon-Pixelshader sie direkt aus b2 lesen kann.
class NeonTimeBuffer
{
public:
    NeonTimeBuffer() = default;

    bool Initialize(GDXECSRenderer& renderer, MaterialHandle material);
    void Update(float deltaTime);
    void Bind();      // absichtlich leer: in GIDX nicht mehr noetig
    void Shutdown();

    float GetTime() const noexcept { return m_elapsedTime; }

private:
    GDXECSRenderer* m_renderer = nullptr;
    MaterialHandle  m_material = MaterialHandle::Invalid();
    float           m_elapsedTime = 0.0f;
};
