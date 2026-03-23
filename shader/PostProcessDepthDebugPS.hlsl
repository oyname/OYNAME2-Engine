Texture2D gSceneDepth : register(t0);
SamplerState gSampler : register(s0);

struct PSIn
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(PSIn input) : SV_TARGET
{
    float rawDepth = gSceneDepth.Sample(gSampler, input.uv).r;

    // Debug-Visualisierung: nah = hell, fern = dunkel.
    // Die Wurzel spreizt den typischerweise stark zusammengedrückten Tiefenbereich etwas auf.
    float v = saturate(pow(1.0 - rawDepth, 0.25));
    return float4(v, v, v, 1.0);
}
