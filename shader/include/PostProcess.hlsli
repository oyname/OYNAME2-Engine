// PostProcess.hlsli — geteilter VS-Output für Fullscreen-Passes.
#ifndef POSTPROCESS_HLSLI
#define POSTPROCESS_HLSLI

struct PSIn
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
};

#endif // POSTPROCESS_HLSLI
