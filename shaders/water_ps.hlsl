// water_ps.hlsl
// Water shading: Gerstner geometry + fine ripple normals, Beer-Lambert depth absorption,
// fixed linearised shore foam, Fresnel (RTR4 §14.4.3), GGX sun specular (RTR4 §9.8).
// Reflections are handled exclusively by raytrace_ps; this pass outputs refraction only.

#include "Common.hlsli"

// Note: Water::Draw() still binds the reflection RT at t0 for historical reasons;
// it is declared here to prevent slot conflicts but intentionally not sampled.
Texture2D        g_reflectionTex : register(t0);   // bound but unused — RT handles reflections
Texture2D        g_refractionTex : register(t1);
Texture2D<float> g_sceneDepth    : register(t2);

SamplerState g_linearSampler : register(s0);
SamplerState g_clampSampler  : register(s2);

// ---------------------------------------------------------------------------
// Procedural fine ripple normal (adds micro-detail on top of VS geometry)
// Uses diagonal wave directions to avoid visible axis-aligned stripes.
// ---------------------------------------------------------------------------
float3 RippleNormal(float2 worldXZ, float speed)
{
    float t = g_time * speed;
    // Pre-normalised diagonal directions
    const float2 d1 = float2( 0.8944,  0.4472);   // ~(2,1) normalised
    const float2 d2 = float2(-0.5547,  0.8321);   // ~(-2,3) normalised
    const float2 d3 = float2( 0.3162, -0.9487);   // ~(1,-3) normalised
    const float2 d4 = float2(-0.9285,  0.3714);   // ~(-5,2) normalised

    float p1 = dot(d1, worldXZ) * 0.08  + t * 1.10;
    float p2 = dot(d2, worldXZ) * 0.13  - t * 1.65;
    float p3 = dot(d3, worldXZ) * 0.22  + t * 2.20;
    float p4 = dot(d4, worldXZ) * 0.31  - t * 1.90;

    float2 d = float2(0, 0);
    d += d1 * cos(p1) * 0.13;
    d += d2 * cos(p2) * 0.09;
    d += d3 * cos(p3) * 0.06;
    d += d4 * cos(p4) * 0.04;
    return normalize(float3(-d.x, 1.0, -d.y));
}

// RT distortion offset (DuDv-style)
float2 WaveDuDv(float2 worldXZ, float speed, float strength)
{
    float t  = g_time * speed;
    float2 uv1 = worldXZ * 0.012 + float2( t * 0.11,  t * 0.08);
    float2 uv2 = worldXZ * 0.009 + float2(-t * 0.07, -t * 0.13);
    float dx = sin(uv1.x * 6.28) * cos(uv2.y * 5.1) + cos(uv1.y * 4.7) * sin(uv2.x * 6.0);
    float dy = cos(uv1.x * 5.3)  * sin(uv2.y * 6.6) + sin(uv1.y * 5.9) * cos(uv2.x * 4.8);
    return float2(dx, dy) * strength * 0.012;
}

// ---------------------------------------------------------------------------
struct PSIn {
    float4 svPos     : SV_Position;
    float3 worldPos  : POSITION0;
    float2 uv        : TEXCOORD0;
    float3 waveNormal: NORMAL0;
};

struct PSOut {
    float4 hdrColor : SV_Target0;
    float4 gbNormal : SV_Target1;  // packed water normal + w=1 (water flag for raytrace_ps)
};

