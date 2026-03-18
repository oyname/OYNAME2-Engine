Texture2D gSceneColor : register(t0);
SamplerState gSampler : register(s0);

cbuffer PostProcessParams : register(b0)
{
    float4 gTintColor;
};

struct PSIn
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(PSIn input) : SV_TARGET
{
    float4 scene = gSceneColor.Sample(gSampler, input.uv);
    return float4(scene.rgb * gTintColor.rgb, scene.a);
}
