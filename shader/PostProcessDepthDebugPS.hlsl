Texture2D gSceneDepth : register(t0);
SamplerState gSampler : register(s0);

cbuffer DepthDebugParams : register(b0)
{
    float gNearPlane;
    float gFarPlane;
    uint  gIsOrtho;
    uint  gFlags;
};

struct PSIn
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float LinearizePerspectiveDepth(float rawDepth, float nearPlane, float farPlane)
{
    return (nearPlane * farPlane) / max(farPlane - rawDepth * (farPlane - nearPlane), 1e-6);
}

float4 main(PSIn input) : SV_TARGET
{
    float rawDepth = saturate(gSceneDepth.Sample(gSampler, input.uv).r);

    const bool useLinearDepth = (gFlags & 1u) != 0u;

    float linear01 = rawDepth;
    if (useLinearDepth)
    {
        if (gIsOrtho != 0u)
        {
            linear01 = rawDepth;
        }
        else
        {
            float linearDepth = LinearizePerspectiveDepth(rawDepth, gNearPlane, gFarPlane);
            linear01 = saturate((linearDepth - gNearPlane) / max(gFarPlane - gNearPlane, 1e-6));
        }
    }

    float v = saturate(pow(1.0f - linear01, 0.35f));
    return float4(v, v, v, 1.0f);
}
