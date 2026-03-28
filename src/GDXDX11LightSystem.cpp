// GDXDX11LightSystem.cpp — DirectX 11 Lichtsystem.
// Scannt LightComponent + WorldTransformComponent.
// Befüllt FrameData.lights (Directional, Point, Spot).
// Berechnet Cascaded Shadow Maps (CSM) für das erste Directional-Licht
// mit castShadows=true.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include "Core/GDXMath.h"
#include "Core/GDXMathOps.h"
#include "GDXMathHelpers.h"
#include <cstring>
#include <cmath>
#include <algorithm>

#include "GDXDX11LightSystem.h"
#include "Components.h"

using namespace DirectX;

// ---------------------------------------------------------------------------
// CSM: Frustum-Ecken einer Kamera-Scheibe berechnen.
// nearZ / farZ sind View-Space-Tiefen (positiv).
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// CSM: Frustum-Ecken einer Kaskaden-Scheibe in Weltkoordinaten.
// nearZ / farZ = View-Space-Tiefen der Scheibe (positiv, entlang Forward).
// ---------------------------------------------------------------------------
static void BuildFrustumCornersViewSpace(
    const XMMATRIX& viewProj,
    float nearZ, float farZ,
    XMVECTOR outCorners[8])
{
    const XMMATRIX inv = XMMatrixInverse(nullptr, viewProj);

    // 8 NDC-Ecken des gesamten Kamera-Frustums (DX: z=0=near, z=1=far)
    static const XMFLOAT3 ndc[8] = {
        { -1.0f, -1.0f, 0.0f }, { -1.0f,  1.0f, 0.0f },
        {  1.0f,  1.0f, 0.0f }, {  1.0f, -1.0f, 0.0f },
        { -1.0f, -1.0f, 1.0f }, { -1.0f,  1.0f, 1.0f },
        {  1.0f,  1.0f, 1.0f }, {  1.0f, -1.0f, 1.0f },
    };

    XMVECTOR worldFull[8];
    for (int i = 0; i < 8; ++i)
        worldFull[i] = XMVector3TransformCoord(XMLoadFloat3(&ndc[i]), inv);

    // Kamera-Position und Forward aus inv(VP)
    const XMVECTOR camPos   = XMVector3TransformCoord(XMVectorZero(), inv);
    const XMVECTOR fwdPoint = XMVector3TransformCoord(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), inv);
    const XMVECTOR forward  = XMVector3Normalize(XMVectorSubtract(fwdPoint, camPos));

    // Depth des Near- und Far-Plane des gesamten Frustums entlang Forward
    // Alle 4 Near-Ecken haben dieselbe Tiefe — Ecke 0 reicht.
    const float camNearDepth = XMVectorGetX(
        XMVector3Dot(XMVectorSubtract(worldFull[0], camPos), forward));
    const float camFarDepth  = XMVectorGetX(
        XMVector3Dot(XMVectorSubtract(worldFull[4], camPos), forward));

    const float range = (std::max)(camFarDepth - camNearDepth, 1e-4f);
    const float tNear = (std::max)(0.0f, (std::min)((nearZ - camNearDepth) / range, 1.0f));
    const float tFar  = (std::max)(0.0f, (std::min)((farZ  - camNearDepth) / range, 1.0f));

    // Kaskaden-Ecken durch Lerp auf den Frustum-Strahlen
    for (int i = 0; i < 4; ++i)
    {
        outCorners[i]     = XMVectorLerp(worldFull[i],     worldFull[i + 4], tNear);
        outCorners[i + 4] = XMVectorLerp(worldFull[i],     worldFull[i + 4], tFar);
    }
}

