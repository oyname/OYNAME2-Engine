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
    float4 gCameraPos;
    row_major float4x4 gShadowViewProj;
};

struct VertexIn
{
    float3 pos : POSITION0;
    float3 nrm : NORMAL0;
    float2 uv : TEXCOORD0;
};

struct VertexOut
{
    float4 posC : SV_POSITION;
    float3 posW : TEXCOORD0;
    float3 nrm : TEXCOORD1;
    float2 uv : TEXCOORD2;
};

VertexOut main(VertexIn v)
{
    VertexOut o;

    float4 posW = mul(float4(v.pos, 1.0f), gWorld);
    o.posW = posW.xyz;
    o.posC = mul(posW, gViewProj);
    o.nrm = normalize(mul(float4(v.nrm, 0.0f), gWorldInverseTranspose).xyz);
    o.uv = v.uv;

    return o;
}
