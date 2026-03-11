// GDXLightSystem.cpp — ECS-Lichtsystem.
// Scannt LightComponent + WorldTransformComponent.
// Befüllt FrameData.lights (Directional, Point, Spot).
// Berechnet Shadow-ViewProj für erstes castShadows-Licht.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <DirectXMath.h>
#include <cstring>
#include <cmath>

#include "GDXLightSystem.h"

using namespace DirectX;

// ---------------------------------------------------------------------------
static constexpr float DEG2RAD = 3.14159265f / 180.0f;

bool GDXLightSystem::Init(ID3D11Device* device)
{
    if (!device) return false;

    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth      = sizeof(GDXLightCBuffer);
    desc.Usage          = D3D11_USAGE_DYNAMIC;
    desc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    return SUCCEEDED(device->CreateBuffer(&desc, nullptr, &m_lightBuffer));
}

void GDXLightSystem::Shutdown()
{
    if (m_lightBuffer) { m_lightBuffer->Release(); m_lightBuffer = nullptr; }
}

void GDXLightSystem::Update(Registry& registry, FrameData& frame, ID3D11DeviceContext* ctx)
{
    frame.lightCount    = 0u;
    frame.hasShadowPass = false;

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

            // --- Richtung: -Z Achse der World-Matrix ---
            le.direction.x = -wt.matrix._31;
            le.direction.y = -wt.matrix._32;
            le.direction.z = -wt.matrix._33;
            le.direction.w = 0.0f;

            // --- Farbe * Intensität ---
            le.diffuse.x = lc.diffuseColor.x * lc.intensity;
            le.diffuse.y = lc.diffuseColor.y * lc.intensity;
            le.diffuse.z = lc.diffuseColor.z * lc.intensity;
            le.diffuse.w = lc.radius;   // a = Radius für Point/Spot Falloff

            le.radius    = lc.radius;
            le.intensity = lc.intensity;

            // --- Spot Cone: cos-Werte vorberechnen ---
            // Shader macht nur einen dot() + Vergleich, keine trig-Funktion nötig.
            le.innerCosAngle = cosf(lc.innerConeAngle * DEG2RAD);
            le.outerCosAngle = cosf(lc.outerConeAngle * DEG2RAD);

            // --- Shadow: erstes Directional mit castShadows ---
            if (!frame.hasShadowPass
                && lc.kind == LightKind::Directional
                && lc.castShadows)
            {
                const XMVECTOR lightDir = XMVector3Normalize(
                    XMVectorSet(le.direction.x, le.direction.y, le.direction.z, 0.0f));

                const XMVECTOR lightPos = XMVectorScale(
                    XMVectorNegate(lightDir), 100.0f);

                const XMVECTOR up = (fabsf(XMVectorGetY(lightDir)) > 0.99f)
                    ? XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f)
                    : XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

                const XMMATRIX lightView = XMMatrixLookAtLH(
                    lightPos, XMVectorZero(), up);
                const XMMATRIX lightProj = XMMatrixOrthographicLH(
                    lc.shadowOrthoSize, lc.shadowOrthoSize,
                    lc.shadowNear, lc.shadowFar);

                XMStoreFloat4x4(&frame.shadowViewProjMatrix,
                    XMMatrixMultiply(lightView, lightProj));

                frame.hasShadowPass = true;
            }
        });

    if (ctx) UploadBuffer(ctx, frame);
}

void GDXLightSystem::UploadBuffer(ID3D11DeviceContext* ctx, const FrameData& frame)
{
    if (!m_lightBuffer || !ctx) return;

    GDXLightCBuffer cb = {};
    cb.lightCount      = frame.lightCount;
    cb.sceneAmbient[0] = frame.sceneAmbient.x;
    cb.sceneAmbient[1] = frame.sceneAmbient.y;
    cb.sceneAmbient[2] = frame.sceneAmbient.z;

    for (uint32_t i = 0; i < frame.lightCount; ++i)
    {
        const LightEntry&      src = frame.lights[i];
        GDXLightCBufferEntry&  dst = cb.lights[i];

        dst.position[0] = src.position.x;
        dst.position[1] = src.position.y;
        dst.position[2] = src.position.z;
        dst.position[3] = src.position.w;  // Lichttyp: 0=dir, 1=point, 2=spot

        dst.direction[0] = src.direction.x;
        dst.direction[1] = src.direction.y;
        dst.direction[2] = src.direction.z;
        dst.direction[3] = 0.0f;

        dst.diffuse[0] = src.diffuse.x;
        dst.diffuse[1] = src.diffuse.y;
        dst.diffuse[2] = src.diffuse.z;
        dst.diffuse[3] = src.diffuse.w;  // radius

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
