// ECSVertexShader.hlsl — GIDX ECS Engine
// Separate Vertex Streams (multi-stream, wie OYNAME VertexShader.hlsl).
//
// cbuffer:
//   b0: EntityConstants  — World, WorldInverseTranspose
//   b1: FrameConstants   — View, Proj, ViewProj, CameraPos, ShadowViewProj

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

struct VS_INPUT
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 texCoord : TEXCOORD0;
};

struct VS_OUTPUT
{
    float4 position          : SV_POSITION;
    float3 normal            : NORMAL;
    float3 worldPosition     : TEXCOORD1;
    float2 texCoord          : TEXCOORD0;
    float4 positionLightSpace: TEXCOORD2;
    float3 viewDirection     : TEXCOORD3;
};

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT o;

    float4 worldPos  = mul(float4(input.position, 1.0f), gWorld);
    o.worldPosition  = worldPos.xyz;
    o.position       = mul(worldPos, gViewProj);
    o.normal         = normalize(mul(input.normal, (float3x3)gWorldInverseTranspose));
    o.texCoord       = input.texCoord;
    o.positionLightSpace = mul(worldPos, gShadowViewProj);
    o.viewDirection  = normalize(gCameraPos.xyz - worldPos.xyz);

    return o;
}
