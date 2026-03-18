Texture2D gSceneColor : register(t0);
SamplerState gSampler : register(s0);

cbuffer BloomBlurParams : register(b0)
{
    float2 gTexelSize;
    float2 gDirection;
};

struct PSIn
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(PSIn input) : SV_TARGET
{
    float2 stepUV = gTexelSize * gDirection;

    float3 color = gSceneColor.Sample(gSampler, input.uv).rgb * 0.227027f;
    color += gSceneColor.Sample(gSampler, input.uv + stepUV * 1.384615f).rgb * 0.316216f;
    color += gSceneColor.Sample(gSampler, input.uv - stepUV * 1.384615f).rgb * 0.316216f;
    color += gSceneColor.Sample(gSampler, input.uv + stepUV * 3.230769f).rgb * 0.070270f;
    color += gSceneColor.Sample(gSampler, input.uv - stepUV * 3.230769f).rgb * 0.070270f;

    return float4(color, 1.0);
}
