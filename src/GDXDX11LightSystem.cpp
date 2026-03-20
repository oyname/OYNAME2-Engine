// GDXDX11LightSystem.cpp — DirectX 11 Lichtsystem.
// Scannt LightComponent + WorldTransformComponent.
// Befüllt FrameData.lights (Directional, Point, Spot).
// Berechnet Shadow-ViewProj für erstes castShadows-Licht.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include "GDXMath.h"
#include "GDXMathHelpers.h"
#include <cstring>
#include <cmath>

#include "GDXDX11LightSystem.h"
#include "Components.h"

using namespace DirectX;

static constexpr float DEG2RAD = GIDX::Pi / 180.0f;

bool GDXDX11LightSystem::Init(ID3D11Device* device)
{
    if (!device) return false;

    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth      = sizeof(GDXDX11LightCBuffer);
    desc.Usage          = D3D11_USAGE_DYNAMIC;
    desc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    return SUCCEEDED(device->CreateBuffer(&desc, nullptr, &m_lightBuffer));
}

void GDXDX11LightSystem::Shutdown()
{
    if (m_lightBuffer) { m_lightBuffer->Release(); m_lightBuffer = nullptr; }
}

void GDXDX11LightSystem::Update(Registry& registry, FrameData& frame, ID3D11DeviceContext* ctx)
{
    frame.lightCount       = 0u;
    frame.hasShadowPass    = false;
    frame.shadowCasterMask = 0xFFFFFFFFu;
    frame.lightAffectMask  = 0xFFFFFFFFu;

    registry.View<LightComponent, WorldTransformComponent>(
        [&](EntityID, LightComponent& lc, WorldTransformComponent& wt)
        {
            if (frame.lightCount >= MAX_LIGHTS) return;

            const uint32_t i = frame.lightCount++;
            LightEntry&    le = frame.lights[i];

            // --- Position aus WorldTransform ---
            le.position.x = wt.matrix._41;
            le.position.y = wt.matrix._42;
            le.position.z = wt.matrix._43;

            // w-Komponente kodiert den Lichttyp (im Shader ausgelesen)
            if      (lc.kind == LightKind::Point) le.position.w = 1.0f;
            else if (lc.kind == LightKind::Spot)  le.position.w = 2.0f;
            else                                   le.position.w = 0.0f; // Directional

            // --- Richtung: +Z Achse der World-Matrix ---
            le.direction.x = wt.matrix._31;
            le.direction.y = wt.matrix._32;
            le.direction.z = wt.matrix._33;
            le.direction.w = lc.castShadows ? 1.0f : 0.0f;

            // --- Farbe * Intensität ---
            le.diffuse.x = lc.diffuseColor.x * lc.intensity;
            le.diffuse.y = lc.diffuseColor.y * lc.intensity;
            le.diffuse.z = lc.diffuseColor.z * lc.intensity;
            le.diffuse.w = lc.radius;

            le.radius    = lc.radius;
            le.intensity = lc.intensity;

            // --- Spot Cone: cos-Werte vorberechnen ---
            le.innerCosAngle = cosf(lc.innerConeAngle * DEG2RAD);
            le.outerCosAngle = cosf(lc.outerConeAngle * DEG2RAD);

            // --- Shadow: erstes Directional mit castShadows ---
            if (!frame.hasShadowPass
                && lc.kind == LightKind::Directional
                && lc.castShadows)
            {
                const XMVECTOR lightDir = XMVector3Normalize(
                    XMVectorSet(le.direction.x, le.direction.y, le.direction.z, 0.0f));

                const XMVECTOR cameraPos = XMVectorSet(
                    frame.cameraPos.x, frame.cameraPos.y, frame.cameraPos.z, 1.0f);
                const XMVECTOR cameraForward = XMVector3Normalize(XMVectorSet(
                    frame.cameraForward.x, frame.cameraForward.y, frame.cameraForward.z, 0.0f));

                const float focusDistance = lc.shadowOrthoSize * 0.5f;
                const XMVECTOR focusPoint = XMVectorAdd(
                    cameraPos,
                    XMVectorScale(cameraForward, focusDistance));

                const float shadowDistance = (lc.shadowFar > lc.shadowNear)
                    ? (lc.shadowFar * 0.5f)
                    : 100.0f;
                const XMVECTOR lightPos = XMVectorSubtract(
                    focusPoint,
                    XMVectorScale(lightDir, shadowDistance));

                const XMVECTOR up = (fabsf(XMVectorGetY(lightDir)) > 0.99f)
                    ? XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f)
                    : XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

                const XMMATRIX lightView = XMMatrixLookAtLH(lightPos, focusPoint, up);
                const XMMATRIX lightProj = XMMatrixOrthographicLH(
                    lc.shadowOrthoSize, lc.shadowOrthoSize,
                    lc.shadowNear, lc.shadowFar);

                GDXMathHelpers::StoreFloat4x4(frame.shadowViewProjMatrix,
                    XMMatrixMultiply(lightView, lightProj));

                frame.hasShadowPass = true;
                frame.shadowCasterMask = lc.shadowLayerMask;
                frame.lightAffectMask = lc.affectLayerMask;
            }
        });

    if (ctx) UploadBuffer(ctx, frame);
}

void GDXDX11LightSystem::Upload(const FrameData& frame, ID3D11DeviceContext* ctx)
{
    UploadBuffer(ctx, frame);
}

void GDXDX11LightSystem::UploadBuffer(ID3D11DeviceContext* ctx, const FrameData& frame)
{
    if (!m_lightBuffer || !ctx) return;

    GDXDX11LightCBuffer cb = {};
    cb.lightCount      = frame.lightCount;
    cb.sceneAmbient[0] = frame.sceneAmbient.x;
    cb.sceneAmbient[1] = frame.sceneAmbient.y;
    cb.sceneAmbient[2] = frame.sceneAmbient.z;

    for (uint32_t i = 0; i < frame.lightCount; ++i)
    {
        const LightEntry&          src = frame.lights[i];
        GDXDX11LightCBufferEntry&  dst = cb.lights[i];

        dst.position[0] = src.position.x;
        dst.position[1] = src.position.y;
        dst.position[2] = src.position.z;
        dst.position[3] = src.position.w;

        dst.direction[0] = src.direction.x;
        dst.direction[1] = src.direction.y;
        dst.direction[2] = src.direction.z;
        dst.direction[3] = src.direction.w;

        dst.diffuse[0] = src.diffuse.x;
        dst.diffuse[1] = src.diffuse.y;
        dst.diffuse[2] = src.diffuse.z;
        dst.diffuse[3] = src.diffuse.w;

        dst.innerCosAngle = src.innerCosAngle;
        dst.outerCosAngle = src.outerCosAngle;
        dst._pad0 = 0.0f;
        dst._pad1 = 0.0f;
    }

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (SUCCEEDED(ctx->Map(m_lightBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        std::memcpy(mapped.pData, &cb, sizeof(cb));
        ctx->Unmap(m_lightBuffer, 0);
    }

    // b3 — kein Konflikt mit b0 (Entity), b1 (Frame), b2 (Material)
    ctx->VSSetConstantBuffers(3, 1, &m_lightBuffer);
    ctx->PSSetConstantBuffers(3, 1, &m_lightBuffer);
}
