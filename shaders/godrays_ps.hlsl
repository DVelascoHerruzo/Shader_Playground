// godrays_ps.hlsl
// Radial blur "god rays" — marches from the current pixel toward the sun's
// screen-space position, accumulating scene colour to fake volumetric scattering.
// Runs at half-resolution for performance.
//
// Inputs:
//   t0 : HDR scene  (R16G16B16A16_FLOAT)
//   t1 : Scene linear depth (R32_FLOAT or R32_TYPELESS SRV)
//
// The sun world position is reconstructed in the shader from g_dirToSun × a
// large distance, projected via g_viewProj.  The god-ray contribution is only
// injected when the sun is in front of the camera and on-screen.

#include "Common.hlsli"

Texture2D<float4> g_scene : register(t0);
Texture2D<float>  g_depth : register(t1);

SamplerState g_linearSampler : register(s0);

struct PSIn {
    float4 svPos : SV_Position;
    float2 uv    : TEXCOORD0;
};

float4 PSMain(PSIn input) : SV_Target0
{
    // Project sun world position to screen UV
    float4 sunClip = mul(float4(g_dirToSun * 5000.0f, 1.0f), g_viewProj);
    if (sunClip.w <= 0.001f) return float4(0, 0, 0, 0);   // behind camera

    float2 sunNDC = sunClip.xy / sunClip.w;
    // Skip if sun is far off-screen (no contribution visible)
    if (abs(sunNDC.x) > 2.0f || abs(sunNDC.y) > 2.0f) return float4(0, 0, 0, 0);

    float2 sunUV = float2(sunNDC.x * 0.5f + 0.5f, -sunNDC.y * 0.5f + 0.5f);

    // Night / below-horizon fade
    float sunElev   = g_dirToSun.y;
    float nightFade = saturate(sunElev * 6.0f + 0.15f);
    if (nightFade < 0.01f) return float4(0, 0, 0, 0);

    // Radial march parameters
    const int   numSamples  = 80;
    const float decay       = 0.97f;     // sample contribution decay per step
    const float exposure    = 0.15f;    // overall god-ray brightness
    const float stepScale   = 0.9f;

    float2 delta = (input.uv - sunUV) * (stepScale / numSamples);
    float2 tc    = input.uv;
    float  illum = 1.0f;
    float3 color = float3(0, 0, 0);

    [loop]
    for (int i = 0; i < numSamples; ++i)
    {
        tc -= delta;
        float2 clampUV = saturate(tc);

        // Accumulate scene brightness — sky pixels contribute, terrain pixels are dark
        float3 sample = g_scene.SampleLevel(g_linearSampler, clampUV, 1).rgb;

        // Weight by depth: sky pixels (depth ≈ 1) contribute fully
        float d = g_depth.SampleLevel(g_linearSampler, clampUV, 1).r;
        float skyMask = saturate((d - 0.9999f) * 10000.0f);  // 1 for sky, 0 for geometry

        // Only bright sky areas generate god rays
        float lum = dot(sample, float3(0.2126f, 0.7152f, 0.0722f));
        color    += sample * lum * illum * skyMask;
        illum    *= decay;
    }

    color *= exposure / numSamples;

    // Tint by sun colour and night fade
    color *= g_sunColor * nightFade;

    return float4(color, 1.0f);
}
