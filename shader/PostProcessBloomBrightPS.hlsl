Texture2D gSceneColor : register(t0);
SamplerState gSampler : register(s0);

cbuffer BloomBrightParams : register(b0)
{
    float gThreshold;
    float gIntensity;
    float gPad0;
    float gPad1;
};

struct PSIn
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(PSIn input) : SV_TARGET
{
    float3 scene = gSceneColor.Sample(gSampler, input.uv).rgb;
    float luma = dot(scene, float3(0.2126, 0.7152, 0.0722));
    float mask = saturate((luma - gThreshold) / max(0.0001, 1.0 - gThreshold));
    float3 bloom = scene * mask * gIntensity;
    return float4(bloom, 1.0);
}
