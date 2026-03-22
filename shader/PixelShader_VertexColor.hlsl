// ECSPixelShader_VertexColor.hlsl — GIDX ECS Engine
// Identisch mit PixelShader.hlsl, aber ohne UV-Detail-Map.
// Shadow: CSM via Texture2DArray (t16) + CascadeConstants (b5).

Texture2D              gAlbedo    : register(t0);
Texture2D              gNormalMap : register(t1);
Texture2D              gORM       : register(t2);
Texture2D              gEmissive  : register(t3);
Texture2DArray         gShadowMap : register(t16);

SamplerState           gSampler    : register(s0);
SamplerComparisonState gShadowSamp : register(s7);

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

#define MF_ALPHA_TEST     (1u << 0)
#define MF_DOUBLE_SIDED   (1u << 1)
#define MF_UNLIT          (1u << 2)
#define MF_USE_NORMAL_MAP (1u << 3)
#define MF_USE_ORM_MAP    (1u << 4)
#define MF_USE_EMISSIVE   (1u << 5)
#define MF_SHADING_PBR    (1u << 10)
#define ROUGHNESS_MIN     0.04f

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

cbuffer LightBuffer : register(b3)
{
    LightData lights[32];
    float3    sceneAmbient;
    uint      lightCount;
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
    float4 vertexColor   : COLOR0;
};

static const float PI = 3.14159265359f;

