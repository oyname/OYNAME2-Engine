#include "DebugCamera.h"

#include "CameraSystem.h"
#include "Components.h"
#include "RenderComponents.h"
#include "ECS/TransformSystem.h"
#include "ECS/Registry.h"
#include "Core/GDXMathOps.h"
#include "GDXInput.h"

#include <cmath>
#include <algorithm>

namespace
{
    constexpr float kPi = 3.14159265f;

    float RadToDeg(float rad)
    {
        return rad * (180.0f / kPi);
    }
}

void FreeCamera::AttachRegistry(Registry* registry)
{
    if (m_registry == registry)
        return;

    m_registry = registry;
    m_cameraEntity = NULL_ENTITY;
    m_previousCameraEntity = NULL_ENTITY;

    if (m_registry)
        EnsureCameraEntity();
}

void FreeCamera::EnsureCameraEntity()
{
    if (!m_registry)
        return;

    if (m_cameraEntity.IsValid() && m_registry->IsAlive(m_cameraEntity))
        return;

    m_cameraEntity = m_registry->CreateEntity();
    m_registry->Add<TagComponent>(m_cameraEntity, TagComponent{ "FreeFlyCamera" });
    m_registry->Add<TransformComponent>(m_cameraEntity);
    m_registry->Add<WorldTransformComponent>(m_cameraEntity);

    CameraComponent cam{};
    cam.fovDeg = m_fovDeg;
    cam.nearPlane = m_near;
    cam.farPlane = m_far;
    cam.aspectRatio = m_aspect;
    cam.isOrtho = m_isOrtho;
    cam.orthoWidth = m_orthoWidth;
    cam.orthoHeight = m_orthoHeight;
    cam.cullMask = m_cullMask;
    m_registry->Add<CameraComponent>(m_cameraEntity, cam);

    SyncEntityTransform();
    SyncEntityCamera();
}

void FreeCamera::SetEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;

    m_enabled = enabled;

    if (!m_registry)
        return;

    EnsureCameraEntity();
    CameraSystem cameraSystem{};

    if (enabled)
    {
        const EntityID currentActive = cameraSystem.FindActiveCameraEntity(*m_registry);
        if (currentActive.IsValid() && currentActive != m_cameraEntity)
        {
            m_previousCameraEntity = currentActive;
            CaptureFromEntity(currentActive);
        }

        SyncEntityTransform();
        SyncEntityCamera();
        cameraSystem.SetActiveCamera(*m_registry, m_cameraEntity);
    }
    else
    {
        if (m_registry->Has<ActiveCameraTag>(m_cameraEntity))
            m_registry->Remove<ActiveCameraTag>(m_cameraEntity);

        if (m_previousCameraEntity.IsValid() && m_registry->IsAlive(m_previousCameraEntity))
            cameraSystem.SetActiveCamera(*m_registry, m_previousCameraEntity);

        m_previousCameraEntity = NULL_ENTITY;
    }
}

void FreeCamera::SetPosition(const Float3& pos)
{
    m_position = pos;
    SyncEntityTransform();
}

void FreeCamera::SetYawPitch(float yawDeg, float pitchDeg)
{
    m_yawDeg   = yawDeg;
    m_pitchDeg = std::clamp(pitchDeg, -89.f, 89.f);
    SyncEntityTransform();
}

void FreeCamera::SetFovDeg(float fov)
{
    m_fovDeg = fov;
    SyncEntityCamera();
}

void FreeCamera::SetNearFar(float n, float f)
{
    m_near = n;
    m_far = f;
    SyncEntityCamera();
}

void FreeCamera::CaptureFromEntity(EntityID entity)
{
    if (!m_registry || !entity.IsValid())
        return;

    const auto* wt = m_registry->Get<WorldTransformComponent>(entity);
    const auto* cam = m_registry->Get<CameraComponent>(entity);
    if (!wt || !cam)
        return;

    m_position = { wt->matrix._41, wt->matrix._42, wt->matrix._43 };

    const Float3 forward = GDX::Normalize3({ wt->matrix._31, wt->matrix._32, wt->matrix._33 });
    m_yawDeg = RadToDeg(std::atan2(forward.x, forward.z));
    m_pitchDeg = std::clamp(RadToDeg(std::asin(-std::clamp(forward.y, -1.0f, 1.0f))), -89.0f, 89.0f);

    m_fovDeg = cam->fovDeg;
    m_near = cam->nearPlane;
    m_far = cam->farPlane;
    m_aspect = cam->aspectRatio;
    m_isOrtho = cam->isOrtho;
    m_orthoWidth = cam->orthoWidth;
    m_orthoHeight = cam->orthoHeight;
    m_cullMask = cam->cullMask;
}

