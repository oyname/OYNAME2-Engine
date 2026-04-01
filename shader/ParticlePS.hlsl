// ============================================================
//  ParticlePS.hlsl  --  GIDX Particle System
//  Samples the particle texture atlas and multiplies by
//  per-vertex color (RGB) and alpha.
// ============================================================

Texture2D    gParticleTex : register(t0);
SamplerState gSampler     : register(s0);

struct PSIn
{
    float4 PosH  : SV_POSITION;
    float2 UV    : TEXCOORD0;
    float4 Color : COLOR0;   // RGB = tint (0..1),  A = alpha
};

float4 PSMain(PSIn pin) : SV_TARGET
{
    float4 texColor = gParticleTex.Sample(gSampler, pin.UV);

    // Modulate texture by per-particle tint and alpha
    float4 result;
    result.rgb = texColor.rgb * pin.Color.rgb;
    result.a   = texColor.a   * pin.Color.a;

    // Discard fully transparent fragments (saves EarlyZ bandwidth)
    clip(result.a - 0.001f);

    return result;
}
