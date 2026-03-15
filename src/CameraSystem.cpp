#include "CameraSystem.h"
#include "GDXMathHelpers.h"

using namespace DirectX;

void CameraSystem::Update(Registry& registry, FrameData& frame) const
{
    registry.View<WorldTransformComponent, CameraComponent, ActiveCameraTag>(
        [&](EntityID, WorldTransformComponent& wt, CameraComponent& cam, ActiveCameraTag&)
        {
            const XMMATRIX world = GDXMathHelpers::LoadFloat4x4(wt.matrix);

            // Kameraposition aus Weltmatrix
            const XMVECTOR position = world.r[3];
            GDXMathHelpers::StoreFloat3(frame.cameraPos, position);
            frame.cullMask = cam.cullMask;

            // Rotationsteil aus Weltmatrix extrahieren
            XMMATRIX rot = world;
            rot.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);

            // Vorwärts- und Up-Vektor wie in der alten Engine aus der Rotation ableiten
            const XMVECTOR baseForward = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
            const XMVECTOR baseUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

            const XMVECTOR forward = XMVector3Normalize(
                XMVector3TransformNormal(baseForward, rot));
            const XMVECTOR up = XMVector3Normalize(
                XMVector3TransformNormal(baseUp, rot));

            GDXMathHelpers::StoreFloat3(frame.cameraForward, forward);

            const XMVECTOR target = XMVectorAdd(position, forward);

            // WICHTIG: View wie in der alten Engine per LookAtLH bauen,
            // nicht per inverse(world)
            const XMMATRIX view = XMMatrixLookAtLH(position, target, up);
            GDXMathHelpers::StoreFloat4x4(frame.viewMatrix, view);

            // Projektion
            XMMATRIX proj;
            if (cam.isOrtho)
            {
                proj = XMMatrixOrthographicLH(
                    cam.orthoWidth,
                    cam.orthoHeight,
                    cam.nearPlane,
                    cam.farPlane);
            }
            else
            {
                const float fovRad = GIDX::ToRadians(cam.fovDeg);
                proj = XMMatrixPerspectiveFovLH(
                    fovRad,
                    cam.aspectRatio,
                    cam.nearPlane,
                    cam.farPlane);
            }

            GDXMathHelpers::StoreFloat4x4(frame.projMatrix, proj);
            GDXMathHelpers::StoreFloat4x4(frame.viewProjMatrix, XMMatrixMultiply(view, proj));
        });
}

float CameraSystem::ComputeNDCDepth(const GIDX::Float4x4& worldMatrix,
    const GIDX::Float4x4& viewProjMatrix)
{
    const XMMATRIX world = GDXMathHelpers::LoadFloat4x4(worldMatrix);
    const XMMATRIX viewProj = GDXMathHelpers::LoadFloat4x4(viewProjMatrix);

    const XMVECTOR worldPos = world.r[3];

    XMVECTOR clip = XMVector4Transform(
        XMVectorSet(
            XMVectorGetX(worldPos),
            XMVectorGetY(worldPos),
            XMVectorGetZ(worldPos),
            1.0f),
        viewProj);

    const float w = XMVectorGetW(clip);
    if (w <= 0.0f)
        return 1.0f;

    return XMVectorGetZ(clip) / w;
}