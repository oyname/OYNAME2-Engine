// ShadowPixelShader.hlsl — KROM Engine
// Unified via preprocessor define:
//   ALPHA_TEST — samples albedo and discards below gAlphaCutoff
//   (no define) — depth-only, empty PS

#ifdef ALPHA_TEST

#include "include/CBuffer_Material.hlsli"

Texture2D    gAlbedo  : register(t0);
SamplerState gSampler : register(s0);

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
};

void main(PS_INPUT input)
{
    float2 uv = input.texCoord * gUVTilingOffset.xy + gUVTilingOffset.zw;
    float4 c  = gAlbedo.Sample(gSampler, uv) * gBaseColor;
    if (c.a < gAlphaCutoff)
        discard;
}

#else

void main() {}

#endif
