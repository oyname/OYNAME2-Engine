#include "CameraSystem.h"
#include "GDXMath.h"

void CameraSystem::Update(Registry& registry, FrameData& frame) const
{
    registry.View<WorldTransformComponent, CameraComponent, ActiveCameraTag>(
        [&](EntityID, WorldTransformComponent& wt, CameraComponent& cam, ActiveCameraTag&)
        {
            // Kameraposition: Zeile 3 der Weltmatrix (row-vector Konvention)
            const GIDX::Float3 position = { wt.matrix._41, wt.matrix._42, wt.matrix._43 };
            frame.cameraPos = position;
            frame.cullMask  = cam.cullMask;

            // Rotationsteil: Translation auf null setzen
            GIDX::Float4x4 rot = wt.matrix;
            rot._41 = 0.0f;  rot._42 = 0.0f;  rot._43 = 0.0f;  rot._44 = 1.0f;

            // Vorwärts- und Up-Vektor aus der Rotation ableiten
            const GIDX::Float3 forward = GIDX::Normalize3(
                GIDX::TransformVector({ 0.0f, 0.0f, 1.0f }, rot));
            const GIDX::Float3 up = GIDX::Normalize3(
                GIDX::TransformVector({ 0.0f, 1.0f, 0.0f }, rot));

            frame.cameraForward = forward;
            const GIDX::Float3 target = GIDX::Add(position, forward);

            // View-Matrix per LookAtLH (wie in der alten Engine)
            const GIDX::Float4x4 view = GIDX::LookAtLH(position, target, up);
            frame.viewMatrix = view;

            // Projektionsmatrix
            GIDX::Float4x4 proj;
            if (cam.isOrtho)
            {
                proj = GIDX::OrthographicLH(
                    cam.orthoWidth,
                    cam.orthoHeight,
                    cam.nearPlane,
                    cam.farPlane);
            }
            else
            {
                const float fovRad = GIDX::ToRadians(cam.fovDeg);
                proj = GIDX::PerspectiveFovLH(
                    fovRad,
                    cam.aspectRatio,
                    cam.nearPlane,
                    cam.farPlane);
            }

            frame.projMatrix     = proj;
            frame.viewProjMatrix = GIDX::Multiply(view, proj);
        });
}

float CameraSystem::ComputeNDCDepth(const GIDX::Float4x4& worldMatrix,
    const GIDX::Float4x4& viewProjMatrix)
{
    // Weltposition: Zeile 3 der Weltmatrix
    const GIDX::Float4 clip = GIDX::TransformFloat4(
        { worldMatrix._41, worldMatrix._42, worldMatrix._43, 1.0f },
        viewProjMatrix);

    if (clip.w <= 0.0f)
        return 1.0f;

    return clip.z / clip.w;
}
