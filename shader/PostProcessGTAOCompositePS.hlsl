Texture2D gAO : register(t0);
Texture2D gOriginalScene : register(t1);
SamplerState gSampler : register(s1);

cbuffer GTAOCompositeParams : register(b0)
{
    float gMinVisibility;
    float gStrength;
    float gHighlightProtectStart;
    float gHighlightProtectEnd;
};

struct VSOut
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

float4 main(VSOut input) : SV_Target
{
    float rawAO = saturate(gAO.SampleLevel(gSampler, input.uv, 0).r);
    float3 scene = gOriginalScene.SampleLevel(gSampler, input.uv, 0).rgb;

    float occlusion = saturate(1.0f - rawAO);
    occlusion = saturate(occlusion * max(gStrength, 0.0f));

    float visibility = max(1.0f - occlusion, gMinVisibility);

    float luma = dot(scene, float3(0.2126f, 0.7152f, 0.0722f));
    float peak = max(scene.r, max(scene.g, scene.b));
    float highlightProtect = smoothstep(gHighlightProtectStart, gHighlightProtectEnd, max(luma, peak));
    float protectedVisibility = max(visibility, 0.92f);
    visibility = lerp(visibility, protectedVisibility, highlightProtect);

    return float4(scene * visibility, 1.0f);
}
