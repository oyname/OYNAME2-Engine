Texture2D gSceneColor : register(t0);
SamplerState gSampler : register(s1);

cbuffer BloomBlurParams : register(b0)
{
    float2 gTexelSize;
    float2 gDirection;
};

struct PSIn
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

static const float kWeights[9] =
{
    0.05f, 0.09f, 0.12f, 0.15f, 0.18f, 0.15f, 0.12f, 0.09f, 0.05f
};

float InBoundsWeight(float2 uv)
{
    return (uv.x >= 0.0f && uv.x <= 1.0f && uv.y >= 0.0f && uv.y <= 1.0f) ? 1.0f : 0.0f;
}

float3 SampleBloomValid(float2 uv, float weight, inout float weightSum)
{
    const float valid = InBoundsWeight(uv);
    if (valid <= 0.0f)
        return 0.0f.xxx;

    weightSum += weight;
    return gSceneColor.SampleLevel(gSampler, uv, 0).rgb * weight;
}

float4 main(PSIn input) : SV_TARGET
{
    // Root cause of the bottom-screen smear:
    // the previous version blurred with a widened kernel and relied on CLAMP sampling.
    // Near the lower/upper edges, out-of-range taps repeatedly sampled the last valid row,
    // which turned bright bottom-edge pixels into a large screen-space blur band.
    // Fix: reject off-screen taps and renormalize the kernel instead of reusing clamped edge texels.
    const float2 stepUV = gTexelSize * gDirection * 1.5f;

    float3 color = 0.0f.xxx;
    float weightSum = 0.0f;

    color += SampleBloomValid(input.uv - stepUV * 4.0f, kWeights[0], weightSum);
    color += SampleBloomValid(input.uv - stepUV * 3.0f, kWeights[1], weightSum);
    color += SampleBloomValid(input.uv - stepUV * 2.0f, kWeights[2], weightSum);
    color += SampleBloomValid(input.uv - stepUV * 1.0f, kWeights[3], weightSum);
    color += SampleBloomValid(input.uv,                  kWeights[4], weightSum);
    color += SampleBloomValid(input.uv + stepUV * 1.0f, kWeights[5], weightSum);
    color += SampleBloomValid(input.uv + stepUV * 2.0f, kWeights[6], weightSum);
    color += SampleBloomValid(input.uv + stepUV * 3.0f, kWeights[7], weightSum);
    color += SampleBloomValid(input.uv + stepUV * 4.0f, kWeights[8], weightSum);

    color /= max(weightSum, 1e-5f);
    return float4(color, 1.0f);
}
