// ECSPixelShader.hlsl — KROM Engine
// Lighting: Directional, Point, Spot.
// Shading:  Phong (Standard) oder Cook-Torrance PBR (MF_SHADING_PBR).
// Shadow:   CSM PCF 3x3 (Texture2DArray t16, Kaskaden-Selektion via b5).
// Texturen: t0=Albedo, t1=Normal, t2=ORM, t3=Emissive, t4=DetailMap.

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
SamplerState           gSamplerAniso : register(s2);  // Anisotropic Wrap — Materialien
SamplerComparisonState gShadowSamp   : register(s7);

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

#define MF_ALPHA_TEST      (1u << 0)
#define MF_DOUBLE_SIDED    (1u << 1)
#define MF_UNLIT           (1u << 2)
#define MF_USE_NORMAL_MAP  (1u << 3)
#define MF_USE_ORM_MAP     (1u << 4)
#define MF_USE_EMISSIVE    (1u << 5)
#define MF_SHADING_PBR     (1u << 10)
#define MF_USE_DETAIL_MAP  (1u << 11)
#define ROUGHNESS_MIN      0.04f

struct LightData
{
    float4 position;
    float4 direction;
    float4 diffuse;
    float  innerCosAngle;
    float  outerCosAngle;
    float  _pad0;
    float  _pad1;
};

StructuredBuffer<LightData> gTileLights     : register(t20);
StructuredBuffer<uint>      gLightIndexList : register(t21);
StructuredBuffer<uint2>     gLightGrid      : register(t22);

cbuffer TileInfo : register(b3)
{
    float3 sceneAmbient;
    uint   lightCount;
    uint   tileCountX;
    uint   tileCountY;
    float  _tPad0;
    float  _tPad1;
};

cbuffer LegacyLightBuffer : register(b4)
{
    LightData gLegacyLights[32];
    float3    gLegacySceneAmbient;
    uint      gLegacyLightCount;
};

cbuffer CascadeConstants : register(b5)
{
    row_major float4x4 gCascadeViewProj[4];
    float4             gCascadeSplits;
    uint               gCascadeCount;
    float3             _cascadePad;
};

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

float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    float3 r = max(float3(1.0f - roughness, 1.0f - roughness, 1.0f - roughness), F0);
    return F0 + (r - F0) * pow(max(1.0f - cosTheta, 0.0f), 5.0f);
}

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

float CalculateShadowCSM(float3 worldPos, float3 NGeom, float3 lightDir, float viewDepth)
{
    uint cascade = SelectCascade(viewDepth);

    float3 Ng = normalize(NGeom);
    float3 L = normalize(-lightDir);
    float NdotL = saturate(dot(Ng, L));

    float normalOffset = 0.0025f + (1.0f - NdotL) * 0.0100f;
    float3 biasedPos = worldPos + Ng * normalOffset;

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

    float bias  = 0.0008f + (1.0f - NdotL) * 0.0017f;
    float depth = proj.z - bias;

    float shadow = 0.0f;
    [unroll] for (int dy = -1; dy <= 1; ++dy)
    [unroll] for (int dx = -1; dx <= 1; ++dx)
        shadow += gShadowMap.SampleCmpLevelZero(
            gShadowSamp, float3(proj.xy + float2(dx, dy) * texelSize, float(cascade)), depth);

    return shadow / 9.0f;
}

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

struct PS_OUTPUT
{
    float4 color   : SV_Target0;
    float4 normal  : SV_Target1;
};

float3 EncodeViewNormal(float3 worldNormal)
{
    float3x3 view3x3 = (float3x3)gView;
    float3 viewNormal = mul(worldNormal, view3x3);
    viewNormal = normalize(viewNormal);
    return viewNormal * 0.5f + 0.5f;
}

