// VertexShader.hlsl — KROM Engine
// Unified VS via preprocessor defines:
//   HAS_VERTEX_COLOR  — liest COLOR0-Stream
//   HAS_SKINNING      — liest BLENDINDICES0/BLENDWEIGHT0, transformiert per Bone

#include "include/CBuffer_Entity.hlsli"
#include "include/CBuffer_Frame.hlsli"
#ifdef HAS_SKINNING
#include "include/CBuffer_Skin.hlsli"
#endif

struct VS_INPUT
{
    float3 position    : POSITION;
    float3 normal      : NORMAL;
#ifdef HAS_VERTEX_COLOR
    float4 color       : COLOR;
#endif
    float2 texCoord    : TEXCOORD0;
#ifdef HAS_SKINNING
    uint4  boneIndices : BLENDINDICES0;
    float4 boneWeights : BLENDWEIGHT0;
#endif
};

struct VS_OUTPUT
{
    float4 position      : SV_POSITION;
    float3 normal        : NORMAL;
    float3 worldPosition : TEXCOORD1;
    float2 texCoord      : TEXCOORD0;
    float3 viewDirection : TEXCOORD3;
    float2 texCoord1     : TEXCOORD4;
    float4 vertexColor   : COLOR0;
};

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT o;

#ifdef HAS_SKINNING
    float4x4 skin    = BuildSkinMatrix(input.boneIndices, input.boneWeights);
    float4 localPos  = mul(float4(input.position, 1.0f), skin);
    float4 worldPos  = mul(localPos, gWorld);
    float3x3 skinN   = (float3x3)mul(skin, gWorldInverseTranspose);
    o.normal         = normalize(mul(input.normal, skinN));
#else
    float4 worldPos  = mul(float4(input.position, 1.0f), gWorld);
    o.normal         = normalize(mul(float4(input.normal, 0.0f), gWorldInverseTranspose).xyz);
#endif

    o.worldPosition  = worldPos.xyz;
    o.position       = mul(worldPos, gViewProj);
    o.texCoord       = input.texCoord;
    o.texCoord1      = input.texCoord;
    o.viewDirection  = normalize(gCameraPos.xyz - worldPos.xyz);

#ifdef HAS_VERTEX_COLOR
    o.vertexColor    = input.color;
#else
    o.vertexColor    = float4(1.0f, 1.0f, 1.0f, 1.0f);
#endif

    return o;
}