// ---------------------------------------------------------------------------
// CSM: Orthographische Light-Matrix aus Frustum-Ecken.
// Texel-Snapping für Shimmer-freie Schatten.
// ---------------------------------------------------------------------------
static XMMATRIX BuildCascadeLightMatrix(
    const XMVECTOR frustumCorners[8],
    const XMVECTOR& lightDir,
    uint32_t shadowMapSize)
{
    // Centroid der Scheibe → stabile Light-View-Basis
    XMVECTOR center = XMVectorZero();
    for (int i = 0; i < 8; ++i)
        center = XMVectorAdd(center, frustumCorners[i]);
    center = XMVectorScale(center, 1.0f / 8.0f);

    const XMVECTOR up = (fabsf(XMVectorGetY(lightDir)) > 0.99f)
        ? XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f)
        : XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    // Light weit zurücksetzen damit Shadow-Caster hinter dem Frustum erfasst werden
    const XMVECTOR lightPos   = XMVectorSubtract(center, XMVectorScale(lightDir, 500.0f));
    const XMMATRIX lightView  = XMMatrixLookAtLH(lightPos, center, up);

    // AABB der Kaskaden-Ecken in Light-Space
    XMVECTOR minLS = XMVectorSet( 1e30f,  1e30f,  1e30f, 0.0f);
    XMVECTOR maxLS = XMVectorSet(-1e30f, -1e30f, -1e30f, 0.0f);
    for (int i = 0; i < 8; ++i)
    {
        XMVECTOR p = XMVector3TransformCoord(frustumCorners[i], lightView);
        minLS = XMVectorMin(minLS, p);
        maxLS = XMVectorMax(maxLS, p);
    }

    // Z-Bereich: großzügiger fester Pullback damit Shadow-Caster hinter
    // der Kaskade (z.B. hohe Objekte) noch in die Depth-Map passen.
    // Kein relativer zRange-Faktor — der war zu klein bei engen Kaskaden (Loch-Artefakt).
    const float zNear = XMVectorGetZ(minLS) - 100.0f;
    const float zFar  = XMVectorGetZ(maxLS) + 20.0f;

    // Texel-Snapping: XY-Bounds auf Shadow-Map-Texelgröße runden
    float width      = XMVectorGetX(maxLS) - XMVectorGetX(minLS);
    float height     = XMVectorGetY(maxLS) - XMVectorGetY(minLS);
    float texelSizeX = width  / static_cast<float>(shadowMapSize);
    float texelSizeY = height / static_cast<float>(shadowMapSize);

    float l = std::floor(XMVectorGetX(minLS) / texelSizeX) * texelSizeX;
    float r = l + std::ceil(width  / texelSizeX) * texelSizeX;
    float b = std::floor(XMVectorGetY(minLS) / texelSizeY) * texelSizeY;
    float t = b + std::ceil(height / texelSizeY) * texelSizeY;

    const XMMATRIX lightProj = XMMatrixOrthographicOffCenterLH(l, r, b, t, zNear, zFar);
    return XMMatrixMultiply(lightView, lightProj);
}

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

