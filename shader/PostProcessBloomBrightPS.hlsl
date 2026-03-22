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

    // HDR-safe bright extract: proportionally retain only the energy above threshold.
    // Works correctly for any threshold value including > 1.0.
    // When luma <= threshold: excess = 0, no bloom contribution.
    // When luma > threshold:  extract the above-threshold fraction of the colour.
    float excess = max(0.0, luma - gThreshold);
    float3 bloom = scene * (excess / max(luma, 0.0001)) * gIntensity;

    return float4(bloom, 1.0);
}
