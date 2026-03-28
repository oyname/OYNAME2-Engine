// ShadowVertexShader.hlsl — GIDX ECS Engine
// Depth-only shadow pass. Unified via preprocessor defines:
//   HAS_SKINNING  — reads BLENDINDICES0/BLENDWEIGHT0, applies bone transforms
//   ALPHA_TEST    — passes TEXCOORD0 to PS for alpha cutout

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

#ifdef HAS_SKINNING
cbuffer SkinConstants : register(b4)
{
    row_major float4x4 gBones[64];
};
#endif

struct VS_INPUT
{
    float3 position    : POSITION;
#ifdef ALPHA_TEST
    float2 texCoord    : TEXCOORD0;
#endif
#ifdef HAS_SKINNING
    uint4  boneIndices : BLENDINDICES0;
    float4 boneWeights : BLENDWEIGHT0;
#endif
};

struct VS_OUTPUT
{
    float4 position : SV_POSITION;
#ifdef ALPHA_TEST
    float2 texCoord : TEXCOORD0;
#endif
};

#ifdef HAS_SKINNING
row_major float4x4 BuildSkinMatrix(uint4 idx, float4 w)
{
    row_major float4x4 skin  = gBones[idx.x] * w.x;
                       skin += gBones[idx.y] * w.y;
                       skin += gBones[idx.z] * w.z;
                       skin += gBones[idx.w] * w.w;
    return skin;
}
#endif

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT o;

#ifdef HAS_SKINNING
    float4 localPos = mul(float4(input.position, 1.0f), BuildSkinMatrix(input.boneIndices, input.boneWeights));
    float4 worldPos = mul(localPos, gWorld);
#else
    float4 worldPos = mul(float4(input.position, 1.0f), gWorld);
#endif

    o.position = mul(worldPos, gShadowViewProj);

#ifdef ALPHA_TEST
    o.texCoord = input.texCoord;
#endif

    return o;
}
