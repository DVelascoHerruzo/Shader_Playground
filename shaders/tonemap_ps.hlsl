// tonemap_ps.hlsl
// ACES filmic tone-mapping + exposure + bloom + god-rays + SSGI + underwater.
// Inputs:
//   t0 : HDR scene (optionally FXAA'd)
//   t1 : Bloom (upsampled, null → no bloom)
//   t2 : God rays (half-res radial, null → no god rays)
//   t3 : SSGI    (half-res indirect, null → no SSGI)

#include "Common.hlsli"

Texture2D    g_hdrScene  : register(t0);
Texture2D    g_bloom     : register(t1);
Texture2D    g_godRays   : register(t2);
Texture2D    g_ssgi      : register(t3);

SamplerState g_linearSampler : register(s0);
SamplerState g_pointSampler  : register(s3);

struct PSIn {
    float4 svPos : SV_Position;
    float2 uv    : TEXCOORD0;
};

float4 PSMain(PSIn input) : SV_Target0
{
    // -----------------------------------------------------------------------
    // Underwater UV distortion
    // -----------------------------------------------------------------------
    float2 sampleUV = input.uv;
    if (g_ppUnderwater)
    {
        float camDepth  = max(g_seaLevel - g_camPos.y, 0.0);
        float amplitude = 0.010 * saturate(camDepth * 0.25 + 0.4);
        float freq      = 22.0;
        sampleUV.x += sin(input.uv.y * freq        + g_time * 2.3) * amplitude;
        sampleUV.y += cos(input.uv.x * freq * 0.9  + g_time * 1.8) * amplitude;
    }

    // -----------------------------------------------------------------------
    // Sample HDR scene
    // -----------------------------------------------------------------------
    float3 hdr = g_ppUnderwater
        ? g_hdrScene.SampleLevel(g_linearSampler, sampleUV, 0).rgb
        : g_hdrScene.SampleLevel(g_pointSampler,  input.uv,  0).rgb;

    // -----------------------------------------------------------------------
    // Additive HDR contributions (pre-tonemap, same exposure block)
    // -----------------------------------------------------------------------
    float3 bloomColor   = g_bloom  .SampleLevel(g_linearSampler, input.uv, 0).rgb;
    float3 godRayColor  = g_godRays.SampleLevel(g_linearSampler, input.uv, 0).rgb;
    float3 ssgiColor    = g_ssgi   .SampleLevel(g_linearSampler, input.uv, 0).rgb;

    hdr += bloomColor  * g_bloomIntensity;
    hdr += godRayColor * g_godRayIntensity;
    hdr += ssgiColor   * g_ssgiIntensity;

    // Exposure
    hdr *= g_exposure;

    // ACES filmic tone-mapping
    float3 ldr = ACESFilmic(hdr);

    // Approximate sRGB gamma (pow 1/2.2)
    ldr = pow(max(ldr, 0.0), 1.0 / 2.2);

    // -----------------------------------------------------------------------
    // Underwater post-tonemap effects (LDR space)
    // -----------------------------------------------------------------------
    if (g_ppUnderwater)
    {
        float camDepth = max(g_seaLevel - g_camPos.y, 0.0);

        float depthFog  = 1.0 - exp(-camDepth * 0.045);
        float3 deepColor = float3(0.01, 0.18, 0.28);
        ldr = lerp(ldr, deepColor, depthFog * 0.55);

        float3 waterTint = float3(0.70, 0.88, 1.00);
        ldr *= waterTint;

        float caustVis = exp(-camDepth * 0.12);
        if (caustVis > 0.01)
        {
            float2 cUV = input.uv * 10.0;
            float c = sin(cUV.x * 1.0 + g_time * 1.3) * cos(cUV.y * 1.1 + g_time * 0.9)
                    + sin(cUV.x * 1.7 - g_time * 0.7) * cos(cUV.y * 1.4 + g_time * 1.2);
            c = pow((c + 2.0) * 0.25, 5.0) * 0.35;
            ldr += c * float3(0.50, 0.80, 0.90) * caustVis;
        }

        float shallowVis = exp(-camDepth * 0.6);
        if (shallowVis > 0.01)
        {
            float2 screenDist = abs(input.uv - float2(0.5, 0.25));
            float  rimLight   = exp(-dot(screenDist, screenDist) * 8.0);
            ldr += rimLight * float3(0.6, 0.9, 1.0) * shallowVis * 0.5;
        }

        float2 vigUV = input.uv * 2.0 - 1.0;
        float  vig   = 1.0 - saturate(dot(vigUV, vigUV) * 0.35);
        ldr *= vig;
    }

    return float4(saturate(ldr), 1.0);
}
