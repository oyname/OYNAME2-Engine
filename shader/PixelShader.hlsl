// ECSPixelShader.hlsl — GIDX ECS Engine
// Lighting: Directional, Point, Spot.
// Shading:  Phong (Standard) oder Cook-Torrance PBR (MF_SHADING_PBR).
// Shadow:   CSM PCF 3x3 (Texture2DArray t16, Kaskaden-Selektion via b5).
// Texturen: t0=Albedo, t1=Normal, t2=ORM, t3=Emissive, t4=DetailMap.

// ---------------------------------------------------------------------------
// Texturen + Sampler
// ---------------------------------------------------------------------------
Texture2D              gAlbedo    : register(t0);
Texture2D              gNormalMap : register(t1);
Texture2D              gORM       : register(t2);
Texture2D              gEmissive  : register(t3);
Texture2D              gDetailMap : register(t4);
Texture2DArray         gShadowMap : register(t16);  // CSM: Array[cascadeCount]
TextureCube            gIrradianceMap   : register(t17);  // IBL diffuse
TextureCube            gPrefilteredEnv  : register(t18);  // IBL specular
Texture2D              gBrdfLut         : register(t19);  // Split-sum LUT

SamplerState           gSampler      : register(s0);  // Linear Wrap — Materialien
SamplerState           gSamplerClamp : register(s1);  // Linear Clamp — IBL, RTT
SamplerComparisonState gShadowSamp   : register(s7);

// ---------------------------------------------------------------------------
// Constant Buffers
// ---------------------------------------------------------------------------
cbuffer FrameConstants : register(b1)
{
    row_major float4x4 gView;
    row_major float4x4 gProj;
    row_major float4x4 gViewProj;
    float4             gCameraPos;
    row_major float4x4 gShadowViewProj;  // Legacy — Padding, nicht mehr genutzt
};

cbuffer MaterialConstants : register(b2)
{
    float4   gBaseColor;
    float4   gSpecularColor;
    float4   gEmissiveColor;
    float4   gUVTilingOffset;
    float4   gUVDetailTilingOffset;
    float4   gUVNormalTilingOffset;
    float    gMetallic;
    float    gRoughness;
    float    gNormalScale;
    float    gOcclusionStrength;
    float    gShininess;
    float    gTransparency;
    float    gAlphaCutoff;
    float    gReceiveShadows;
    float    gBlendMode;
    float    gBlendFactor;
    uint     gFlags;
    float    _pad0;
};

// MaterialFlags
#define MF_ALPHA_TEST      (1u << 0)
#define MF_DOUBLE_SIDED    (1u << 1)
#define MF_UNLIT           (1u << 2)
#define MF_USE_NORMAL_MAP  (1u << 3)
#define MF_USE_ORM_MAP     (1u << 4)
#define MF_USE_EMISSIVE    (1u << 5)
#define MF_SHADING_PBR     (1u << 10)
#define MF_USE_DETAIL_MAP  (1u << 11)
#define ROUGHNESS_MIN      0.04f

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Forward+ Light Data (t20/t21/t22) + Tile Info (b3)
// ---------------------------------------------------------------------------
struct LightData
{
    float4 position;       // xyz=pos, w: 0=directional, 1=point, 2=spot
    float4 direction;      // xyz=dir (world space), w=castShadows
    float4 diffuse;        // rgb=color*intensity, a=radius
    float  innerCosAngle;
    float  outerCosAngle;
    float  _pad0;
    float  _pad1;
};

StructuredBuffer<LightData> gTileLights     : register(t20);  // all light data
StructuredBuffer<uint>      gLightIndexList : register(t21);  // flat index list
StructuredBuffer<uint2>     gLightGrid      : register(t22);  // (offset,count) per tile

cbuffer TileInfo : register(b3)
{
    float3 sceneAmbient;
    uint   lightCount;
    uint   tileCountX;
    uint   tileCountY;
    float  _tPad0;
    float  _tPad1;
};

