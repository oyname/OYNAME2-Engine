// CBuffer_Frame.hlsli — b1: Kamera + View/Proj.
#ifndef CBUFFER_FRAME_HLSLI
#define CBUFFER_FRAME_HLSLI

cbuffer FrameConstants : register(b1)
{
    row_major float4x4 gView;
    row_major float4x4 gProj;
    row_major float4x4 gViewProj;
    float4             gCameraPos;
    row_major float4x4 gShadowViewProj;  // Legacy — Padding
};

#endif // CBUFFER_FRAME_HLSLI
