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
    const float alpha = texColor.a * pin.Color.a;

    // Kill effectively invisible fragments before the remaining blend work.
    clip(alpha - (1.0f / 255.0f));

    float4 result;
    result.rgb = texColor.rgb * pin.Color.rgb;
    result.a   = alpha;
    return result;
}
