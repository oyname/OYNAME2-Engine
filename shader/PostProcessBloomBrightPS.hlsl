Texture2D gSceneColor : register(t0);
SamplerState gSampler : register(s1);

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
    float3 scene = gSceneColor.SampleLevel(gSampler, input.uv, 0).rgb;
    float luma = dot(scene, float3(0.2126, 0.7152, 0.0722));

    // Soft-knee threshold so bloom starts before hard clipping and gives a visible halo.
    const float knee = max(gThreshold * 0.5f, 0.05f);
    float soft = saturate((luma - gThreshold + knee) / (2.0f * knee));
    float contribution = max(luma - gThreshold, 0.0f) + knee * soft * soft;
    float3 bloom = scene * (contribution / max(luma, 1e-4f)) * gIntensity;

    return float4(max(bloom, 0.0f), 1.0f);
}