PSOut PSMain(PSIn input)
{
    float2 screenUV = input.svPos.xy / float2(g_screenW, g_screenH);

    // --- Normals ---
    // Blend Gerstner geometric normal (large waves) with ripple detail (small waves)
    float3 geoN    = normalize(input.waveNormal);
    float3 rippleN = RippleNormal(input.worldPos.xz, g_waveSpeed);
    float3 N       = normalize(geoN + rippleN * 0.30);

    // --- RT distortion ---
    float2 distort = clamp(WaveDuDv(input.worldPos.xz, g_waveSpeed, g_waveStrength), -0.025, 0.025);

    // --- Depth-based water absorption (Beer-Lambert, RTR4 §14.3.2) --------
    // Attenuation coefficients for clear ocean water (R absorbed most, B least)
    // Compute VERTICAL water column depth by reconstructing terrain world Y.
    // Using view-space Z difference (old approach) over-estimates depth at glancing
    // camera angles, making even shallow water look opaque from the horizon.
    float rawSceneDepth = g_sceneDepth.Sample(g_clampSampler, screenUV).r;
    float depthUnder;
    if (rawSceneDepth >= 1.0) {
        depthUnder = 50.0;   // no terrain below (open ocean) — treat as deep
    } else {
        float3 terrainWorld = ReconstructWorldPos(screenUV, rawSceneDepth);
        depthUnder = max(0.0, g_seaLevel - terrainWorld.y);
    }

    float3 attenCoeff = float3(0.38, 0.14, 0.06);   // per-metre extinction
    float3 beerLambert = exp(-depthUnder * attenCoeff);

    float3 deepCol = float3(0.01, 0.06, 0.18);      // dark deep-water tint
    float  depthT  = saturate(depthUnder / 18.0);   // 0=shallow, 1=deep (for SSS/foam)

    // --- Refraction + Beer-Lambert absorption ----------------------------
    float2 refrUV  = clamp(screenUV + distort * 0.7, 0.001, 0.999);
    float3 refrCol = g_refractionTex.Sample(g_clampSampler, refrUV).rgb;
    // Apply Beer-Lambert: attenuate refracted colour, fill remainder with deep water tint
    refrCol = refrCol * beerLambert + deepCol * (1.0 - beerLambert);

    // --- Fresnel (RTR4 §14.4.3, Schlick) ---------------------------------
    // Reflection is handled by raytrace_ps; Fresnel here only controls specular weight.
    float3 V        = normalize(g_camPos - input.worldPos);
    float  cosTheta = saturate(dot(V, N));
    float  R0       = ((1.0 - g_ior) / (1.0 + g_ior));
    R0 *= R0;
    float  fresnel  = saturate(R0 + (1.0 - R0) * pow(1.0 - cosTheta, 5.0));

    // Water colour = refraction only (RT pass adds reflection on top)
    float3 waterCol = refrCol;

    // --- Sun specular: GGX Cook-Torrance (RTR4 §9.8) ---------------------
    // Roughness driven by wave amplitude: calm=mirror-sharp, rough=diffuse glitter
    float3 L     = normalize(g_dirToSun);
    float3 H     = normalize(L + V);
    float  NdotH = max(0.0, dot(N, H));
    float  NdotL = max(0.0, dot(N, L));
    float  NdotV = max(dot(N, V), 0.0001);

    float  alpha = g_waveStrength * 0.4 + 0.01;   // roughness driven by wave amplitude
    float  a2    = alpha * alpha;
    float  denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    float  D     = a2 / max(PI * denom * denom, 0.0001);
    float  k     = a2 * 0.5;                       // IBL remapping for G term
    float  G_V   = NdotV / max(NdotV * (1.0 - k) + k, 0.0001);
    float  G_L   = NdotL / max(NdotL * (1.0 - k) + k, 0.0001);
    float  G     = G_V * G_L;
    float3 F0_w  = float3(0.02, 0.02, 0.02);       // water F0
    float3 Fspec = F0_w + (1.0 - F0_w) * pow(1.0 - max(dot(H, V), 0.0), 5.0);
    float3 spec  = (D * G * Fspec) / max(4.0 * NdotV * NdotL, 0.0001);
    waterCol += g_sunColor * spec * g_sunIntensity * NdotL;

    // --- Subsurface scatter approximation (bright teal at lit shallow edges) ---
    float3 shallowCol = float3(0.06, 0.38, 0.58);
    NdotL = max(0.0, dot(N, L));
    waterCol += shallowCol * NdotL * (1.0 - fresnel) * (1.0 - depthT) * 0.30;

    // --- Shore foam ---
    // foamThreshold * 2.5 ≈ 3.75m zone (was 9m — way too wide, covered whole shallow lake)
    float foamZone = max(g_foamThreshold * 2.5, 0.4);
    float foam     = 1.0 - saturate(depthUnder / foamZone);
    foam = foam * foam;
    // Animate foam edge with wave motion
    float foamAnim = sin((input.worldPos.x + input.worldPos.z) * 0.4 + g_time * g_waveSpeed) * 0.5 + 0.5;
    foam *= lerp(0.5, 1.0, foamAnim);
    waterCol = lerp(waterCol, float3(0.96, 0.98, 1.00), foam * 0.90);

    // --- Fog ---
    waterCol = ApplyFog(waterCol, input.worldPos);

    PSOut o;
    o.hdrColor = float4(waterCol, 1.0);
    // Write packed wave normal to GBuffer so raytrace_ps can detect water pixels
    // and read the correct perturbed surface normal.  w=1 is the water flag.
    o.gbNormal = float4(N * 0.5 + 0.5, 1.0);
    return o;
}
