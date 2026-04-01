// Lighting.hlsli — LightData-Struct, Forward+-Buffer, TileInfo cbuffer.
// Eingebunden von PixelShader.hlsl und TileLightCullCS.hlsl.
#ifndef LIGHTING_HLSLI
#define LIGHTING_HLSLI

struct LightData
{
    float4 position;       // xyz=pos, w: 0=directional 1=point 2=spot
    float4 direction;      // xyz=dir, w=castShadows
    float4 diffuse;        // rgb=color*intensity, a=radius
    float  innerCosAngle;
    float  outerCosAngle;
    float  _pad0;
    float  _pad1;
};

// Forward+ tile outputs (CS writes, PS reads)
StructuredBuffer<LightData> gTileLights     : register(t20);
StructuredBuffer<uint>      gLightIndexList : register(t21);
StructuredBuffer<uint2>     gLightGrid      : register(t22);

cbuffer TileInfo : register(b3)
{
    float3 gSceneAmbient;
    uint   gLightCount;
    uint   gTileCountX;
    uint   gTileCountY;
    float  _tPad0;
    float  _tPad1;
};

cbuffer LegacyLightBuffer : register(b4)
{
    LightData gLegacyLights[32];
    float3    gLegacySceneAmbient;
    uint      gLegacyLightCount;
};

// Attenuation
float CalcAttenuation(float dist, float radius)
{
    float r  = max(radius, 0.001f);
    float s  = dist / r;
    if (s >= 1.0f) return 0.0f;
    float s2 = s * s;
    return (1.0f - s2) * (1.0f - s2) / max(1.0f + s2, 1e-5f);
}

float CalcSpotCone(float3 lightDir, float3 spotDir, float innerCos, float outerCos)
{
    return smoothstep(outerCos, innerCos, dot(normalize(lightDir), normalize(spotDir)));
}

#endif // LIGHTING_HLSLI
