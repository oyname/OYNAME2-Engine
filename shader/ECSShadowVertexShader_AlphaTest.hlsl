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

struct VS_INPUT  { float3 position : POSITION; float2 texCoord : TEXCOORD0; };
struct VS_OUTPUT { float4 position : SV_POSITION; float2 texCoord : TEXCOORD0; };

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT o;
    float4 worldPos = mul(float4(input.position, 1.0f), gWorld);
    o.position = mul(worldPos, gShadowViewProj);
    o.texCoord = input.texCoord;
    return o;
}
