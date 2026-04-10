// ssao_blur_ps.hlsl
// Edge-aware (bilateral) blur of the raw SSAO result.
// Blurs in a 4×4 neighbourhood, weighting samples by depth similarity.

#include "Common.hlsli"

Texture2D<float>  g_ssaoRaw  : register(t0);
Texture2D<float>  g_depthTex : register(t1);

SamplerState g_pointSampler : register(s3);

struct PSIn {
    float4 svPos : SV_Position;
    float2 uv    : TEXCOORD0;
};

float PSMain(PSIn input) : SV_Target0
{
    float2 texelSize = 1.0 / float2(g_screenW * 0.5, g_screenH * 0.5);
    float  centerDepth = LinearDepth(g_depthTex.SampleLevel(g_pointSampler, input.uv, 0).r);
    float  result      = 0.0;
    float  totalWeight = 0.0;

    for (int y = -1; y <= 2; ++y)
    for (int x = -1; x <= 2; ++x)
    {
        float2 sampleUV    = input.uv + float2(x, y) * texelSize;
        float  sampleDepth = LinearDepth(g_depthTex.SampleLevel(g_pointSampler, sampleUV, 0).r);
        float  sampleAO    = g_ssaoRaw.SampleLevel(g_pointSampler, sampleUV, 0).r;

        // Bilateral weight: down-weight samples with very different depth
        float depthDiff = abs(sampleDepth - centerDepth);
        float weight    = exp(-depthDiff * 2.0);

        result      += sampleAO * weight;
        totalWeight += weight;
    }

    return result / max(totalWeight, 0.0001);
}
