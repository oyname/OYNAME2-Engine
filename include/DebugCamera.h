#pragma once

#include "Core/GDXMath.h"
#include "ECS/ECSTypes.h"
#include <cstdint>

class Registry;

// Echte ECS-Free-Fly-Camera.
// Besitzt eine eigene Kamera-Entity und schaltet per ActiveCameraTag um.
// Kein FrameData-Override, kein nachträgliches Snapshot-Mutieren.
class FreeCamera
{
public:
    FreeCamera() = default;

    void AttachRegistry(Registry* registry);

    void   SetEnabled(bool enabled);
    bool   IsEnabled() const { return m_enabled; }
    void   Toggle()          { SetEnabled(!m_enabled); }

    void   SetPosition(const Float3& pos);
    void   SetYawPitch(float yawDeg, float pitchDeg);

    Float3 GetPosition() const { return m_position; }
    float  GetYaw() const { return m_yawDeg; }
    float  GetPitch() const { return m_pitchDeg; }

    void   SetFovDeg(float fov);
    void   SetNearFar(float n, float f);
    float  GetFovDeg() const { return m_fovDeg; }

    void ProcessInput(float deltaTime);

    Matrix4 ViewMatrix() const;
    Matrix4 ProjMatrix(float aspect) const;

    EntityID GetCameraEntity() const { return m_cameraEntity; }
    EntityID GetPreviousCameraEntity() const { return m_previousCameraEntity; }

    float moveSpeed      = 8.0f;
    float fastMultiplier = 5.0f;
    float turnSpeedDeg   = 90.0f;

private:
    void EnsureCameraEntity();
    void SyncEntityTransform();
    void SyncEntityCamera();
    void CaptureFromEntity(EntityID entity);

    Float3 ForwardVector() const;
    Float3 RightVector() const;
    Float3 UpVector() const;

private:
    Registry* m_registry = nullptr;
    bool      m_enabled = false;
    EntityID  m_cameraEntity = NULL_ENTITY;
    EntityID  m_previousCameraEntity = NULL_ENTITY;

    Float3    m_position = { 0.f, 2.f, -5.f };
    float     m_yawDeg = 0.f;
    float     m_pitchDeg = 0.f;

    float     m_fovDeg = 60.f;
    float     m_near = 0.25f;
    float     m_far = 1000.f;
    float     m_aspect = 16.0f / 9.0f;
    bool      m_isOrtho = false;
    float     m_orthoWidth = 10.0f;
    float     m_orthoHeight = 10.0f;
    uint32_t  m_cullMask = 0xFFFFFFFFu;
};
