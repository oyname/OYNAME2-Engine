#include "CameraSystem.h"
#include "GDXMath.h"

// ---------------------------------------------------------------------------
// Statische Kern-Hilfsmethode — wird von Update() und GDXECSRenderer genutzt.
// Kein Duplikat mehr zwischen CameraSystem und BuildFrameDataFromWorldAndCamera.
// ---------------------------------------------------------------------------
void CameraSystem::BuildFrameData(
    const WorldTransformComponent& wt,
    const CameraComponent& cam,
    FrameData& frame)
{
    const GIDX::Float3 position = { wt.matrix._41, wt.matrix._42, wt.matrix._43 };
    frame.cameraPos = position;
    frame.cullMask  = cam.cullMask;

    GIDX::Float4x4 rot = wt.matrix;
    rot._41 = 0.0f; rot._42 = 0.0f; rot._43 = 0.0f; rot._44 = 1.0f;

    const GIDX::Float3 forward = GIDX::Normalize3(GIDX::TransformVector({ 0.0f, 0.0f, 1.0f }, rot));
    const GIDX::Float3 up      = GIDX::Normalize3(GIDX::TransformVector({ 0.0f, 1.0f, 0.0f }, rot));
    frame.cameraForward = forward;

    const GIDX::Float3 target = GIDX::Add(position, forward);
    const GIDX::Float4x4 view = GIDX::LookAtLH(position, target, up);
    frame.viewMatrix = view;

    GIDX::Float4x4 proj;
    if (cam.isOrtho)
    {
        proj = GIDX::OrthographicLH(cam.orthoWidth, cam.orthoHeight, cam.nearPlane, cam.farPlane);
    }
    else
    {
        proj = GIDX::PerspectiveFovLH(GIDX::ToRadians(cam.fovDeg), cam.aspectRatio, cam.nearPlane, cam.farPlane);
    }

    frame.projMatrix     = proj;
    frame.viewProjMatrix = GIDX::Multiply(view, proj);
}

void CameraSystem::Update(Registry& registry, FrameData& frame) const
{
    registry.View<WorldTransformComponent, CameraComponent, ActiveCameraTag>(
        [&](EntityID, WorldTransformComponent& wt, CameraComponent& cam, ActiveCameraTag&)
        {
            BuildFrameData(wt, cam, frame);
        });
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

float CameraSystem::ComputeNDCDepth(const GIDX::Float4x4& worldMatrix,
    const GIDX::Float4x4& viewProjMatrix)
{
    const GIDX::Float4 clip = GIDX::TransformFloat4(
        { worldMatrix._41, worldMatrix._42, worldMatrix._43, 1.0f },
        viewProjMatrix);

    if (clip.w <= 0.0f)
        return 1.0f;

    return clip.z / clip.w;
}
