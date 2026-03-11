cbuffer FrameConstants : register(b1)
{
    row_major float4x4 gView;
    row_major float4x4 gProj;
    row_major float4x4 gViewProj;
    float4 gCameraPos;
    row_major float4x4 gShadowViewProj;
}

cbuffer MaterialConstants : register(b2)
{
    float4 gBaseColor;
    float4 gSpecularColor;
    float4 gEmissiveColor;
    float4 gUVTilingOffset;
    float gMetallic;
    float gRoughness;
    float gNormalScale;
    float gOcclusionStrength;
    float gShininess;
    float gTransparency;
    float gAlphaCutoff;
    float gReceiveShadows;
    float gBlendMode;
    float gBlendFactor;
    uint gFlags;
    float _gPad0;
}

struct VertexOut
{
    float4 posC : SV_POSITION;
    float3 posW : TEXCOORD0;
    float3 nrm : TEXCOORD1;
    float2 uv : TEXCOORD2;
};

float4 main(VertexOut p) : SV_Target
{
    float3 N = normalize(p.nrm);
    float3 L = normalize(float3(0.5f, 1.0f, -0.5f));

    float d = max(dot(N, L), 0.0f);

    float3 ambient = float3(0.08f, 0.08f, 0.10f);
    float3 color = gBaseColor.rgb * (ambient + d) + gEmissiveColor.rgb;

    float alpha = gBaseColor.a * (1.0f - gTransparency);
    if ((gFlags & 1u) != 0u && alpha < gAlphaCutoff)
        discard;

    return float4(color, alpha);
}
