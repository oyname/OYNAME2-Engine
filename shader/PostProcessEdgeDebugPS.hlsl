Texture2D gSceneDepth   : register(t0);
Texture2D gSceneNormals : register(t1);
SamplerState gSampler   : register(s1);

cbuffer EdgeDebugParams : register(b0)
{
    float gTexelW;
    float gTexelH;
    float gDepthScale;
    float gNormalScale;
    float gDepthOnly;
    float gNormalOnly;
    float gPad0;
    float gPad1;
};

struct VSOut
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
};

float3 DecodeNormal(float3 enc)
{
    return normalize(enc * 2.0f - 1.0f);
}

float SampleDepth(float2 uv)
{
    return gSceneDepth.SampleLevel(gSampler, uv, 0).r;
}

float3 SampleNormal(float2 uv)
{
    return DecodeNormal(gSceneNormals.SampleLevel(gSampler, uv, 0).rgb);
}

float4 main(VSOut input) : SV_Target
{    
    const float2 uv = input.uv;
    const float2 dx = float2(gTexelW, 0.0f);
    const float2 dy = float2(0.0f, gTexelH);

    const float dC = SampleDepth(uv);
    const float dR = SampleDepth(uv + dx);
    const float dL = SampleDepth(uv - dx);
    const float dU = SampleDepth(uv - dy);
    const float dD = SampleDepth(uv + dy);

    const float depthEdge =
        abs(dC - dR) +
        abs(dC - dL) +
        abs(dC - dU) +
        abs(dC - dD);

    const float3 nC = SampleNormal(uv);
    const float3 nR = SampleNormal(uv + dx);
    const float3 nL = SampleNormal(uv - dx);
    const float3 nU = SampleNormal(uv - dy);
    const float3 nD = SampleNormal(uv + dy);

    const float normalEdge =
        (1.0f - saturate(dot(nC, nR))) +
        (1.0f - saturate(dot(nC, nL))) +
        (1.0f - saturate(dot(nC, nU))) +
        (1.0f - saturate(dot(nC, nD)));

    float edge = 0.0f;
    if (gDepthOnly > 0.5f)
    {
        edge = saturate(depthEdge * gDepthScale);
    }
    else if (gNormalOnly > 0.5f)
    {
        edge = saturate(normalEdge * gNormalScale);
    }
    else
    {
        edge = saturate(depthEdge * gDepthScale + normalEdge * gNormalScale);
    }

    return float4(edge, edge, edge, 1.0f);
}
