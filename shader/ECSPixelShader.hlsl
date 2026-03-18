// ECSPixelShader.hlsl — GIDX ECS Engine
// Lighting: Directional, Point, Spot.
// Shading:  Phong (Standard) oder Cook-Torrance PBR (MF_SHADING_PBR).
// Shadow:   PCF 3x3 (t16, s7).
// Texturen: t0=Albedo, t1=Normal, t2=ORM, t3=Emissive, t4=DetailMap.
//
// UV-Sets:
//   texCoord  (TEXCOORD0) — UV0: primäre Texturkoordinaten (Albedo, Normal, ORM, Emissive)
//   texCoord1 (TEXCOORD4) — UV1: Detail-Map / Lightmap
//   MF_USE_DETAIL_MAP steuert ob UV1 ausgewertet wird.
//   Kein echtes UV1 im Mesh: Executor aliasiert UV0 → TEXCOORD1-Slot.
//   Ohne MF_USE_DETAIL_MAP wird texCoord1 nie ausgewertet — kein Overhead.

// ---------------------------------------------------------------------------
// Texturen + Sampler
// ---------------------------------------------------------------------------
Texture2D              gAlbedo    : register(t0);
Texture2D              gNormalMap : register(t1);
Texture2D              gORM       : register(t2);
Texture2D              gEmissive  : register(t3);
Texture2D              gDetailMap : register(t4);   // UV1-Detail-Map (2x Multiply Blend)
Texture2D              gShadowMap : register(t16);

SamplerState           gSampler    : register(s0);
SamplerComparisonState gShadowSamp : register(s7);

// ---------------------------------------------------------------------------
// Constant Buffers
// ---------------------------------------------------------------------------
cbuffer FrameConstants : register(b1)
{
    row_major float4x4 gView;
    row_major float4x4 gProj;
    row_major float4x4 gViewProj;
    float4             gCameraPos;
    row_major float4x4 gShadowViewProj;
};

cbuffer MaterialConstants : register(b2)
{
    float4   gBaseColor;              // Zeile 1
    float4   gSpecularColor;          // Zeile 2
    float4   gEmissiveColor;          // Zeile 3
    float4   gUVTilingOffset;         // Zeile 4 — UV0: xy=tiling, zw=offset
    float4   gUVDetailTilingOffset;   // Zeile 5 — UV1: xy=tiling, zw=offset
    float    gMetallic;               // Zeile 6
    float    gRoughness;
    float    gNormalScale;
    float    gOcclusionStrength;
    float    gShininess;              // Zeile 7
    float    gTransparency;
    float    gAlphaCutoff;
    float    gReceiveShadows;
    float    gBlendMode;              // Zeile 8
    float    gBlendFactor;
    uint     gFlags;
    float    _pad0;
};

// MaterialFlags (muss mit MaterialResource.h übereinstimmen)
#define MF_ALPHA_TEST      (1u << 0)
#define MF_DOUBLE_SIDED    (1u << 1)
#define MF_UNLIT           (1u << 2)
#define MF_USE_NORMAL_MAP  (1u << 3)
#define MF_USE_ORM_MAP     (1u << 4)
#define MF_USE_EMISSIVE    (1u << 5)
#define MF_SHADING_PBR     (1u << 10)
#define MF_USE_DETAIL_MAP  (1u << 11)  // UV1-Detail-Map aktiv
#define ROUGHNESS_MIN      0.04f

// ---------------------------------------------------------------------------
// LightBuffer (b3)
// ---------------------------------------------------------------------------
struct LightData
{
    float4 position;       // xyz=pos, w=typ (0=dir, 1=point, 2=spot)
    float4 direction;      // xyz=dir (world, normalized), w=unused
    float4 diffuse;        // rgb=color*intensity, a=radius
    float  innerCosAngle;
    float  outerCosAngle;
    float  _pad0;
    float  _pad1;
};

cbuffer LightBuffer : register(b3)
{
    LightData lights[32];
    float3    sceneAmbient;
    uint      lightCount;
};