void GDXDX11LightSystem::FillFrameData(Registry& registry, FrameData& frame)
{
    frame.lightCount        = 0u;
    frame.hasShadowPass     = false;
    frame.shadowCascadeCount = 0u;
    frame.shadowCasterMask  = 0xFFFFFFFFu;
    frame.lightAffectMask   = 0xFFFFFFFFu;

    registry.View<LightComponent, WorldTransformComponent>(
        [&](EntityID, LightComponent& lc, WorldTransformComponent& wt)
        {
            if (frame.lightCount >= MAX_LIGHTS) return;

            const uint32_t i = frame.lightCount++;
            LightEntry&    le = frame.lights[i];

            le.position.x = wt.matrix._41;
            le.position.y = wt.matrix._42;
            le.position.z = wt.matrix._43;

            if      (lc.kind == LightKind::Point) le.position.w = 1.0f;
            else if (lc.kind == LightKind::Spot)  le.position.w = 2.0f;
            else                                   le.position.w = 0.0f;

            le.direction.x = wt.matrix._31;
            le.direction.y = wt.matrix._32;
            le.direction.z = wt.matrix._33;
            le.direction.w = lc.castShadows ? 1.0f : 0.0f;

            le.diffuse.x = lc.diffuseColor.x * lc.intensity;
            le.diffuse.y = lc.diffuseColor.y * lc.intensity;
            le.diffuse.z = lc.diffuseColor.z * lc.intensity;
            le.diffuse.w = lc.radius;

            le.radius         = lc.radius;
            le.intensity      = lc.intensity;
            le.innerCosAngle  = cosf(GDX::ToRadians(lc.innerConeAngle));
            le.outerCosAngle  = cosf(GDX::ToRadians(lc.outerConeAngle));

            // --- Cascaded Shadow Maps (erstes Directional mit castShadows) ---
            if (!frame.hasShadowPass
                && lc.kind == LightKind::Directional
                && lc.castShadows)
            {
                const XMVECTOR lightDir = XMVector3Normalize(
                    XMVectorSet(le.direction.x, le.direction.y, le.direction.z, 0.0f));

                const uint32_t numCascades = (std::clamp)(
                    lc.shadowCascadeCount, 1u, MAX_SHADOW_CASCADES);

                const float camNear = (std::max)(frame.cameraNearPlane, 0.001f);
                const float camFarCamera = (std::max)(frame.cameraFarPlane, camNear + 1.0f);
                const float camFarShadow = (std::max)(lc.shadowFar, camNear + 1.0f);
                const float camFar = (std::min)(camFarCamera, camFarShadow);
                const float lambda  = (std::clamp)(lc.shadowCascadeLambda, 0.0f, 1.0f);

                // Praktisches Split-Schema (PSSM):
                //   split_i = lerp(linear_i, log_i, lambda)
                float splits[MAX_SHADOW_CASCADES + 1];
                splits[0] = camNear;
                for (uint32_t c = 1; c <= numCascades; ++c)
                {
                    const float frac  = static_cast<float>(c) / static_cast<float>(numCascades);
                    const float lin   = camNear + (camFar - camNear) * frac;
                    const float logS  = camNear * powf(camFar / camNear, frac);
                    splits[c] = lin + lambda * (logS - lin);
                }

                for (uint32_t c = 0; c < numCascades; ++c)
                {
                    XMVECTOR corners[8];
                    BuildFrustumCornersViewSpace(
                        GDXMathHelpers::LoadMatrix4(frame.viewProjMatrix),
                        splits[c], splits[c + 1],
                        corners);

                    const XMMATRIX cascadeVP = BuildCascadeLightMatrix(
                        corners, lightDir, lc.shadowMapSize > 0u ? lc.shadowMapSize : 2048u);

                    GDXMathHelpers::StoreMatrix4(frame.shadowCascadeViewProj[c], cascadeVP);
                    frame.shadowCascadeSplits[c] = splits[c + 1];
                }

                frame.shadowCascadeCount = numCascades;

                // Äußerste Kaskade als Frustum-Culling-Matrix beibehalten
                GDXMathHelpers::StoreMatrix4(frame.shadowViewProjMatrix,
                    GDXMathHelpers::LoadMatrix4(frame.shadowCascadeViewProj[numCascades - 1]));

                frame.hasShadowPass    = true;
                frame.shadowCasterMask = lc.shadowLayerMask;
                frame.lightAffectMask  = lc.affectLayerMask;
            }
        });

    // GPU upload intentionally absent here — call UploadLightBuffer() at execution time.
}

void GDXDX11LightSystem::UploadLightBuffer(const FrameData& frame, ID3D11DeviceContext* ctx)
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
        const LightEntry&         src = frame.lights[i];
        GDXDX11LightCBufferEntry& dst = cb.lights[i];

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

    ctx->VSSetConstantBuffers(3, 1, &m_lightBuffer);
    // PixelShader.hlsl nutzt b3 fuer TileInfo und b4 fuer Legacy-Licht-Fallback.
    ctx->PSSetConstantBuffers(4, 1, &m_lightBuffer);
}
