// ECSShadowVertexShader.hlsl — Depth-Only Shadow Pass.
// Nur POSITION-Stream (GDX_VERTEX_POSITION).
// Adaptiert aus OYNAME VertexShader_Shadow.hlsl.

cbuffer EntityConstants : register(b0)
{
    row_major float4x4 gWorld;
    row_major float4x4 gWorldInverseTranspose;
};

cbuffer FrameConstants : register(b1)
{
    row_major float4x4 gView;
    row_major float4x4 gProj;
    row_major float4x4 gViewProj;
    float4             gCameraPos;
    row_major float4x4 gShadowViewProj;
};

struct VS_INPUT  { float3 position : POSITION; };
struct VS_OUTPUT { float4 position : SV_POSITION; };

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT o;
    float4 worldPos = mul(float4(input.position, 1.0f), gWorld);
    o.position      = mul(worldPos, gShadowViewProj);
    return o;
}
