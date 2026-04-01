Texture2D gSceneColor : register(t0);
Texture2D gSceneDepth : register(t1);
SamplerState gSampler : register(s1);

cbuffer FogParams : register(b0)
{
    float gFogColorR;
    float gFogColorG;
    float gFogColorB;
    uint  gFogMode;

    float gFogStart;
    float gFogEnd;
    float gFogDensity;
    float gFogMaxOpacity;

    float gFogPower;
    float gFogHeightStart;
    float gFogHeightEnd;
    float gFogHeightStrength;

    float gCameraNearPlane;
    float gCameraFarPlane;
    float gProjScaleX;
    float gProjScaleY;

    uint  gFogEnabled;
    uint  gFogHeightEnabled;
    uint  gCameraIsOrtho;
    uint  _fogPad0;

    row_major float4x4 gInvView;
};

struct PSIn
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float LinearizeDepth(float rawDepth)
{
    if (gCameraIsOrtho != 0u)
        return lerp(gCameraNearPlane, gCameraFarPlane, rawDepth);

    return (gCameraNearPlane * gCameraFarPlane) /
           max(gCameraFarPlane - rawDepth * (gCameraFarPlane - gCameraNearPlane), 1e-5f);
}

float3 ReconstructViewPos(float2 uv, float linearDepth)
{
    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);

    if (gCameraIsOrtho != 0u)
    {
        return float3(
            ndc.x / max(gProjScaleX, 1e-5f),
            ndc.y / max(gProjScaleY, 1e-5f),
            linearDepth);
    }

    return float3(
        ndc.x * linearDepth / max(gProjScaleX, 1e-5f),
        ndc.y * linearDepth / max(gProjScaleY, 1e-5f),
        linearDepth);
}

float3 ReconstructWorldPos(float2 uv, float rawDepth)
{
    float linearDepth = LinearizeDepth(rawDepth);
    float3 viewPos = ReconstructViewPos(uv, linearDepth);
    return mul(float4(viewPos, 1.0f), gInvView).xyz;
}

float ComputeDepthFog(float rawDepth)
{
    if (gFogMode == 0u)
    {
        float denom = max(gFogEnd - gFogStart, 1e-5f);
        return saturate((rawDepth - gFogStart) / denom);
    }

    float linearDepth = LinearizeDepth(rawDepth);
    float d = saturate(linearDepth / max(gCameraFarPlane, 1e-5f));
    if (gFogMode == 1u)
        return saturate(1.0f - exp(-d * max(gFogDensity, 0.0f)));

    return saturate(1.0f - exp(-d * d * max(gFogDensity, 0.0f)));
}

float4 main(PSIn input) : SV_TARGET
{
    float3 scene = gSceneColor.Sample(gSampler, input.uv).rgb;

    if (gFogEnabled == 0u)
        return float4(scene, 1.0f);

    float rawDepth = saturate(gSceneDepth.Sample(gSampler, input.uv).r);
    float fogAmount = ComputeDepthFog(rawDepth);

    if (gFogHeightEnabled != 0u)
    {
        float worldY = ReconstructWorldPos(input.uv, rawDepth).y;
        float denom = max(gFogHeightEnd - gFogHeightStart, 1e-5f);
        float heightFactor = saturate((worldY - gFogHeightStart) / denom);
        fogAmount *= lerp(1.0f, heightFactor, saturate(gFogHeightStrength));
    }

    fogAmount = saturate(pow(max(fogAmount, 0.0f), max(gFogPower, 1e-4f)) * saturate(gFogMaxOpacity));

    float3 fogColor = float3(gFogColorR, gFogColorG, gFogColorB);
    float3 color = lerp(scene, fogColor, fogAmount);
    return float4(color, 1.0f);
}