// ---------------------------------------------------------------------------
// CascadeConstants (b5) — CSM
// ---------------------------------------------------------------------------
cbuffer CascadeConstants : register(b5)
{
    row_major float4x4 gCascadeViewProj[4];
    float4             gCascadeSplits;   // x=c0, y=c1, z=c2, w=c3 (View-Space far)
    uint               gCascadeCount;
    float3             _cascadePad;
};

// ---------------------------------------------------------------------------
// PS Input — kein positionLightSpace mehr (wird im PS pro Kaskade berechnet)
// ---------------------------------------------------------------------------
struct PS_INPUT
{
    float4 position      : SV_POSITION;
    float3 normal        : NORMAL;
    float3 worldPosition : TEXCOORD1;
    float2 texCoord      : TEXCOORD0;
    float3 viewDirection : TEXCOORD3;
    float2 texCoord1     : TEXCOORD4;
    float4 vertexColor   : COLOR0;
};

// ---------------------------------------------------------------------------
// PBR Hilfsfunktionen
// ---------------------------------------------------------------------------
static const float PI = 3.14159265359f;

float DistributionGGX(float NdotH, float roughness)
{
    float a  = roughness * roughness;
    float a2 = a * a;
    float d  = (NdotH * NdotH) * (a2 - 1.0f) + 1.0f;
    return a2 / max(PI * d * d, 1e-7f);
}

float GeometrySchlick(float NdotV, float roughness)
{
    float r = roughness + 1.0f;
    float k = (r * r) / 8.0f;
    return NdotV / max(NdotV * (1.0f - k) + k, 1e-7f);
}

float GeometrySmith(float NdotV, float NdotL, float roughness)
{
    return GeometrySchlick(NdotV, roughness) * GeometrySchlick(NdotL, roughness);
}

float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0f - F0) * pow(max(1.0f - cosTheta, 0.0f), 5.0f);
}

// IBL: Fresnel mit Roughness-Dämpfung (Karis)
float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    float3 r = max(float3(1.0f - roughness, 1.0f - roughness, 1.0f - roughness), F0);
    return F0 + (r - F0) * pow(max(1.0f - cosTheta, 0.0f), 5.0f);
}

// ---------------------------------------------------------------------------
// CSM: Kaskaden-Index aus View-Space-Tiefe wählen
// ---------------------------------------------------------------------------
uint SelectCascade(float viewDepth)
{
    float splits[4] = {
        gCascadeSplits.x,
        gCascadeSplits.y,
        gCascadeSplits.z,
        gCascadeSplits.w
    };
    uint cascade = gCascadeCount - 1u;
    [unroll] for (uint c = 0; c < gCascadeCount; ++c)
    {
        if (viewDepth < splits[c])
        {
            cascade = c;
            break;
        }
    }
    return cascade;
}

// ---------------------------------------------------------------------------
// CSM PCF 3x3
// ---------------------------------------------------------------------------
float CalculateShadowCSM(float3 worldPos, float3 N, float3 lightDir, float viewDepth)
{
    uint cascade = SelectCascade(viewDepth);

    // NdotL muss VOR der Projektion stehen, damit der Normal-Offset-Bias greifen kann.
    float NdotL = saturate(dot(normalize(N), normalize(-lightDir)));

    // Normal Offset Bias: worldPos entlang der Oberflächennormale verschieben, bevor
    // in den Light-Space projiziert wird.  Robuster als reiner Depth-Bias für flache
    // Flächen (NdotL ≈ 1) und streifende Lichtwinkel (NdotL ≈ 0).
    // Bei NdotL≈1 (Licht senkrecht) ist Offset klein; bei NdotL≈0 (streifend) groß.
    // Hardware-Bias in GDXShadowMap.cpp ist minimal — kein doppelter Bias.
    float normalOffset = 0.01f * (1.0f - NdotL);
    float3 biasedPos = worldPos + normalize(N) * normalOffset;

    float4 lightSpacePos = mul(float4(biasedPos, 1.0f), gCascadeViewProj[cascade]);

    if (lightSpacePos.w <= 0.00001f) return 1.0f;

    float3 proj = lightSpacePos.xyz / lightSpacePos.w;
    proj.x =  proj.x * 0.5f + 0.5f;
    proj.y = -proj.y * 0.5f + 0.5f;

    if (proj.x < 0.0f || proj.x > 1.0f ||
        proj.y < 0.0f || proj.y > 1.0f ||
        proj.z < 0.0f || proj.z > 1.0f) return 1.0f;

    uint tw2, th2, elems2;
    gShadowMap.GetDimensions(tw2, th2, elems2);
    float texelSize = 1.0f / float(tw2);

    // Minimaler Depth-Bias — normalOffset übernimmt die Hauptarbeit.
    // Kein cascadeScale-Multiplikator mehr — verhindert Peter Panning in weiten Kaskaden.
    float bias  = 0.0005f;
    float depth = proj.z - bias;

    float shadow = 0.0f;
    [unroll] for (int dy = -1; dy <= 1; ++dy)
    [unroll] for (int dx = -1; dx <= 1; ++dx)
        shadow += gShadowMap.SampleCmpLevelZero(
            gShadowSamp, float3(proj.xy + float2(dx, dy) * texelSize, float(cascade)), depth);

    return shadow / 9.0f;
}

