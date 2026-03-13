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

cbuffer SkinConstants : register(b4)
{
    row_major float4x4 gBones[64];
};

struct VS_INPUT
{
    float3 position    : POSITION;
    float2 texCoord    : TEXCOORD0;
    uint4  boneIndices : BLENDINDICES0;
    float4 boneWeights : BLENDWEIGHT0;
};
struct VS_OUTPUT { float4 position : SV_POSITION; float2 texCoord : TEXCOORD0; };

row_major float4x4 BuildSkinMatrix(uint4 idx, float4 w)
{
    row_major float4x4 skin = gBones[idx.x] * w.x;
    skin += gBones[idx.y] * w.y;
    skin += gBones[idx.z] * w.z;
    skin += gBones[idx.w] * w.w;
    return skin;
}

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT o;
    float4 skinnedPos = mul(float4(input.position, 1.0f), BuildSkinMatrix(input.boneIndices, input.boneWeights));
    float4 worldPos = mul(skinnedPos, gWorld);
    o.position = mul(worldPos, gShadowViewProj);
    o.texCoord = input.texCoord;
    return o;
}
