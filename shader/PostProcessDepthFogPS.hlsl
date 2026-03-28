Texture2D gSceneColor : register(t0);
Texture2D gSceneDepth : register(t1);
SamplerState gSampler : register(s1);

struct PSIn
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(PSIn input) : SV_TARGET
{
    float3 scene = gSceneColor.Sample(gSampler, input.uv).rgb;
    float depth = saturate(gSceneDepth.Sample(gSampler, input.uv).r);

    // einfacher Test-Fog: fern = mehr Nebel
    float fogAmount = smoothstep(0.55f, 0.98f, depth);
    float3 fogColor = float3(0.62f, 0.68f, 0.78f);

    float3 color = lerp(scene, fogColor, fogAmount * 0.75f);
    return float4(color, 1.0f);
}
