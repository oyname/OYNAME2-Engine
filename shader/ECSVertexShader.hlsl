// ECSVertexShader.hlsl — GIDX ECS Engine
// Separate Vertex Streams (multi-stream).
//
// cbuffer:
//   b0: EntityConstants  — World, WorldInverseTranspose
//   b1: FrameConstants   — View, Proj, ViewProj, CameraPos, ShadowViewProj
//
// UV-Sets:
//   TEXCOORD0 — UV0 (primäre Texturkoordinaten)
//   TEXCOORD1 — UV1 (Detail-Map / Lightmap / 2. UV-Set)
//   Kein echtes UV1-Set im Mesh? Executor aliasiert UV0 → Slot 1.
//
// Vertex Color ist kein eigener Pixelshader-Pfad mehr.
// Dieser VS liefert für Meshes ohne Farb-Stream immer Weiß an den PS.

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
    float3 position  : POSITION;
    float3 normal    : NORMAL;
    float2 texCoord  : TEXCOORD0;
    float2 texCoord1 : TEXCOORD1;
};

struct VS_OUTPUT
{
    float4 position           : SV_POSITION;
    float3 normal             : NORMAL;
    float3 worldPosition      : TEXCOORD1;
    float2 texCoord           : TEXCOORD0;
    float4 positionLightSpace : TEXCOORD2;
    float3 viewDirection      : TEXCOORD3;
    float2 texCoord1          : TEXCOORD4;
    float4 vertexColor        : COLOR0;
};

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT o;

    float4 worldPos      = mul(float4(input.position, 1.0f), gWorld);
    o.worldPosition      = worldPos.xyz;
    o.position           = mul(worldPos, gViewProj);
    o.normal             = normalize(mul(input.normal, (float3x3)gWorldInverseTranspose));
    o.texCoord           = input.texCoord;
    o.texCoord1          = input.texCoord1;
    o.positionLightSpace = mul(worldPos, gShadowViewProj);
    o.viewDirection      = normalize(gCameraPos.xyz - worldPos.xyz);
    o.vertexColor        = float4(1.0f, 1.0f, 1.0f, 1.0f);

    return o;
}
