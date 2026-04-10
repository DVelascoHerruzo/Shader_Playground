// bloom_ps.hlsl
// Dual-Kawase multi-resolution bloom with lens flare ghosts and starburst.
//
// Three separate entry points driven by PostProcess.cpp:
//   PSBright     — isolate pixels above threshold (bright-pass)
//   PSDownsample — Kawase 4-tap downsample (used 4×)
//   PSUpsample   — tent 9-tap upsample + lens-flare injection (used 4×)
//
// All passes use t0 as the input and write to the current RTV.
// CB b6 = PostProcessData (g_bloomThreshold, g_bloomIntensity, g_lensFlareIntensity …)

#include "Common.hlsli"

Texture2D<float4> g_src : register(t0);
SamplerState      g_linearSampler : register(s0);

struct PSIn {
    float4 svPos : SV_Position;
    float2 uv    : TEXCOORD0;
};

// ===========================================================================
// PSBright  — extract pixels brighter than threshold
// ===========================================================================
float4 PSBright(PSIn input) : SV_Target0
{
    float3 hdr = g_src.SampleLevel(g_linearSampler, input.uv, 0).rgb;
    float  lum = dot(hdr, float3(0.2126f, 0.7152f, 0.0722f));
    // Soft knee: smooth transition around threshold
    float  knee  = g_bloomThreshold * 0.5f;
    float  wb    = saturate((lum - (g_bloomThreshold - knee)) / (2.0f * knee + 0.001f));
    return float4(hdr * wb, 1.0f);
}

// ===========================================================================
// PSDownsample  — Dual Kawase 4-tap downsample (box filter at half-texel)
// ===========================================================================
float4 PSDownsample(PSIn input) : SV_Target0
{
    float2 texel = rcp(float2(g_screenW, g_screenH));  // approximate; fine for bloom
    float4 s = float4(0, 0, 0, 0);
    s += g_src.SampleLevel(g_linearSampler, input.uv + float2(-texel.x, -texel.y), 0);
    s += g_src.SampleLevel(g_linearSampler, input.uv + float2( texel.x, -texel.y), 0);
    s += g_src.SampleLevel(g_linearSampler, input.uv + float2(-texel.x,  texel.y), 0);
    s += g_src.SampleLevel(g_linearSampler, input.uv + float2( texel.x,  texel.y), 0);
    return s * 0.25f;
}

// ===========================================================================
// PSUpsample  — tent 9-tap upsample + lens flare injection
// ===========================================================================

// Circular ghost flare disc centred at `centre` with given radius.
float3 GhostFlare(float2 uv, float2 centre, float radius, float3 tint)
{
    float dist = length((uv - centre) * float2(g_screenW / g_screenH, 1.0f));
    float disc = saturate(1.0f - dist / max(radius, 0.0001f));
    disc = disc * disc * (3.0f - 2.0f * disc);  // smoothstep
    return disc * tint;
}

float4 PSUpsample(PSIn input) : SV_Target0
{
    float2 texel = rcp(float2(g_screenW, g_screenH));

    // 3×3 tent filter
    float4 s = float4(0, 0, 0, 0);
    s += g_src.SampleLevel(g_linearSampler, input.uv + float2(-texel.x, -texel.y), 0) * 1.0f;
    s += g_src.SampleLevel(g_linearSampler, input.uv + float2( 0.0f,    -texel.y), 0) * 2.0f;
    s += g_src.SampleLevel(g_linearSampler, input.uv + float2( texel.x, -texel.y), 0) * 1.0f;
    s += g_src.SampleLevel(g_linearSampler, input.uv + float2(-texel.x,  0.0f   ), 0) * 2.0f;
    s += g_src.SampleLevel(g_linearSampler, input.uv + float2( 0.0f,     0.0f   ), 0) * 4.0f;
    s += g_src.SampleLevel(g_linearSampler, input.uv + float2( texel.x,  0.0f   ), 0) * 2.0f;
    s += g_src.SampleLevel(g_linearSampler, input.uv + float2(-texel.x,  texel.y), 0) * 1.0f;
    s += g_src.SampleLevel(g_linearSampler, input.uv + float2( 0.0f,     texel.y), 0) * 2.0f;
    s += g_src.SampleLevel(g_linearSampler, input.uv + float2( texel.x,  texel.y), 0) * 1.0f;
    s /= 16.0f;

    // ----- Lens flare (injected during the final upsample pass) ---------------
    if (g_lensFlareIntensity > 0.001f)
    {
        float4 sunClip = mul(float4(g_dirToSun * 5000.0f, 1.0f), g_viewProj);
        if (sunClip.w > 0.001f)
        {
            float2 sunNDC = sunClip.xy / sunClip.w;
            float2 sunUV  = float2(sunNDC.x * 0.5f + 0.5f, -sunNDC.y * 0.5f + 0.5f);

            // Only active when sun is roughly on screen
            float onScreen = saturate(1.0f - max(abs(sunNDC.x), abs(sunNDC.y)));
            if (onScreen > 0.01f)
            {
                float nightFade = saturate(g_dirToSun.y * 6.0f + 0.15f);
                float lensBase  = g_lensFlareIntensity * g_sunIntensity * nightFade * onScreen;

                float3 lensFlare = float3(0, 0, 0);

                // Sun screen→centre axis for ghost positioning
                float2 axis  = float2(0.5f, 0.5f) - sunUV;

                // Ghost 1: opposite side of screen
                float2 g1 = sunUV + axis * 0.5f;
                lensFlare += GhostFlare(input.uv, g1, 0.07f, float3(1.0f, 0.65f, 0.25f));

                // Ghost 2: further along axis in opposite direction
                float2 g2 = sunUV + axis * 1.25f;
                lensFlare += GhostFlare(input.uv, g2, 0.045f, float3(0.40f, 0.80f, 1.0f));

                // Ghost 3: even further
                float2 g3 = sunUV + axis * 2.0f;
                lensFlare += GhostFlare(input.uv, g3, 0.030f, float3(0.70f, 0.40f, 1.0f));

                // Small chromatic ring near sun
                float2 ringOffset = input.uv - sunUV;
                float  ringD      = length(ringOffset * float2(g_screenW / g_screenH, 1.0f));
                float  ring       = exp(-abs(ringD - 0.09f) * 80.0f) * 0.6f;
                lensFlare        += ring * g_sunColor * float3(0.8f, 0.6f, 1.0f);

                // Diffraction starburst around the sun (6 spikes)
                float2 starOff = input.uv - sunUV;
                float  starD   = length(starOff * float2(g_screenW / g_screenH, 1.0f));
                float  starAng = atan2(starOff.y, starOff.x * g_screenH / g_screenW);
                float  spikes  = abs(sin(starAng * 6.0f + g_time * 0.4f));
                float  burst   = exp(-starD * 28.0f) * spikes * 0.8f;
                lensFlare     += burst * g_sunColor;

                s.rgb += lensFlare * lensBase;
            }
        }
    }

    return s;
}
