Texture2D    gSceneDepth   : register(t0);
Texture2D    gSceneNormals : register(t1);
SamplerState gSampler      : register(s1);

cbuffer GTAOParams : register(b0)
{
    float gTexelW;
    float gTexelH;
    float gRadiusPixels;
    float gThickness;

    float gIntensity;
    float gPower;
    float gNormalBias;
    float gDepthClamp;

    float gNearPlane;
    float gFarPlane;
    float gDepthFadeStart;
    float gDepthFadeEnd;

    float gProjScaleX;
    float gProjScaleY;
    uint  gDirectionCount;
    uint  gStepCount;

    uint  gCameraIsOrtho;
    uint  gDebugView;
    uint  _pad0;
    uint  _pad1;
};

struct VSOut
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
};

static const float PI = 3.14159265359f;
static const uint  MAX_DIRECTIONS = 8u;
static const uint  MAX_STEPS      = 4u;

float LinearizeDepth(float rawDepth)
{
    if (gCameraIsOrtho != 0u)
        return lerp(gNearPlane, gFarPlane, rawDepth);
    return (gNearPlane * gFarPlane) / max(gFarPlane - rawDepth * (gFarPlane - gNearPlane), 1e-5f);
}

float3 DecodeNormal(float3 enc)
{
    return normalize(enc * 2.0f - 1.0f);
}

float3 ReconstructViewPos(float2 uv, float linearDepth)
{
    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    if (gCameraIsOrtho != 0u)
        return float3(ndc.x / max(gProjScaleX, 1e-5f), ndc.y / max(gProjScaleY, 1e-5f), linearDepth);

    return float3(
        ndc.x * linearDepth / max(gProjScaleX, 1e-5f),
        ndc.y * linearDepth / max(gProjScaleY, 1e-5f),
        linearDepth);
}

float Hash12(float2 p)
{
    float h = dot(p, float2(127.1f, 311.7f));
    return frac(sin(h) * 43758.5453123f);
}

float2 Rotate2D(float2 v, float angle)
{
    float s = sin(angle);
    float c = cos(angle);
    return float2(c * v.x - s * v.y, s * v.x + c * v.y);
}

float ThicknessWeight(float3 centerPos, float3 samplePos, float thickness)
{
    float distVS = length(samplePos - centerPos);
    return saturate(1.0f - distVS / max(thickness, 1e-4f));
}

float DirectionalHorizonAO(float2 uv, float3 centerPos, float3 centerNormal, float radiusPixels, float thickness, uint stepCount, float2 dir, float jitter)
{
    float horizonForward = 0.0f;
    float horizonBackward = 0.0f;
    float hitWeightForward = 0.0f;
    float hitWeightBackward = 0.0f;

    [unroll]
    for (uint stepIdx = 0u; stepIdx < MAX_STEPS; ++stepIdx)
    {
        if (stepIdx >= stepCount)
            break;

        float t = (stepIdx + 1.0f) / (float)stepCount;
        float radial = lerp(0.22f, 1.0f, t * t);
        float distPixels = radiusPixels * radial;
        float stepJitter = lerp(0.85f, 1.15f, Hash12(uv * 1499.0f + float2(stepIdx + 1.0f, jitter * 31.0f)));
        float2 stepUV = dir * distPixels * stepJitter * float2(gTexelW, gTexelH);

        [unroll]
        for (uint side = 0u; side < 2u; ++side)
        {
            float2 signedStepUV = (side == 0u) ? stepUV : -stepUV;
            float2 sampleUV = saturate(uv + signedStepUV);
            float sampleRawDepth = gSceneDepth.SampleLevel(gSampler, sampleUV, 0).r;
            if (sampleRawDepth >= 0.999999f)
                continue;

            float sampleDepth = LinearizeDepth(sampleRawDepth);
            float3 samplePos  = ReconstructViewPos(sampleUV, sampleDepth);
            float3 toSample   = samplePos - centerPos;

            // closer sample in front of the receiver only
            if (samplePos.z >= centerPos.z)
                continue;

            float dirLenSq = dot(toSample, toSample);
            if (dirLenSq <= 1e-6f)
                continue;

            float3 toSampleN = toSample * rsqrt(dirLenSq);
            float elevation  = saturate(dot(centerNormal, toSampleN));
            float thickW     = ThicknessWeight(centerPos, samplePos, thickness);
            float proxW      = saturate(1.0f - t * t);
            float sampleOcc  = elevation * thickW * proxW;

            if (side == 0u)
            {
                horizonForward = max(horizonForward, sampleOcc);
                hitWeightForward = max(hitWeightForward, thickW);
            }
            else
            {
                horizonBackward = max(horizonBackward, sampleOcc);
                hitWeightBackward = max(hitWeightBackward, thickW);
            }
        }
    }

    float dirOcc = 0.5f * (horizonForward + horizonBackward);
    float viewFacing = saturate(centerNormal.z);
    dirOcc *= lerp(1.0f, 0.7f, viewFacing * gNormalBias);
    float anyHit = saturate(hitWeightForward + hitWeightBackward);
    return dirOcc * anyHit;
}

float mainAO(float2 uv)
{
    float rawDepth = gSceneDepth.SampleLevel(gSampler, uv, 0).r;
    if (rawDepth >= 0.999999f)
        return 1.0f;

    float centerDepth   = LinearizeDepth(rawDepth);
    float3 centerNormal = DecodeNormal(gSceneNormals.SampleLevel(gSampler, uv, 0).rgb);
    float3 centerPos    = ReconstructViewPos(uv, centerDepth);

    uint dirCount  = clamp(gDirectionCount, 1u, MAX_DIRECTIONS);
    uint stepCount = clamp(gStepCount,      1u, MAX_STEPS);

    float radiusPixels  = clamp(gRadiusPixels, 4.0f, 32.0f);
    float viewThickness = max(gThickness * max(centerDepth * 0.35f, 0.25f), 0.05f);
    float jitterAngle = Hash12(uv * float2(1637.0f, 2131.0f)) * (2.0f * PI);

    float occSum = 0.0f;
    float occWeight = 0.0f;

    [unroll]
    for (uint dirIdx = 0u; dirIdx < MAX_DIRECTIONS; ++dirIdx)
    {
        if (dirIdx >= dirCount)
            break;

        float angle = (PI * (dirIdx + 0.5f)) / (float)dirCount;
        float2 dir = Rotate2D(float2(cos(angle), sin(angle)), jitterAngle);
        float dirOcc = DirectionalHorizonAO(uv, centerPos, centerNormal, radiusPixels, viewThickness, stepCount, dir, angle);
        occSum += dirOcc;
        occWeight += 1.0f;
    }

    if (occWeight <= 1e-5f)
        return 1.0f;

    float occNorm = saturate((occSum / occWeight) * gIntensity);
    float ao = saturate(1.0f - occNorm);
    ao = pow(ao, max(gPower, 0.001f));

    float fade = saturate((gDepthFadeEnd - centerDepth) / max(gDepthFadeEnd - gDepthFadeStart, 1e-4f));
    ao = lerp(1.0f, ao, fade);
    return ao;
}

float4 main(VSOut input) : SV_Target
{
    float ao = mainAO(input.uv);
    return float4(ao, ao, ao, 1.0f);
}
