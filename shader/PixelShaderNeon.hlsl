cbuffer MaterialConstants : register(b2)
{
    float4   gBaseColor;
    float4   gSpecularColor;
    float4   gEmissiveColor;
    float4   gUVTilingOffset;
    float4   gUVDetailTilingOffset;
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
    float4 position      : SV_POSITION;
    float3 worldPosition : TEXCOORD0;
};

float3 HsvToRgb(float h, float s, float v)
{
    float c = v * s;
    float x = c * (1.0f - abs(fmod(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;

    float3 rgb;
    if      (h <  60.0f) rgb = float3(c, x, 0.0f);
    else if (h < 120.0f) rgb = float3(x, c, 0.0f);
    else if (h < 180.0f) rgb = float3(0.0f, c, x);
    else if (h < 240.0f) rgb = float3(0.0f, x, c);
    else if (h < 300.0f) rgb = float3(x, 0.0f, c);
    else                 rgb = float3(c, 0.0f, x);

    return rgb + m;
}

float4 main(PS_INPUT input) : SV_TARGET
{
    float3 wp = input.worldPosition;
    float  time = gUVTilingOffset.x;

    float wave1 = sin(wp.x * 2.0f + time * 1.5f);
    float wave2 = sin(wp.y * 2.5f + time * 1.1f);
    float wave3 = sin(wp.z * 3.0f - time * 0.9f);
    float wave4 = sin((wp.x + wp.z) * 1.8f + time * 2.0f);

    float plasma = (wave1 + wave2 + wave3 + wave4) * 0.25f;
    plasma = plasma * 0.5f + 0.5f;

    float hue = fmod(plasma * 360.0f + time * 40.0f, 360.0f);
    float brightness = 0.75f + 0.25f * sin(time * 3.0f + plasma * 6.2831853f);
    float saturation = 1.0f;

    float3 color = HsvToRgb(hue, saturation, brightness);
    color = pow(color, 0.7f);

    color *= gBaseColor.rgb;
    color += gEmissiveColor.rgb;

    float alpha = saturate(gBaseColor.a * (1.0f - gTransparency));
    return float4(saturate(color), alpha);
}
