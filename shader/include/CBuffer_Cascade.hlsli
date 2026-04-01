// CBuffer_Cascade.hlsli — b5: CSM Kaskaden-Matrizen und Splits.
#ifndef CBUFFER_CASCADE_HLSLI
#define CBUFFER_CASCADE_HLSLI

cbuffer CascadeConstants : register(b5)
{
    row_major float4x4 gCascadeViewProj[4];
    float4             gCascadeSplits;
    uint               gCascadeCount;
    float3             _cascadePad;
};

#endif // CBUFFER_CASCADE_HLSLI
