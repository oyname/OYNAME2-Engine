Texture2D    gAlbedo  : register(t0);
SamplerState gSampler : register(s0);

cbuffer MaterialConstants : register(b2)
{
    float4   gBaseColor;
    float4   gSpecularColor;
    float4   gEmissiveColor;
    float4   gUVTilingOffset;
    float    gMetallic;
    float    gRoughness;
    float    gNormalScale;
    float    gOcclusionStrength;
    float    gShininess;
    float    gTransparency;
    float    gAlphaCutoff;
    float    gReceiveShadows;
    float    gBlendMode;
    float    gBlendFactor;
    uint     gFlags;
    float    _pad0;
};

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
};

void main(PS_INPUT input)
{
    float2 uv = input.texCoord * gUVTilingOffset.xy + gUVTilingOffset.zw;
    float4 c = gAlbedo.Sample(gSampler, uv) * gBaseColor;
    if (c.a < gAlphaCutoff)
        discard;
}
