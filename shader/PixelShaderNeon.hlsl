cbuffer FrameConstants : register(b1)
{
    row_major float4x4 gView;
    row_major float4x4 gProj;
    row_major float4x4 gViewProj;
    float4             gCameraPos;
    row_major float4x4 gShadowViewProj;
};

cbuffer MaterialConstants : register(b2)
{
    float4   gBaseColor;
    float4   gSpecularColor;
    float4   gEmissiveColor;
    float4   gUVTilingOffset;
    float4   gUVDetailTilingOffset;
    float4   gUVNormalTilingOffset;
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

struct PS_OUTPUT
{
    float4 color  : SV_Target0;
    float4 normal : SV_Target1;
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

float3 EncodeViewNormal(float3 worldNormal)
{
    float3x3 view3x3 = (float3x3)gView;
    float3 viewNormal = mul(worldNormal, view3x3);
    viewNormal = normalize(viewNormal);
    return viewNormal * 0.5f + 0.5f;
}

PS_OUTPUT main(PS_INPUT input)
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
    float brightness = 0.52f + 0.12f * sin(time * 2.5f + plasma * 6.2831853f);
    float3 color = HsvToRgb(hue, 0.95f, brightness);
    color = pow(max(color, 0.0f), 1.05f);

    float3 tintedBase = lerp(float3(1.0f, 1.0f, 1.0f), max(gBaseColor.rgb, 0.0f), 0.65f);
    color *= tintedBase;

    float3 emissive = max(gEmissiveColor.rgb, 0.0f) * 0.12f;
    float3 finalColor = color + emissive;

    float3 dpdx = ddx(input.worldPosition);
    float3 dpdy = ddy(input.worldPosition);
    float3 worldNormal = normalize(cross(dpdx, dpdy));

    float alpha = saturate(gBaseColor.a * (1.0f - gTransparency));

    PS_OUTPUT o;
    o.color = float4(finalColor, alpha);
    o.normal = float4(EncodeViewNormal(worldNormal), 1.0f);
    return o;
}
