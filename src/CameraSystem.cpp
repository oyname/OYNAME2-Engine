#include "CameraSystem.h"
#include "Core/GDXMath.h"
#include "Core/GDXMathOps.h"

#include <vector>

// ---------------------------------------------------------------------------
// Statische Kern-Hilfsmethode — wird von Update() und GDXECSRenderer genutzt.
// Kein Duplikat mehr zwischen CameraSystem und BuildFrameDataFromWorldAndCamera.
// ---------------------------------------------------------------------------
void CameraSystem::BuildFrameData(
    const WorldTransformComponent& wt,
    const CameraComponent& cam,
    FrameData& frame)
{
    const Float3 position = { wt.matrix._41, wt.matrix._42, wt.matrix._43 };
    frame.cameraPos = position;
    frame.cameraNearPlane = cam.nearPlane;
    frame.cameraFarPlane = cam.farPlane;
    frame.cameraProjectionFlags = cam.isOrtho ? 1u : 0u;
    frame.cullMask  = cam.cullMask;

    Matrix4 rot = wt.matrix;
    rot._41 = 0.0f; rot._42 = 0.0f; rot._43 = 0.0f; rot._44 = 1.0f;

    const Float3 forward = GDX::Normalize3(GDX::TransformVector({ 0.0f, 0.0f, 1.0f }, rot));
    const Float3 up      = GDX::Normalize3(GDX::TransformVector({ 0.0f, 1.0f, 0.0f }, rot));
    frame.cameraForward = forward;

    const Float3 target = GDX::Add(position, forward);
    const Matrix4 view = GDX::LookAtLH(position, target, up);
    frame.viewMatrix = view;

    Matrix4 proj;
    if (cam.isOrtho)
    {
        proj = GDX::OrthographicLH(cam.orthoWidth, cam.orthoHeight, cam.nearPlane, cam.farPlane);
    }
    else
    {
        proj = GDX::PerspectiveFovLH(GDX::ToRadians(cam.fovDeg), cam.aspectRatio, cam.nearPlane, cam.farPlane);
    }

    frame.projMatrix     = proj;
    frame.viewProjMatrix = GDX::Multiply(view, proj);
}

EntityID CameraSystem::FindActiveCameraEntity(Registry& registry) const
{
    EntityID found = NULL_ENTITY;
    registry.View<WorldTransformComponent, CameraComponent, ActiveCameraTag>(
        [&](EntityID id, WorldTransformComponent&, CameraComponent&, ActiveCameraTag&)
        {
            if (!found.IsValid())
                found = id;
        });
    return found;
}

bool CameraSystem::SetActiveCamera(Registry& registry, EntityID cameraEntity) const
{
    if (!cameraEntity.IsValid())
        return false;

    if (!registry.IsAlive(cameraEntity))
        return false;

    if (!registry.Has<CameraComponent>(cameraEntity) || !registry.Has<WorldTransformComponent>(cameraEntity))
        return false;

    std::vector<EntityID> activeCameras;
    registry.View<CameraComponent, ActiveCameraTag>(
        [&](EntityID id, CameraComponent&, ActiveCameraTag&)
        {
            activeCameras.push_back(id);
        });

    for (EntityID id : activeCameras)
    {
        if (id != cameraEntity)
            registry.Remove<ActiveCameraTag>(id);
    }

    if (!registry.Has<ActiveCameraTag>(cameraEntity))
        registry.Add<ActiveCameraTag>(cameraEntity);

    return true;
}

bool CameraSystem::Update(Registry& registry, FrameData& frame) const
{
    const EntityID activeCamera = FindActiveCameraEntity(registry);
    if (!activeCamera.IsValid())
        return false;

    return BuildFrameDataForCamera(registry, activeCamera, frame);
}

bool CameraSystem::BuildFrameDataForCamera(Registry& registry, EntityID cameraEntity, FrameData& frame, float aspectOverride) const
{
    const auto* wt  = registry.Get<WorldTransformComponent>(cameraEntity);
    const auto* cam = registry.Get<CameraComponent>(cameraEntity);
    if (!wt || !cam) return false;

    CameraComponent camCopy = *cam;
    if (aspectOverride > 0.0f)
        camCopy.aspectRatio = aspectOverride;

    BuildFrameData(*wt, camCopy, frame);
    return true;
}

float CameraSystem::ComputeNDCDepth(const Matrix4& worldMatrix,
    const Matrix4& viewProjMatrix)
{
    const Float4 clip = GDX::TransformFloat4(
        { worldMatrix._41, worldMatrix._42, worldMatrix._43, 1.0f },
        viewProjMatrix);

    if (clip.w <= 0.0f)
        return 1.0f;

    return clip.z / clip.w;
}