PS_OUTPUT main(PS_INPUT input)
{
    float2 uv    = input.texCoord  * gUVTilingOffset.xy       + gUVTilingOffset.zw;
    float2 uvN   = input.texCoord  * gUVNormalTilingOffset.xy + gUVNormalTilingOffset.zw;
    float2 uv1   = input.texCoord1 * gUVDetailTilingOffset.xy + gUVDetailTilingOffset.zw;

    float3 ddx_worldPos = ddx(input.worldPosition);
    float3 ddy_worldPos = ddy(input.worldPosition);
    float2 ddx_uvN      = ddx(uvN);
    float2 ddy_uvN      = ddy(uvN);
    float2 ddx_uv       = ddx(uv);
    float2 ddy_uv       = ddy(uv);

    float3 NGeom = normalize(input.normal);
    float3 NShade = NGeom;
    if ((gFlags & MF_USE_NORMAL_MAP) != 0u)
    {
        float3 dp1   = ddx_worldPos;
        float3 dp2   = ddy_worldPos;
        float2 duv1  = ddx_uvN;
        float2 duv2  = ddy_uvN;

        float3 dp2perp = cross(dp2, NGeom);
        float3 dp1perp = cross(NGeom, dp1);
        float3 T = dp2perp * duv1.x + dp1perp * duv2.x;
        float3 B = dp2perp * duv1.y + dp1perp * duv2.y;

        float maxLen2 = max(dot(T, T), dot(B, B));
        if (maxLen2 > 1e-10f)
        {
            float invmax = rsqrt(maxLen2);
            T *= invmax;
            B *= invmax;

            float3 nTS = gNormalMap.SampleGrad(gSamplerAniso, uvN, ddx_uv, ddy_uv).xyz * 2.0f - 1.0f;
            float  nLen = dot(nTS, nTS);
            nTS = (nLen > 1e-10f) ? nTS * rsqrt(nLen) : float3(0.0f, 0.0f, 1.0f);
            nTS.xy *= gNormalScale;

            NShade = normalize(nTS.x * T + nTS.y * B + nTS.z * NGeom);
        }
    }

    float3 N = NShade;

    {
        float NGeomDotN = dot(N, NGeom);
        if (NGeomDotN < 0.0f)
            N = normalize(N - (NGeomDotN - 1e-4f) * NGeom);
    }

    float4 baseColor = gAlbedo.Sample(gSamplerAniso, uv) * gBaseColor * input.vertexColor;
    if ((gFlags & MF_ALPHA_TEST) != 0u)
        if (baseColor.a < gAlphaCutoff) discard;

    if ((gFlags & MF_USE_DETAIL_MAP) != 0u)
    {
        float3 detail  = gDetailMap.Sample(gSamplerAniso, uv1).rgb;
        baseColor.rgb *= detail * 2.0f;
        baseColor.rgb  = saturate(baseColor.rgb);
    }

    float3 albedo = baseColor.rgb;

    float metallic  = gMetallic;
    float roughness = max(gRoughness, ROUGHNESS_MIN);
    float ao        = 1.0f;
    if ((gFlags & MF_USE_ORM_MAP) != 0u)
    {
        float3 orm = gORM.Sample(gSamplerAniso, uv).rgb;
        ao         = lerp(1.0f, orm.r, saturate(gOcclusionStrength));
        roughness  = max(orm.g, ROUGHNESS_MIN);
        metallic   = saturate(orm.b);
    }

    if ((gFlags & MF_UNLIT) != 0u)
    {
        float3 em = float3(0, 0, 0);
        if ((gFlags & MF_USE_EMISSIVE) != 0u)
            em = gEmissive.Sample(gSamplerAniso, uv).rgb * gEmissiveColor.rgb;

        PS_OUTPUT output;
        output.color = float4(albedo + em, baseColor.a * (1.0f - gTransparency));
        output.normal = float4(EncodeViewNormal(N), 1.0f);
        return output;
    }

    float4 viewPos = mul(float4(input.worldPosition, 1.0f), gView);
    float  viewDepth = viewPos.z;

    const bool useTiledLighting = (tileCountX > 0u) && (tileCountY > 0u);
    const uint fallbackLightCount = min(gLegacyLightCount, 32u);

    uint   tileOffset  = 0u;
    uint   tileLights  = fallbackLightCount;
    float3 activeSceneAmbient = gLegacySceneAmbient;

    if (useTiledLighting)
    {
        uint2 tileIdx = uint2(input.position.xy) / uint2(16u, 16u);
        tileIdx.x = min(tileIdx.x, tileCountX - 1u);
        tileIdx.y = min(tileIdx.y, tileCountY - 1u);
        uint  tileFlat = tileIdx.y * tileCountX + tileIdx.x;
        uint2 tileGrid = gLightGrid[tileFlat];
        tileOffset = tileGrid.x;
        tileLights = tileGrid.y;
        activeSceneAmbient = sceneAmbient;
    }

    float3 V = normalize(input.viewDirection);
    float  NdotV_shading = max(dot(N, V), 1e-4f);
    bool   isPBR = (gFlags & MF_SHADING_PBR) != 0u;

    float3 diffuseAcc  = float3(0, 0, 0);
    float3 specularAcc = float3(0, 0, 0);

    int    dirLightIdx = -1;
    float3 dirLightDir = float3(0, -1, 0);
    float3 dirLightColor = float3(0, 0, 0);

    const uint globalLightCount = useTiledLighting ? lightCount : fallbackLightCount;
    [loop]
    for (uint s = 0; s < globalLightCount; ++s)
    {
        LightData light;
        if (useTiledLighting)
            light = gTileLights[s];
        else
            light = gLegacyLights[s];

        const bool isDirectional = (light.position.w < 0.5f);
        const bool castsShadows  = (light.direction.w > 0.5f);
        if (isDirectional && castsShadows)
        {
            dirLightIdx = (int)s;
            dirLightDir = normalize(light.direction.xyz);
            dirLightColor = light.diffuse.rgb;
            break;
        }
    }

    if (dirLightIdx >= 0)
    {
        float shadow = (gReceiveShadows > 0.5f)
            ? CalculateShadowCSM(input.worldPosition, NGeom, dirLightDir, viewDepth)
            : 1.0f;

        float NdotL = max(dot(N, -dirLightDir), 0.0f);

        if (!isPBR)
        {
            diffuseAcc += dirLightColor * NdotL * shadow;
            float3 H    = normalize(-dirLightDir + V);
            float  spec = pow(max(dot(N, H), 0.0f), max(gShininess, 1.0f));
            specularAcc += gSpecularColor.rgb * spec * dirLightColor * shadow * NdotL;
        }
        else
        {
            float3 L    = -dirLightDir;
            float3 H    = normalize(V + L);
            float NdotL_pbr = max(dot(N, L), 0.0f);
            float NdotV = NdotV_shading;
            float NdotH = max(dot(N, H), 0.0f);
            float VdotH = max(dot(V, H), 0.0f);

            float3 F0   = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
            float  D    = DistributionGGX(NdotH, roughness);
            float  G    = GeometrySmith(NdotV, NdotL_pbr, roughness);
            float3 F    = FresnelSchlick(VdotH, F0);
            float3 kD   = (1.0f - F) * (1.0f - metallic);
            float3 spec = (D * G * F) / max(4.0f * NdotV * NdotL_pbr, 1e-6f);

            diffuseAcc += (kD * albedo / PI + spec) * dirLightColor * NdotL_pbr * shadow;
        }
    }

    [loop]
    for (uint i = 0; i < tileLights; ++i)
    {
        uint lightIdx = useTiledLighting ? gLightIndexList[tileOffset + i] : i;

        LightData light;
        if (useTiledLighting)
            light = gTileLights[lightIdx];
        else
            light = gLegacyLights[lightIdx];

        if (light.position.w < 0.5f)
            continue;

        float lightType = light.position.w;
        float3 lightColor = light.diffuse.rgb;
        float radius = light.diffuse.a;

        float3 lightDir;
        float  attenuation = 1.0f;

        if (lightType < 1.5f)
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

        float NdotL = max(dot(N, -lightDir), 0.0f);

        if (!isPBR)
        {
            diffuseAcc += lightColor * NdotL * attenuation;
            float3 H    = normalize(-lightDir + V);
            float  spec = pow(max(dot(N, H), 0.0f), max(gShininess, 1.0f));
            specularAcc += gSpecularColor.rgb * spec * lightColor * attenuation * NdotL;
        }
        else
        {
            float3 L    = -lightDir;
            float3 H    = normalize(V + L);
            float NdotL_pbr = max(dot(N, L), 0.0f);
            float NdotV = NdotV_shading;
            float NdotH = max(dot(N, H), 0.0f);
            float VdotH = max(dot(V, H), 0.0f);

            float3 F0   = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
            float  D    = DistributionGGX(NdotH, roughness);
            float  G    = GeometrySmith(NdotV, NdotL_pbr, roughness);
            float3 F    = FresnelSchlick(VdotH, F0);
            float3 kD   = (1.0f - F) * (1.0f - metallic);
            float3 spec = (D * G * F) / max(4.0f * NdotV * NdotL_pbr, 1e-6f);

            diffuseAcc += (kD * albedo / PI + spec) * lightColor * NdotL_pbr * attenuation;
        }
    }

    float3 emissive = float3(0, 0, 0);
    if ((gFlags & MF_USE_EMISSIVE) != 0u)
    {
        emissive = gEmissiveColor.rgb;
        float3 emissiveTex = gEmissive.Sample(gSampler, uv).rgb;
        if (dot(emissiveTex, emissiveTex) > 0.000001f)
            emissive *= emissiveTex;
    }

    float3 color;
    if (isPBR)
    {
        float3 F0     = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
        float3 kS_amb = FresnelSchlickRoughness(NdotV_shading, F0, roughness);
        float3 kD_amb = (1.0f - kS_amb) * (1.0f - metallic);

        float3 irradiance  = gIrradianceMap.Sample(gSamplerClamp, N).rgb;
        float3 diffuseIBL  = kD_amb * irradiance * albedo;

        float3 R = reflect(-V, N);
        const float MAX_REFLECTION_LOD = 4.0f;
        float3 prefilteredColor = gPrefilteredEnv.SampleLevel(gSamplerClamp, R, roughness * MAX_REFLECTION_LOD).rgb;
        float2 brdf = gBrdfLut.Sample(gSamplerClamp, float2(NdotV_shading, roughness)).rg;
        float3 specularIBL = prefilteredColor * (kS_amb * brdf.x + brdf.y);

        float3 ambient = ((diffuseIBL + specularIBL) + activeSceneAmbient * albedo) * ao;
        color = ambient + diffuseAcc + emissive;
    }
    else
    {
        color = (activeSceneAmbient + diffuseAcc) * albedo * ao + specularAcc + emissive;
    }

    PS_OUTPUT output;
    output.color = float4(color, baseColor.a * (1.0f - gTransparency));
    output.normal = float4(EncodeViewNormal(N), 1.0f);
    return output;
}
