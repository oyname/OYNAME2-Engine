Texture2D    gSceneColor : register(t0);
SamplerState gSampler    : register(s1);

cbuffer ToneMappingParams : register(b0)
{
    float gExposure;
    float gGamma;
    int   gMode;
    float gPad;
};

struct PSIn
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
};

float3 ACESFilmic(float3 x)
{
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float3 Reinhard(float3 x)
{
    return x / (1.0f + x);
}

float4 main(PSIn input) : SV_TARGET
{
    float exposure = max(gExposure, 0.001f);
    float gamma = max(gGamma, 0.001f);

    float3 hdr = gSceneColor.SampleLevel(gSampler, input.uv, 0).rgb * exposure;
    float3 mapped = (gMode == 1) ? Reinhard(hdr) : ACESFilmic(hdr);
    float3 outColor = pow(saturate(mapped), 1.0f / gamma);
    return float4(outColor, 1.0f);
}
