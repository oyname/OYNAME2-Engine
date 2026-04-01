// CBuffer_Entity.hlsli — b0: Objekt-Transformation.
#ifndef CBUFFER_ENTITY_HLSLI
#define CBUFFER_ENTITY_HLSLI

cbuffer EntityConstants : register(b0)
{
    row_major float4x4 gWorld;
    row_major float4x4 gWorldInverseTranspose;
};

#endif // CBUFFER_ENTITY_HLSLI
