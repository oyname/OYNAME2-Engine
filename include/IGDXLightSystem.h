#pragma once

#include "Registry.h"
#include "FrameData.h"

// ---------------------------------------------------------------------------
// IGDXLightSystem — API-neutrales Interface für das ECS-Lichtsystem.
//
// Verantwortung:
//   - Scannt LightComponent + WorldTransformComponent aus der Registry
//   - Befüllt FrameData.lights + shadowViewProjMatrix
//   - Lädt den LightBuffer auf die GPU
//
// Implementierungen:
//   - GDXDX11LightSystem  (DirectX 11)
//   - zukünftig: GDXOpenGLLightSystem, GDXDX12LightSystem, ...
//
// Kein <d3d11.h>, kein ID3D11* in diesem Header.
// ---------------------------------------------------------------------------
class IGDXLightSystem
{
public:
    virtual ~IGDXLightSystem() = default;

    // Einmalige GPU-Ressourcen anlegen (Buffer etc.).
    // device ist void* — der Aufrufer castet intern auf den API-spezifischen Typ.
    virtual bool Init(void* device) = 0;

    virtual void Shutdown() = 0;

    // Scannt Registry, befüllt FrameData, lädt GPU-Buffer.
    // ctx ist void* — Implementierung castet auf den passenden Context-Typ.
    virtual void Update(Registry& registry, FrameData& frame, void* ctx) = 0;

    virtual bool IsReady() const = 0;
};
