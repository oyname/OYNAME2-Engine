Texture2D    gSceneColor : register(t0);
SamplerState gSampler    : register(s0);

cbuffer ToneMappingParams : register(b0)
{
    float gExposure;   // linear exposure multiplier (1.0 = neutral)
    float gGamma;      // output gamma (2.2 for standard monitors)
    int   gMode;       // 0 = ACES Filmic, 1 = Reinhard
    float gPad;
};

struct PSIn
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
};

// ACES Filmic approximation (Krzysztof Narkowicz, 2015)
float3 ACESFilmic(float3 x)
{
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

// Reinhard (extended luminance-based)
float3 Reinhard(float3 x)
{
    float luma    = dot(x, float3(0.2126, 0.7152, 0.0722));
    float lumaMapped = luma / (1.0 + luma);
    return x * (lumaMapped / max(luma, 0.0001));
}

float4 main(PSIn input) : SV_TARGET
{
    float3 hdr = gSceneColor.Sample(gSampler, input.uv).rgb;

    // Exposure
    hdr *= gExposure;

    // Tonemapping
    float3 ldr;
    if (gMode == 1)
        ldr = Reinhard(hdr);
    else
        ldr = ACESFilmic(hdr);

    // Gamma correction (linear -> monitor gamma)
    float invGamma = 1.0 / max(gGamma, 0.0001);
    ldr = pow(max(ldr, 0.0), invGamma);

    return float4(ldr, 1.0);
}
