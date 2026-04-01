// CBuffer_Material.hlsli — b2: Material-Parameter.
// Muss byte-genau mit MaterialData in MaterialResource.h übereinstimmen.
#ifndef CBUFFER_MATERIAL_HLSLI
#define CBUFFER_MATERIAL_HLSLI

cbuffer MaterialConstants : register(b2)
{
    float4   gBaseColor;
    float4   gLegacySpecularColor;   // nur im Legacy-Phong-Kompatibilitaetspfad
    float4   gEmissiveColor;
    float4   gUVTilingOffset;
    float4   gUVDetailTilingOffset;
    float4   gUVNormalTilingOffset;
    float    gMetallic;
    float    gRoughness;
    float    gNormalScale;
    float    gOcclusionStrength;
    float    gLegacyShininess;       // nur im Legacy-Phong-Kompatibilitaetspfad
    float    gOpacity;               // 1=opak, 0=transparent
    float    gAlphaCutoff;
    float    _matPad1;               // war gReceiveShadows — jetzt MF_RECEIVE_SHADOWS in gFlags
    uint     gDetailBlendMode;       // MaterialDetailBlendMode
    float    gBlendFactor;
    uint     gFlags;
    float    _matPad0;
};

#endif // CBUFFER_MATERIAL_HLSLI
