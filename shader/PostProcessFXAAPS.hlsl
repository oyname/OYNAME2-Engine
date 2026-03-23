// PostProcessFXAAPS.hlsl
// Based on FXAA 3.11 by Timothy Lottes (NVIDIA)
// Same algorithm used verbatim in Unreal Engine 4/5 and Unity PostProcessing Stack.
//
// t0 = LDR scene colour (post ToneMapping)
// s1 = Linear Clamp (SamplerCache slot 1)

Texture2D gSceneColor : register(t0);
SamplerState gSampler : register(s1);

cbuffer FXAAParams : register(b0)
{
    float2 gTexelSize;
    float gContrastThreshold; // default 0.0312  (min edge luma delta)
    float gRelativeThreshold; // default 0.125   (relative to local max)
};

// Quality preset 12: 5 search steps with multipliers matching UE4 default.
static const int SEARCH_STEPS = 5;
static const float STEP_SIZES[SEARCH_STEPS] = { 1.0, 1.5, 2.0, 2.0, 4.0 };
static const float SUBPIX_QUALITY = 0.75; // same default as UE4 / Unity

struct PSIn
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

// Green channel luma — Unity and UE4 both use this approximation for speed.
float Luma(float3 c)
{
    return c.g * (0.587 / 0.299) * 0.5 + c.r * 0.5;
}
// (exact: dot(c, float3(0.299, 0.587, 0.114)) — identical shape, green-biased version is cheaper)

float4 main(PSIn input) : SV_TARGET
{
    float2 uv = input.uv;
    float2 ts = gTexelSize;

    float4 rgbyM = gSceneColor.Sample(gSampler, uv);
    float lumaM = Luma(rgbyM.rgb);

    // --- Cross luma ---
    float lumaN = Luma(gSceneColor.Sample(gSampler, uv + float2(0, -ts.y)).rgb);
    float lumaS = Luma(gSceneColor.Sample(gSampler, uv + float2(0, ts.y)).rgb);
    float lumaE = Luma(gSceneColor.Sample(gSampler, uv + float2(ts.x, 0)).rgb);
    float lumaW = Luma(gSceneColor.Sample(gSampler, uv + float2(-ts.x, 0)).rgb);

    float rangeMax = max(max(lumaN, lumaS), max(lumaE, max(lumaW, lumaM)));
    float rangeMin = min(min(lumaN, lumaS), min(lumaE, min(lumaW, lumaM)));
    float range = rangeMax - rangeMin;

    // Early exit — flat region
    if (range < max(gContrastThreshold, rangeMax * gRelativeThreshold))
        return rgbyM;

    // --- Corner luma (for subpixel aliasing + edge direction) ---
    float lumaNE = Luma(gSceneColor.Sample(gSampler, uv + float2(ts.x, -ts.y)).rgb);
    float lumaNW = Luma(gSceneColor.Sample(gSampler, uv + float2(-ts.x, -ts.y)).rgb);
    float lumaSE = Luma(gSceneColor.Sample(gSampler, uv + float2(ts.x, ts.y)).rgb);
    float lumaSW = Luma(gSceneColor.Sample(gSampler, uv + float2(-ts.x, ts.y)).rgb);

    // --- Subpixel aliasing (FXAA 3.11 exact formula) ---
    // Weighted neighbourhood average: direct neighbours count 2x, diagonals 1x, total = 12
    float lumaAvg = (2.0 * (lumaN + lumaS + lumaE + lumaW) + lumaNE + lumaNW + lumaSE + lumaSW) / 12.0;
    float subpixC = saturate(abs(lumaAvg - lumaM) / range);
    // Smooth step then square — same as UE4 "subpixF * subpixG" chain
    float subpixBlend = subpixC * subpixC * (3.0 - 2.0 * subpixC); // smoothstep
    subpixBlend = subpixBlend * subpixBlend * SUBPIX_QUALITY; // squared, then scaled

    // --- Edge direction (Sobel, identical to FXAA 3.11) ---
    float edgeH = abs(lumaNW + 2.0 * lumaN + lumaNE - lumaSW - 2.0 * lumaS - lumaSE);
    float edgeV = abs(lumaNW + 2.0 * lumaW + lumaSW - lumaNE - 2.0 * lumaE - lumaSE);
    bool isH = edgeH >= edgeV;

    // Perpendicular step toward the higher-contrast side
    float luma1 = isH ? lumaN : lumaW;
    float luma2 = isH ? lumaS : lumaE;
    float grad1 = abs(luma1 - lumaM);
    float grad2 = abs(luma2 - lumaM);
    bool side1 = grad1 >= grad2;

    float2 stepPerp = isH ? float2(0, side1 ? -ts.y : ts.y)
                           : float2(side1 ? -ts.x : ts.x, 0);
    float2 stepEdge = isH ? float2(ts.x, 0) : float2(0, ts.y);

    // --- Edge search along the edge ---
    float lumaEdge = (lumaM + (side1 ? luma1 : luma2)) * 0.5;
    float2 uvP = uv + stepPerp * 0.5 + stepEdge;
    float2 uvN = uv + stepPerp * 0.5 - stepEdge;
    float gradThresh = 0.25 * range;

    float deltaP = Luma(gSceneColor.Sample(gSampler, uvP).rgb) - lumaEdge;
    float deltaN = Luma(gSceneColor.Sample(gSampler, uvN).rgb) - lumaEdge;
    bool doneP = abs(deltaP) >= gradThresh;
    bool doneN = abs(deltaN) >= gradThresh;

    // Walk with increasing step sizes — matches FXAA 3.11 quality preset 12
    [unroll]
    for (int i = 0; i < SEARCH_STEPS; ++i)
    {
        float2 s = stepEdge * STEP_SIZES[i];
        if (!doneP)
        {
            uvP += s;
            deltaP = Luma(gSceneColor.Sample(gSampler, uvP).rgb) - lumaEdge;
            doneP = abs(deltaP) >= gradThresh;
        }
        if (!doneN)
        {
            uvN -= s;
            deltaN = Luma(gSceneColor.Sample(gSampler, uvN).rgb) - lumaEdge;
            doneN = abs(deltaN) >= gradThresh;
        }
    }

    float distP = isH ? abs(uvP.x - uv.x) : abs(uvP.y - uv.y);
    float distN = isH ? abs(uvN.x - uv.x) : abs(uvN.y - uv.y);
    float dist = min(distP, distN);
    float span = max(distP + distN, 0.0001);

    // Blend only when we are on the short side of an edge ending (FXAA 3.11 rule)
    float lumaEndP = distP < distN ? deltaP : deltaN;
    bool correct = (lumaM - lumaEdge < 0.0) != (lumaEndP < 0.0);
    float edgeBlend = correct ? (0.5 - dist / span) : 0.0;

    float blend = max(subpixBlend, edgeBlend);
    return gSceneColor.Sample(gSampler, uv + stepPerp * blend);
}