Float3 FreeCamera::ForwardVector() const
{
    const float yR = m_yawDeg   * (kPi / 180.f);
    const float pR = m_pitchDeg * (kPi / 180.f);
    return GDX::Normalize3({
        std::sin(yR) * std::cos(pR),
        -std::sin(pR),
        std::cos(yR) * std::cos(pR)
    });
}

Float3 FreeCamera::RightVector() const
{
    const Float3 fwd = ForwardVector();
    const Float3 up  = { 0.f, 1.f, 0.f };
    return GDX::Normalize3(GDX::Cross(fwd, up));
}

Float3 FreeCamera::UpVector() const
{
    return GDX::Cross(RightVector(), ForwardVector());
}

void FreeCamera::ProcessInput(float deltaTime)
{
    if (!m_enabled) return;

    const float speed = GDXInput::KeyDown(Key::Space)
        ? moveSpeed * fastMultiplier
        : moveSpeed;
    const float move  = speed * deltaTime;
    const float turn  = turnSpeedDeg * deltaTime;

    const Float3 fwd   = ForwardVector();
    const Float3 right = RightVector();

    if (GDXInput::KeyDown(Key::W))
        m_position = GDX::Add(m_position, GDX::Scale3(fwd,   move));
    if (GDXInput::KeyDown(Key::S))
        m_position = GDX::Add(m_position, GDX::Scale3(fwd,  -move));
    if (GDXInput::KeyDown(Key::A))
        m_position = GDX::Add(m_position, GDX::Scale3(right,  move));
    if (GDXInput::KeyDown(Key::D))
        m_position = GDX::Add(m_position, GDX::Scale3(right, -move));
    if (GDXInput::KeyDown(Key::E))
        m_position.y += move;
    if (GDXInput::KeyDown(Key::Q))
        m_position.y -= move;

    if (GDXInput::KeyDown(Key::Left))  m_yawDeg   -= turn;
    if (GDXInput::KeyDown(Key::Right)) m_yawDeg   += turn;
    if (GDXInput::KeyDown(Key::Up))    m_pitchDeg -= turn;
    if (GDXInput::KeyDown(Key::Down))  m_pitchDeg += turn;

    m_pitchDeg = std::clamp(m_pitchDeg, -89.f, 89.f);
    SyncEntityTransform();
}

Matrix4 FreeCamera::ViewMatrix() const
{
    const Float3 target = GDX::Add(m_position, ForwardVector());
    return GDX::LookAtLH(m_position, target, { 0.f, 1.f, 0.f });
}

Matrix4 FreeCamera::ProjMatrix(float aspect) const
{
    if (m_isOrtho)
        return GDX::OrthographicLH(m_orthoWidth, m_orthoHeight, m_near, m_far);

    return GDX::PerspectiveFovLH(
        m_fovDeg * (kPi / 180.f),
        aspect,
        m_near,
        m_far);
}

void FreeCamera::SyncEntityTransform()
{
    if (!m_registry)
        return;

    EnsureCameraEntity();

    auto* transform = m_registry->Get<TransformComponent>(m_cameraEntity);
    if (!transform)
        return;

    transform->localPosition = m_position;
    transform->localRotation = GDX::QuaternionFromEulerDeg(m_pitchDeg, m_yawDeg, 0.0f);
    TransformSystem::MarkDirty(*m_registry, m_cameraEntity);
}

void FreeCamera::SyncEntityCamera()
{
    if (!m_registry)
        return;

    EnsureCameraEntity();

    auto* cam = m_registry->Get<CameraComponent>(m_cameraEntity);
    if (!cam)
        return;

    cam->fovDeg = m_fovDeg;
    cam->nearPlane = m_near;
    cam->farPlane = m_far;
    cam->aspectRatio = m_aspect;
    cam->isOrtho = m_isOrtho;
    cam->orthoWidth = m_orthoWidth;
    cam->orthoHeight = m_orthoHeight;
    cam->cullMask = m_cullMask;
}
