// CBuffer_Skin.hlsli — b4: Bone-Matrizen für Skinning.
#ifndef CBUFFER_SKIN_HLSLI
#define CBUFFER_SKIN_HLSLI

cbuffer SkinConstants : register(b4)
{
    row_major float4x4 gBones[64];
};

row_major float4x4 BuildSkinMatrix(uint4 idx, float4 w)
{
    row_major float4x4 skin  = gBones[idx.x] * w.x;
                       skin += gBones[idx.y] * w.y;
                       skin += gBones[idx.z] * w.z;
                       skin += gBones[idx.w] * w.w;
    return skin;
}

#endif // CBUFFER_SKIN_HLSLI
