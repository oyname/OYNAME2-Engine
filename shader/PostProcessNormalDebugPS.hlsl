Texture2D gSceneNormals : register(t0);
SamplerState gSampler : register(s1);

struct VSOut
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
};

float4 main(VSOut input) : SV_Target
{
    float3 encoded = gSceneNormals.Sample(gSampler, input.uv).rgb;
    return float4(encoded, 1.0f);
}