// ---------------------------------------------------------------------------
// PS Input
// ---------------------------------------------------------------------------
struct PS_INPUT
{
    float4 position           : SV_POSITION;
    float3 normal             : NORMAL;
    float3 worldPosition      : TEXCOORD1;
    float2 texCoord           : TEXCOORD0;    // UV0
    float4 positionLightSpace : TEXCOORD2;
    float3 viewDirection      : TEXCOORD3;
    float2 texCoord1          : TEXCOORD4;    // UV1 (oder Alias UV0 wenn kein 2. UV-Set)
    float4 vertexColor        : COLOR0;
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

// ---------------------------------------------------------------------------
// Shadow PCF 3x3
// ---------------------------------------------------------------------------
float CalculateShadow(float4 lightSpacePos, float3 N, float3 lightDir)
{
    if (lightSpacePos.w <= 0.00001f) return 1.0f;

    float3 proj = lightSpacePos.xyz / lightSpacePos.w;
    proj.x =  proj.x * 0.5f + 0.5f;
    proj.y = -proj.y * 0.5f + 0.5f;

    if (proj.x < 0.0f || proj.x > 1.0f ||
        proj.y < 0.0f || proj.y > 1.0f ||
        proj.z < 0.0f || proj.z > 1.0f) return 1.0f;

    float NdotL  = saturate(dot(normalize(N), normalize(-lightDir)));
    float bias   = max(0.0005f, 0.003f * (1.0f - NdotL));
    float depth  = proj.z - bias;

    uint tw, th;
    gShadowMap.GetDimensions(tw, th);
    float texel = 1.0f / float(tw);

    float shadow = 0.0f;
    [unroll] for (int dy = -1; dy <= 1; ++dy)
    [unroll] for (int dx = -1; dx <= 1; ++dx)
        shadow += gShadowMap.SampleCmpLevelZero(
            gShadowSamp, proj.xy + float2(dx, dy) * texel, depth);

    return shadow / 9.0f;
}

// ---------------------------------------------------------------------------
// Attenuation
// ---------------------------------------------------------------------------
float CalcAttenuation(float dist, float radius)
{
    float r = max(radius, 0.001f);
    float s = dist / r;
    if (s >= 1.0f) return 0.0f;
    float s2 = s * s;
    return (1.0f - s2) * (1.0f - s2) / max(1.0f + s2, 1e-5f);
}

float CalcSpotCone(float3 lightDir, float3 spotDir,
                   float innerCos, float outerCos)
{
    float cosAngle = dot(normalize(lightDir), normalize(spotDir));
    return smoothstep(outerCos, innerCos, cosAngle);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
float4 main(PS_INPUT input) : SV_TARGET
{
    // UV0 — primäre Koordinaten (Tiling + Offset aus Material)
    float2 uv  = input.texCoord  * gUVTilingOffset.xy       + gUVTilingOffset.zw;

    // UV1 — Detail-Map-Koordinaten (eigenes Tiling + Offset)
    // Ohne MF_USE_DETAIL_MAP wird diese Variable nie ausgewertet.
    float2 uv1 = input.texCoord1 * gUVDetailTilingOffset.xy + gUVDetailTilingOffset.zw;

    // --- Normal ---
    float3 N = normalize(input.normal);
    if ((gFlags & MF_USE_NORMAL_MAP) != 0u)
    {
        float3 Q1  = ddx(input.worldPosition);
        float3 Q2  = ddy(input.worldPosition);
        float2 st1 = ddx(uv);
        float2 st2 = ddy(uv);
        float3 T   = normalize(Q1 * st2.y - Q2 * st1.y);
        float3 B   = normalize(cross(N, T));
        float3 nTS = gNormalMap.Sample(gSampler, uv).xyz * 2.0f - 1.0f;
        nTS.xy    *= gNormalScale;
        N          = normalize(nTS.x * T + nTS.y * B + nTS.z * N);
    }

    // --- Albedo ---
    float4 baseColor = gAlbedo.Sample(gSampler, uv) * gBaseColor * input.vertexColor;
    if ((gFlags & MF_ALPHA_TEST) != 0u)
        if (baseColor.a < gAlphaCutoff) discard;

    // --- Detail-Map (UV1) ---
    // Nur wenn MF_USE_DETAIL_MAP gesetzt — kein Overhead ohne das Flag.
    // Blend: 2x Multiply (neutral bei 0.5 Grau, heller/dunkler bei Abweichung).
    if ((gFlags & MF_USE_DETAIL_MAP) != 0u)
    {
        float3 detail    = gDetailMap.Sample(gSampler, uv1).rgb;
        baseColor.rgb   *= detail * 2.0f;
        baseColor.rgb    = saturate(baseColor.rgb);
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

    // --- Lighting ---
    float3 V     = normalize(input.viewDirection);
    bool   isPBR = (gFlags & MF_SHADING_PBR) != 0u;

    float3 ambient     = sceneAmbient;
    float3 diffuseAcc  = float3(0, 0, 0);
    float3 specularAcc = float3(0, 0, 0);

    int    shadowIdx = -1;
    float3 shadowDir = float3(0, -1, 0);
    [loop] for (uint s = 0; s < lightCount; ++s)
    {
        const bool isDirectional = (lights[s].position.w < 0.5f);
        const bool castsShadows  = (lights[s].direction.w > 0.5f);
        if (isDirectional && castsShadows)
        {
            shadowIdx = (int)s;
            shadowDir = normalize(lights[s].direction.xyz);
            break;
        }
    }

    [loop]
    for (uint i = 0; i < lightCount; ++i)
    {
        float  lightType  = lights[i].position.w;
        float3 lightColor = lights[i].diffuse.rgb;
        float  radius     = lights[i].diffuse.a;

        float3 lightDir;
        float  attenuation = 1.0f;

        if (lightType < 0.5f)
        {
            lightDir    = normalize(lights[i].direction.xyz);
            attenuation = 1.0f;
        }
        else if (lightType < 1.5f)
        {
            float3 toLight = lights[i].position.xyz - input.worldPosition;
            float  dist    = length(toLight);
            lightDir       = -normalize(toLight);
            attenuation    = CalcAttenuation(dist, radius);
        }
        else
        {
            float3 toLight  = lights[i].position.xyz - input.worldPosition;
            float  dist     = length(toLight);
            float3 toLightN = normalize(toLight);
            lightDir        = -toLightN;
            float distAtt   = CalcAttenuation(dist, radius);
            float coneAtt   = CalcSpotCone(
                lights[i].direction.xyz, -toLightN,
                lights[i].innerCosAngle, lights[i].outerCosAngle);
            attenuation = distAtt * coneAtt;
        }

        if (attenuation <= 0.0f) continue;

        float shadow = 1.0f;
        if (shadowIdx >= 0 && gReceiveShadows > 0.5f && (int)i == shadowIdx)
            shadow = CalculateShadow(input.positionLightSpace, N, shadowDir);

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
        color = ambient * albedo * ao + diffuseAcc + emissive;
    else
        color = (ambient + diffuseAcc) * albedo * ao + specularAcc + emissive;

    return float4(color, baseColor.a * (1.0f - gTransparency));
}