float DistributionGGX(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float d = (NdotH * NdotH) * (a2 - 1.0f) + 1.0f;
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

uint SelectCascade(float viewDepth)
{
    float splits[4] = { gCascadeSplits.x, gCascadeSplits.y, gCascadeSplits.z, gCascadeSplits.w };
    uint cascade = gCascadeCount - 1u;
    [unroll] for (uint c = 0; c < gCascadeCount; ++c)
    {
        if (viewDepth < splits[c]) { cascade = c; break; }
    }
    return cascade;
}

float CalculateShadowCSM(float3 worldPos, float3 N, float3 lightDir, float viewDepth)
{
    uint cascade = SelectCascade(viewDepth);
    float4 lsp = mul(float4(worldPos, 1.0f), gCascadeViewProj[cascade]);
    if (lsp.w <= 0.00001f) return 1.0f;
    float3 proj = lsp.xyz / lsp.w;
    proj.x =  proj.x * 0.5f + 0.5f;
    proj.y = -proj.y * 0.5f + 0.5f;
    if (proj.x < 0.0f || proj.x > 1.0f ||
        proj.y < 0.0f || proj.y > 1.0f ||
        proj.z < 0.0f || proj.z > 1.0f) return 1.0f;
    float NdotL = saturate(dot(normalize(N), normalize(-lightDir)));
    float bias  = 0.00005f;
    float depth = proj.z - bias;
    uint tw, th, elems;
    gShadowMap.GetDimensions(tw, th, elems);
    float texel = 1.0f / float(tw);
    float shadow = 0.0f;
    [unroll] for (int dy = -1; dy <= 1; ++dy)
    [unroll] for (int dx = -1; dx <= 1; ++dx)
        shadow += gShadowMap.SampleCmpLevelZero(
            gShadowSamp, float3(proj.xy + float2(dx, dy) * texel, float(cascade)), depth);
    return shadow / 9.0f;
}

float CalcAttenuation(float dist, float radius)
{
    float r = max(radius, 0.001f);
    float s = dist / r;
    if (s >= 1.0f) return 0.0f;
    float s2 = s * s;
    return (1.0f - s2) * (1.0f - s2) / max(1.0f + s2, 1e-5f);
}

float CalcSpotCone(float3 lightDir, float3 spotDir, float innerCos, float outerCos)
{
    return smoothstep(outerCos, innerCos, dot(normalize(lightDir), normalize(spotDir)));
}

float4 main(PS_INPUT input) : SV_TARGET
{
    float2 uv = input.texCoord * gUVTilingOffset.xy + gUVTilingOffset.zw;

    float3 N = normalize(input.normal);
    if ((gFlags & MF_USE_NORMAL_MAP) != 0u)
    {
        float3 Q1 = ddx(input.worldPosition); float3 Q2 = ddy(input.worldPosition);
        float2 s1 = ddx(uv);                  float2 s2 = ddy(uv);
        float3 T  = normalize(Q1 * s2.y - Q2 * s1.y);
        float3 B  = normalize(cross(N, T));
        float3 nTS = gNormalMap.Sample(gSampler, uv).xyz * 2.0f - 1.0f;
        nTS.xy    *= gNormalScale;
        N          = normalize(nTS.x * T + nTS.y * B + nTS.z * N);
    }

    float4 baseColor = gAlbedo.Sample(gSampler, uv) * gBaseColor * input.vertexColor;
    if ((gFlags & MF_ALPHA_TEST) != 0u)
        if (baseColor.a < gAlphaCutoff) discard;
    float3 albedo = baseColor.rgb;

    float metallic  = gMetallic;
    float roughness = max(gRoughness, ROUGHNESS_MIN);
    float ao        = 1.0f;
    if ((gFlags & MF_USE_ORM_MAP) != 0u)
    {
        float3 orm = gORM.Sample(gSampler, uv).rgb;
        ao        = lerp(1.0f, orm.r, saturate(gOcclusionStrength));
        roughness = max(orm.g, ROUGHNESS_MIN);
        metallic  = saturate(orm.b);
    }

    if ((gFlags & MF_UNLIT) != 0u)
    {
        float3 em = float3(0,0,0);
        if ((gFlags & MF_USE_EMISSIVE) != 0u)
            em = gEmissive.Sample(gSampler, uv).rgb * gEmissiveColor.rgb;
        return float4(albedo + em, baseColor.a * (1.0f - gTransparency));
    }

    float4 viewPos = mul(float4(input.worldPosition, 1.0f), gView);
    float  viewDepth = viewPos.z;

    float3 V    = normalize(input.viewDirection);
    bool   isPBR = (gFlags & MF_SHADING_PBR) != 0u;
    float3 ambient = sceneAmbient;
    float3 diffuseAcc = float3(0,0,0);
    float3 specularAcc = float3(0,0,0);

    int    shadowIdx = -1;
    float3 shadowDir = float3(0,-1,0);
    [loop] for (uint s = 0; s < lightCount; ++s)
    {
        if (lights[s].position.w < 0.5f && lights[s].direction.w > 0.5f)
        { shadowIdx = (int)s; shadowDir = normalize(lights[s].direction.xyz); break; }
    }

    [loop] for (uint i = 0; i < lightCount; ++i)
    {
        float  lightType  = lights[i].position.w;
        float3 lightColor = lights[i].diffuse.rgb;
        float  radius     = lights[i].diffuse.a;
        float3 lightDir;
        float  attenuation = 1.0f;

        if (lightType < 0.5f)
        { lightDir = normalize(lights[i].direction.xyz); attenuation = 1.0f; }
        else if (lightType < 1.5f)
        {
            float3 toLight = lights[i].position.xyz - input.worldPosition;
            lightDir = -normalize(toLight);
            attenuation = CalcAttenuation(length(toLight), radius);
        }
        else
        {
            float3 toLight = lights[i].position.xyz - input.worldPosition;
            float3 toLN    = normalize(toLight);
            lightDir       = -toLN;
            attenuation    = CalcAttenuation(length(toLight), radius)
                           * CalcSpotCone(lights[i].direction.xyz, -toLN,
                                          lights[i].innerCosAngle, lights[i].outerCosAngle);
        }

        if (attenuation <= 0.0f) continue;

        float shadow = 1.0f;
        if (shadowIdx >= 0 && gReceiveShadows > 0.5f && (int)i == shadowIdx)
            shadow = CalculateShadowCSM(input.worldPosition, N, shadowDir, viewDepth);

        float NdotL = max(dot(N, -lightDir), 0.0f);

        if (!isPBR)
        {
            diffuseAcc += lightColor * NdotL * attenuation * shadow;
            float3 H = normalize(-lightDir + V);
            float  spec = pow(max(dot(N, H), 0.0f), max(gShininess, 1.0f));
            specularAcc += gSpecularColor.rgb * spec * lightColor * attenuation * shadow * NdotL;
        }
        else
        {
            float3 L = -lightDir; float3 H = normalize(V + L);
            float NdotV = max(dot(N, V), 0.0f);
            float NdotH = max(dot(N, H), 0.0f);
            float VdotH = max(dot(V, H), 0.0f);
            float3 F0   = lerp(float3(0.04f,0.04f,0.04f), albedo, metallic);
            float3 F    = FresnelSchlick(VdotH, F0);
            float3 kD   = (1.0f - F) * (1.0f - metallic);
            float3 spec = (DistributionGGX(NdotH, roughness) * GeometrySmith(NdotV, NdotL, roughness) * F)
                        / max(4.0f * NdotV * NdotL, 1e-6f);
            diffuseAcc += (kD * albedo / PI + spec) * lightColor * NdotL * attenuation * shadow;
        }
    }

    float3 emissive = float3(0,0,0);
    if ((gFlags & MF_USE_EMISSIVE) != 0u)
    {
        emissive = gEmissiveColor.rgb;
        float3 et = gEmissive.Sample(gSampler, uv).rgb;
        if (dot(et, et) > 0.000001f) emissive *= et;
    }

    float3 color = isPBR
        ? ambient * albedo * ao + diffuseAcc + emissive
        : (ambient + diffuseAcc) * albedo * ao + specularAcc + emissive;

    return float4(color, baseColor.a * (1.0f - gTransparency));
}
