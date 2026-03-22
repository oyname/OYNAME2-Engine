Texture2D gBloomTexture : register(t0);
Texture2D gSceneColor : register(t1);
SamplerState gSampler : register(s0);

cbuffer BloomCompositeParams : register(b0)
{
    float4 gBloomTint;
    float gBloomStrength;
    float gSceneStrength;
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
    float3 scene = gSceneColor.Sample(gSampler, input.uv).rgb * gSceneStrength;
    float3 bloom = gBloomTexture.Sample(gSampler, input.uv).rgb * gBloomTint.rgb * gBloomStrength;

    // No saturate here: keep HDR values intact so the downstream ToneMap pass
    // can operate on the full linear HDR range.  The backbuffer is UNORM and the
    // ToneMap pass is the sole LDR conversion step.
    float3 color = scene + bloom;
    return float4(color, 1.0);
}
