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
    row_major float4x4 gShadowViewProj;  // Legacy — Padding
};

cbuffer SkinConstants : register(b4)
{
    row_major float4x4 gBones[64];
};

struct VS_INPUT
{
    float3 position    : POSITION;
    float3 normal      : NORMAL;
    float4 color       : COLOR;
    float2 texCoord    : TEXCOORD0;
    uint4  boneIndices : BLENDINDICES0;
    float4 boneWeights : BLENDWEIGHT0;
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
    row_major float4x4 skin = BuildSkinMatrix(input.boneIndices, input.boneWeights);
    float4 skinnedPos = mul(float4(input.position, 1.0f), skin);
    float3 skinnedN   = normalize(mul(input.normal, (float3x3)skin));
    float4 worldPos   = mul(skinnedPos, gWorld);
    o.worldPosition   = worldPos.xyz;
    o.position        = mul(worldPos, gViewProj);
    o.normal          = normalize(mul(skinnedN, (float3x3)gWorldInverseTranspose));
    o.texCoord        = input.texCoord;
    o.texCoord1       = input.texCoord;
    o.viewDirection   = normalize(gCameraPos.xyz - worldPos.xyz);
    o.vertexColor     = input.color;
    return o;
}