// ---------------------------------------------------------------------------
// Attenuation
// ---------------------------------------------------------------------------
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
    float cosAngle = dot(normalize(lightDir), normalize(spotDir));
    return smoothstep(outerCos, innerCos, cosAngle);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
float4 main(PS_INPUT input) : SV_TARGET
{
    float2 uv    = input.texCoord  * gUVTilingOffset.xy       + gUVTilingOffset.zw;
    float2 uvN   = input.texCoord  * gUVNormalTilingOffset.xy + gUVNormalTilingOffset.zw;
    float2 uv1   = input.texCoord1 * gUVDetailTilingOffset.xy + gUVDetailTilingOffset.zw;

    // --- Normal ---
    float3 N = normalize(input.normal);
    if ((gFlags & MF_USE_NORMAL_MAP) != 0u)
    {
        // Cotangent frame (Mikkelsen / ShaderX5 / thetenthplanet.de/archives/1180)
        // This is the standard used by Unreal Engine and Unity for ddx/ddy-based
        // TBN reconstruction without precomputed tangents.
        //
        // Key properties vs naive T=normalize(Q1*st2.y - Q2*st1.y):
        //   - Solves the full UV->position linear system (not just one component).
        //   - UV handedness (mirrored faces, det<0) is handled automatically.
        //   - Scale-invariant: invmax uses max(|T|,|B|) instead of normalizing
        //     both independently, so degenerate near-zero cases are safe.
        //   - No branch, no quad-divergence seams.

        float3 dp1   = ddx(input.worldPosition);
        float3 dp2   = ddy(input.worldPosition);
        float2 duv1  = ddx(uvN);
        float2 duv2  = ddy(uvN);

        // Solve: [dp1, dp2]^T = [duv1, duv2]^T * [T, B]
        float3 dp2perp = cross(dp2, N);
        float3 dp1perp = cross(N, dp1);
        float3 T = dp2perp * duv1.x + dp1perp * duv2.x;
        float3 B = dp2perp * duv1.y + dp1perp * duv2.y;

        // Scale-invariant normalization: safe against degenerate UV layouts.
        float invmax = rsqrt(max(dot(T, T), dot(B, B)));
        T *= invmax;
        B *= invmax;

        // Sample and decode normal map.
        // Re-normalize after decode: GenerateMips() box-filters XYZ bytes
        // which shrinks vector length at mip boundaries.
        float3 nTS = gNormalMap.Sample(gSampler, uvN).xyz * 2.0f - 1.0f;
        float  nLen = dot(nTS, nTS);
        nTS = (nLen > 1e-10f) ? nTS * rsqrt(nLen) : float3(0.0f, 0.0f, 1.0f);
        nTS.xy *= gNormalScale;

        N = normalize(nTS.x * T + nTS.y * B + nTS.z * N);
    }

    // --- Albedo ---
    float4 baseColor = gAlbedo.Sample(gSampler, uv) * gBaseColor * input.vertexColor;
    if ((gFlags & MF_ALPHA_TEST) != 0u)
        if (baseColor.a < gAlphaCutoff) discard;

    // --- Detail-Map (UV1) ---
    if ((gFlags & MF_USE_DETAIL_MAP) != 0u)
    {
        float3 detail  = gDetailMap.Sample(gSampler, uv1).rgb;
        baseColor.rgb *= detail * 2.0f;
        baseColor.rgb  = saturate(baseColor.rgb);
    }

    float3 albedo = baseColor.rgb;

    // --- PBR Parameter ---
    float metallic  = gMetallic;
    float roughness = max(gRoughness, ROUGHNESS_MIN);
    float ao        = 1.0f;
    if ((gFlags & MF_USE_ORM_MAP) != 0u)
    {
        float3 orm = gORM.Sample(gSampler, uv).rgb;
        ao         = lerp(1.0f, orm.r, saturate(gOcclusionStrength));
        roughness  = max(orm.g, ROUGHNESS_MIN);
        metallic   = saturate(orm.b);
    }

    // --- Unlit ---
    if ((gFlags & MF_UNLIT) != 0u)
    {
        float3 em = float3(0, 0, 0);
        if ((gFlags & MF_USE_EMISSIVE) != 0u)
            em = gEmissive.Sample(gSampler, uv).rgb * gEmissiveColor.rgb;
        return float4(albedo + em, baseColor.a * (1.0f - gTransparency));
    }

    // --- View-Space-Tiefe für Kaskaden-Selektion ---
    float4 viewPos = mul(float4(input.worldPosition, 1.0f), gView);
    float  viewDepth = viewPos.z;

    // --- Forward+: Tile-Index bestimmen ---
    uint2  tileIdx     = uint2(input.position.xy) / uint2(16u, 16u);
    uint   tileFlat    = tileIdx.y * tileCountX + tileIdx.x;
    uint2  tileGrid    = (tileCountX > 0u) ? gLightGrid[tileFlat] : uint2(0u, lightCount);
    uint   tileOffset  = tileGrid.x;
    uint   tileLights  = tileGrid.y;


    // --- Lighting ---
    float3 V     = normalize(input.viewDirection);
    bool   isPBR = (gFlags & MF_SHADING_PBR) != 0u;

    float3 ambient     = sceneAmbient;
    float3 diffuseAcc  = float3(0, 0, 0);
    float3 specularAcc = float3(0, 0, 0);

    // Find shadow-casting directional light (first in tile list)
    int    shadowIdx = -1;
    float3 shadowDir = float3(0, -1, 0);
    [loop] for (uint s = 0; s < tileLights; ++s)
    {
        uint lightIdx = (tileCountX > 0u) ? gLightIndexList[tileOffset + s] : s;
        const bool isDirectional = (gTileLights[lightIdx].position.w < 0.5f);
        const bool castsShadows  = (gTileLights[lightIdx].direction.w > 0.5f);
        if (isDirectional && castsShadows)
        {
            shadowIdx = (int)lightIdx;
            shadowDir = normalize(gTileLights[lightIdx].direction.xyz);
            break;
        }
    }

    // Main lighting loop — only tile-visible lights
    [loop]
    for (uint i = 0; i < tileLights; ++i)
    {
        uint      lightIdx  = (tileCountX > 0u) ? gLightIndexList[tileOffset + i] : i;
        LightData light     = gTileLights[lightIdx];

        float  lightType  = light.position.w;
        float3 lightColor = light.diffuse.rgb;
        float  radius     = light.diffuse.a;

        float3 lightDir;
        float  attenuation = 1.0f;

        if (lightType < 0.5f)
        {
            lightDir    = normalize(light.direction.xyz);
            attenuation = 1.0f;
        }
        else if (lightType < 1.5f)
        {
            float3 toLight = light.position.xyz - input.worldPosition;
            float  dist    = length(toLight);
            lightDir       = -normalize(toLight);
            attenuation    = CalcAttenuation(dist, radius);
        }
        else
        {
            float3 toLight  = light.position.xyz - input.worldPosition;
            float  dist     = length(toLight);
            float3 toLightN = normalize(toLight);
            lightDir        = -toLightN;
            float distAtt   = CalcAttenuation(dist, radius);
            float coneAtt   = CalcSpotCone(
                light.direction.xyz, -toLightN,
                light.innerCosAngle, light.outerCosAngle);
            attenuation = distAtt * coneAtt;
        }

        if (attenuation <= 0.0f) continue;

        float shadow = 1.0f;
        if (shadowIdx >= 0 && gReceiveShadows > 0.5f && (int)lightIdx == shadowIdx)
            shadow = CalculateShadowCSM(input.worldPosition, N, shadowDir, viewDepth);

        float NdotL = max(dot(N, -lightDir), 0.0f);

        if (!isPBR)
        {
            diffuseAcc += lightColor * NdotL * attenuation * shadow;
            float3 H    = normalize(-lightDir + V);
            float  spec = pow(max(dot(N, H), 0.0f), max(gShininess, 1.0f));
            specularAcc += gSpecularColor.rgb * spec * lightColor * attenuation * shadow * NdotL;
        }
        else
        {
            float3 L    = -lightDir;
            float3 H    = normalize(V + L);
            float NdotV = max(dot(N, V), 0.0f);
            float NdotH = max(dot(N, H), 0.0f);
            float VdotH = max(dot(V, H), 0.0f);

            float3 F0   = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
            float  D    = DistributionGGX(NdotH, roughness);
            float  G    = GeometrySmith(NdotV, NdotL, roughness);
            float3 F    = FresnelSchlick(VdotH, F0);
            float3 kD   = (1.0f - F) * (1.0f - metallic);
            float3 spec = (D * G * F) / max(4.0f * NdotV * NdotL, 1e-6f);

            diffuseAcc += (kD * albedo / PI + spec) * lightColor * NdotL * attenuation * shadow;
        }
    }

    // --- Emissive ---
    float3 emissive = float3(0, 0, 0);
    if ((gFlags & MF_USE_EMISSIVE) != 0u)
    {
        emissive = gEmissiveColor.rgb;
        float3 emissiveTex = gEmissive.Sample(gSampler, uv).rgb;
        if (dot(emissiveTex, emissiveTex) > 0.000001f)
            emissive *= emissiveTex;
    }

    // --- Zusammensetzen ---
    float3 color;
    if (isPBR)
    {
        // IBL ambient: diffuse Irradiance + specular Prefiltered-Env + BRDF-LUT
        float3 F0     = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
        float3 kS_amb = FresnelSchlickRoughness(max(dot(N, V), 0.0f), F0, roughness);
        float3 kD_amb = (1.0f - kS_amb) * (1.0f - metallic);

        // Diffuse IBL: Irradiance-Cubemap samplen
        float3 irradiance  = gIrradianceMap.Sample(gSamplerClamp, N).rgb;
        float3 diffuseIBL  = kD_amb * irradiance * albedo;

        // Specular IBL: Prefiltered Env + BRDF-LUT (Karis Split-Sum)
        float3 R = reflect(-V, N);
        const float MAX_REFLECTION_LOD = 4.0f;
        float3 prefilteredColor = gPrefilteredEnv.SampleLevel(gSamplerClamp, R, roughness * MAX_REFLECTION_LOD).rgb;
        float2 brdf = gBrdfLut.Sample(gSamplerClamp, float2(max(dot(N, V), 0.0f), roughness)).rg;
        float3 specularIBL = prefilteredColor * (kS_amb * brdf.x + brdf.y);

        float3 ambient = (diffuseIBL + specularIBL) * ao;
        color = ambient + diffuseAcc + emissive;
    }
    else
    {
        color = (sceneAmbient + diffuseAcc) * albedo * ao + specularAcc + emissive;
    }

    return float4(color, baseColor.a * (1.0f - gTransparency));
}
