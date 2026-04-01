#include "include/PostProcess.hlsli"

Texture2D      gSceneColor : register(t0);
Texture2D      gSceneDepth : register(t1);
Texture2DArray gShadowMap  : register(t2);
SamplerState           gSampler      : register(s1);
SamplerComparisonState gShadowSamp   : register(s7);

cbuffer VolumetricFogParams : register(b0)
{
    float gFogColorR;
    float gFogColorG;
    float gFogColorB;
    float gFogDensity;

    float gAnisotropy;
    float gStartDistance;
    float gMaxDistance;
    float gMaxOpacity;

    float gBaseHeight;
    float gHeightFalloff;
    uint  gStepCount;
    float gShadowStrength;

    float gLightIntensity;
    float gJitterStrength;
    float gNearPlane;
    uint  gCameraIsOrtho;

    float gFarPlane;
    float gProjScaleX;
    float gProjScaleY;
    uint  gCascadeCount;

    float4 gCameraPos;
    float4 gLightDir;
    row_major float4x4 gInvView;
    row_major float4x4 gCascadeViewProj[4];
    float4 gCascadeSplits;
};

static const float PI = 3.14159265359f;
static const uint  MAX_STEPS = 128u;

float LinearizeDepth(float rawDepth)
{
    if (gCameraIsOrtho != 0u)
        return lerp(gNearPlane, gFarPlane, rawDepth);
    return (gNearPlane * gFarPlane) / max(gFarPlane - rawDepth * (gFarPlane - gNearPlane), 1e-5f);
}

float Hash12(float2 p)
{
    float h = dot(p, float2(127.1f, 311.7f));
    return frac(sin(h) * 43758.5453123f);
}

float3 GetWorldRayDir(float2 uv)
{
    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    float3 dirVS = normalize(float3(ndc.x / max(gProjScaleX, 1e-5f), ndc.y / max(gProjScaleY, 1e-5f), 1.0f));
    return normalize(mul(float4(dirVS, 0.0f), gInvView).xyz);
}

float3 GetCameraForward()
{
    return normalize(mul(float4(0.0f, 0.0f, 1.0f, 0.0f), gInvView).xyz);
}

uint SelectCascade(float viewDepth)
{
    float splits[4] = { gCascadeSplits.x, gCascadeSplits.y, gCascadeSplits.z, gCascadeSplits.w };
    uint count = max(gCascadeCount, 1u);
    uint cascade = count - 1u;
    [unroll] for (uint c = 0u; c < 4u; ++c)
    {
        if (c >= count) break;
        if (viewDepth < splits[c]) { cascade = c; break; }
    }
    return cascade;
}

float SampleShadow(float3 worldPos, float viewDepth)
{
    if (gCascadeCount == 0u)
        return 1.0f;

    uint cascade = SelectCascade(viewDepth);
    float4 lightSpacePos = mul(float4(worldPos, 1.0f), gCascadeViewProj[cascade]);
    if (lightSpacePos.w <= 1e-5f)
        return 1.0f;

    float3 proj = lightSpacePos.xyz / lightSpacePos.w;
    proj.x = proj.x * 0.5f + 0.5f;
    proj.y = -proj.y * 0.5f + 0.5f;

    if (proj.x < 0.0f || proj.x > 1.0f || proj.y < 0.0f || proj.y > 1.0f || proj.z < 0.0f || proj.z > 1.0f)
        return 1.0f;

    uint tw, th, elems;
    gShadowMap.GetDimensions(tw, th, elems);
    float texelSize = 1.0f / max((float)tw, 1.0f);
    float depth = proj.z - 0.00012f;

    float shadow = 0.0f;
    [unroll] for (int dy = -1; dy <= 1; ++dy)
    [unroll] for (int dx = -1; dx <= 1; ++dx)
    {
        shadow += gShadowMap.SampleCmpLevelZero(
            gShadowSamp,
            float3(proj.xy + float2(dx, dy) * texelSize, (float)cascade),
            depth);
    }
    return shadow / 9.0f;
}

float PhaseHG(float cosTheta, float g)
{
    float gg = g * g;
    float denom = pow(max(1.0f + gg - 2.0f * g * cosTheta, 1e-4f), 1.5f);
    return (1.0f - gg) / max(4.0f * PI * denom, 1e-4f);
}

float ComputeHeightFactor(float worldY)
{
    float h = max(worldY - gBaseHeight, 0.0f);
    return exp(-h * max(gHeightFalloff, 0.0f));
}

float4 main(PSIn input) : SV_Target
{
    float4 scene = gSceneColor.Sample(gSampler, input.uv);
    float rawDepth = gSceneDepth.SampleLevel(gSampler, input.uv, 0).r;

    if (gStepCount == 0u || gFogDensity <= 0.0f)
        return scene;

    bool noGeometryHit = (rawDepth <= 1e-6f) || (rawDepth >= 0.999999f);
    float linearDepth = noGeometryHit ? gMaxDistance : LinearizeDepth(rawDepth);

    float3 worldRayDir = GetWorldRayDir(input.uv);
    float3 camForward = GetCameraForward();
    float cosAngle = max(dot(worldRayDir, camForward), 1e-4f);
    float rayDepth = noGeometryHit ? gMaxDistance : min(linearDepth / cosAngle, gMaxDistance);
    float marchEnd = min(rayDepth, gMaxDistance);
    if (marchEnd <= gStartDistance)
        return scene;

    uint stepCount = clamp(gStepCount, 1u, MAX_STEPS);
    float rayLength = max(marchEnd - gStartDistance, 1e-4f);
    float stepLen = rayLength / (float)stepCount;
    float jitter = (Hash12(input.uv * float2(1130.0f, 706.0f)) - 0.5f) * gJitterStrength;

    float3 lightDir = normalize(gLightDir.xyz);
    float cosTheta = dot(worldRayDir, -lightDir);
    float phase = PhaseHG(cosTheta, clamp(gAnisotropy, -0.95f, 0.95f));

    float transmittance = 1.0f;
    float3 accum = 0.0f;
    float3 fogColor = float3(gFogColorR, gFogColorG, gFogColorB);

    [loop]
    for (uint stepIdx = 0u; stepIdx < MAX_STEPS; ++stepIdx)
    {
        if (stepIdx >= stepCount)
            break;

        float t = ((float)stepIdx + 0.5f + jitter) / (float)stepCount;
        t = saturate(t);
        float dist = lerp(gStartDistance, marchEnd, t);
        float3 worldPos = gCameraPos.xyz + worldRayDir * dist;

        float localDensity = gFogDensity * ComputeHeightFactor(worldPos.y);
        if (localDensity <= 1e-5f)
            continue;

        float shadow = SampleShadow(worldPos, dist * cosAngle);
        float lightVis = lerp(1.0f, shadow, saturate(gShadowStrength));
        float extinction = exp(-localDensity * stepLen);
        float scattering = (1.0f - extinction);
        float3 inscatter = fogColor * (gLightIntensity * phase * lightVis) * scattering;

        accum += transmittance * inscatter;
        transmittance *= extinction;
    }

    float fogAlpha = saturate(1.0f - transmittance);
    fogAlpha = min(fogAlpha, saturate(gMaxOpacity));
    float3 finalColor = scene.rgb * (1.0f - fogAlpha) + accum;
    return float4(finalColor, scene.a);
}
