Texture2D    gAO           : register(t0);
Texture2D    gSceneDepth   : register(t1);
Texture2D    gSceneNormals : register(t2);
SamplerState gSampler      : register(s1);

cbuffer GTAOBlurParams : register(b0)
{
    float gTexelW;
    float gTexelH;
    float gDepthSharpness;
    float gNormalSharpness;

    float gNearPlane;
    float gFarPlane;
    uint  gCameraIsOrtho;
    float _pad0;
};

struct VSOut
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
};

float LinearizeDepth(float rawDepth)
{
    if (gCameraIsOrtho != 0u)
        return lerp(gNearPlane, gFarPlane, rawDepth);
    return (gNearPlane * gFarPlane) / max(gFarPlane - rawDepth * (gFarPlane - gNearPlane), 1e-5f);
}

float3 DecodeNormal(float3 enc)
{
    return normalize(enc * 2.0f - 1.0f);
}

float WeightFor(float centerDepth, float sampleDepth, float3 centerNormal, float3 sampleNormal, float2 sampleOffset)
{
    float spatial = exp(-dot(sampleOffset, sampleOffset) * 0.35f);
    float depthW  = exp(-abs(sampleDepth - centerDepth) * gDepthSharpness);
    float normalW = pow(saturate(dot(centerNormal, sampleNormal)), gNormalSharpness);
    return spatial * depthW * normalW;
}

float4 main(VSOut input) : SV_Target
{
    float centerRawDepth = gSceneDepth.SampleLevel(gSampler, input.uv, 0).r;
    float centerDepth = LinearizeDepth(centerRawDepth);
    float centerAO    = gAO.SampleLevel(gSampler, input.uv, 0).r;
    float3 centerNormal = DecodeNormal(gSceneNormals.SampleLevel(gSampler, input.uv, 0).rgb);

    float sum  = centerAO * 2.0f;
    float wsum = 2.0f;

    const float2 offsets[12] =
    {
        float2( 1.0f,  0.0f), float2(-1.0f,  0.0f), float2( 0.0f,  1.0f), float2( 0.0f, -1.0f),
        float2( 1.0f,  1.0f), float2(-1.0f,  1.0f), float2( 1.0f, -1.0f), float2(-1.0f, -1.0f),
        float2( 2.0f,  0.0f), float2(-2.0f,  0.0f), float2( 0.0f,  2.0f), float2( 0.0f, -2.0f)
    };

    [unroll]
    for (int i = 0; i < 12; ++i)
    {
        float2 pixelOffset = offsets[i];
        float2 suv = saturate(input.uv + pixelOffset * float2(gTexelW, gTexelH));
        float ao = gAO.SampleLevel(gSampler, suv, 0).r;
        float d  = LinearizeDepth(gSceneDepth.SampleLevel(gSampler, suv, 0).r);
        float3 n = DecodeNormal(gSceneNormals.SampleLevel(gSampler, suv, 0).rgb);
        float w  = WeightFor(centerDepth, d, centerNormal, n, pixelOffset);
        sum += ao * w;
        wsum += w;
    }

    float ao = (wsum > 1e-5f) ? (sum / wsum) : centerAO;
    ao = min(ao, centerAO + 0.03f);
    return float4(ao, ao, ao, 1.0f);
}
