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

struct VS_INPUT
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float4 color    : COLOR;
    float2 texCoord : TEXCOORD0;
    float4 instanceWorld0  : TEXCOORD8;
    float4 instanceWorld1  : TEXCOORD9;
    float4 instanceWorld2  : TEXCOORD10;
    float4 instanceWorld3  : TEXCOORD11;
    float4 instanceWorldIT0 : TEXCOORD12;
    float4 instanceWorldIT1 : TEXCOORD13;
    float4 instanceWorldIT2 : TEXCOORD14;
    float4 instanceWorldIT3 : TEXCOORD15;
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
    row_major float4x4 instanceWorld = float4x4(input.instanceWorld0, input.instanceWorld1, input.instanceWorld2, input.instanceWorld3);
    row_major float4x4 instanceWorldIT = float4x4(input.instanceWorldIT0, input.instanceWorldIT1, input.instanceWorldIT2, input.instanceWorldIT3);
    float4 worldPos  = mul(float4(input.position, 1.0f), instanceWorld);
    o.worldPosition  = worldPos.xyz;
    o.position       = mul(worldPos, gViewProj);
    o.normal         = normalize(mul(float4(input.normal, 0.0f), instanceWorldIT).xyz);
    o.texCoord       = input.texCoord;
    o.texCoord1      = input.texCoord;
    o.viewDirection  = normalize(gCameraPos.xyz - worldPos.xyz);
    o.vertexColor    = input.color;
    return o;
}
