// sky_ps.hlsl
// Procedural atmospheric sky with Rayleigh-Mie scattering approximation.
// Day and night HDRI panoramas (t12/t13) are blended and drive the sky color,
// with procedural sun disc, Mie scattering, and fog layered on top.

#include "Common.hlsli"

// Day HDRI panorama  (t12) – DaySkyHDRI056B_TONEMAPPED
Texture2D<float4> g_skyboxDay   : register(t12);
// Night HDRI panorama (t13) – NightSkyHDRI002_TONEMAPPED
Texture2D<float4> g_skyboxNight : register(t13);
SamplerState      g_wrapSampler : register(s0);   // anisotropic-wrap (global s0)
SamplerState      g_clampSampler: register(s2);   // linear-clamp     (global s2)

struct PSIn {
    float4 svPos : SV_Position;
    float2 uv    : TEXCOORD0;
};

// ---------------------------------------------------------------------------
// Mie scattering phase (Henyey-Greenstein approximation)
float MiePhase(float cosAngle, float g)
{
    float g2 = g * g;
    float denom = 1.0 + g2 - 2.0 * g * cosAngle;
    return (1.0 - g2) / (4.0 * PI * pow(max(denom, 0.0001), 1.5));
}

// ---------------------------------------------------------------------------
float4 PSMain(PSIn input) : SV_Target0
{
    // Reconstruct world-space view ray from NDC
    float ndcX =  input.uv.x * 2.0 - 1.0;
    float ndcY = -input.uv.y * 2.0 + 1.0;
    float4 worldPos = mul(float4(ndcX, ndcY, 1.0, 1.0), g_invViewProj);
    worldPos.xyz /= worldPos.w;
    float3 rayDir = normalize(worldPos.xyz - g_camPos);

    float3 sunDir  = normalize(g_dirToSun);
    float  cosAngle = dot(rayDir, sunDir);
    float  sinElev  = sunDir.y;           // sun elevation: -1 below, +1 overhead

    // ---- Day/night factor -----------------------------------------------------
    // Smooth crossfade: fully dark below -0.1, fully bright above 0.20
    float dayFade   = saturate(sinElev * 4.0 + 0.4);

    // ---- Rayleigh sky color ---------------------------------------------------
    // Approximate Rayleigh scattering: zenith is blue, horizon shifts toward cyan/white.
    // We drive colors from the CB values (g_zenithColor, g_horizonColor) so UI
    // controls still work, and blend in orange/red at sunrise/sunset when sunDir.y ≈ 0.
    float3 zenithDay  = g_zenithColor;
    float3 horizonDay = g_horizonColor;

    // Sunrise / sunset warm tint: orange glow bleeds upward when sun is near horizon
    float3 sunsetColor = float3(1.0, 0.45, 0.15);
    float  sunsetBand  = saturate(1.0 - abs(sinElev) * 6.0);   // strong when sun near horizon
    horizonDay = lerp(horizonDay, sunsetColor, sunsetBand * 0.75);
    zenithDay  = lerp(zenithDay,  float3(0.12, 0.20, 0.55), sunsetBand * 0.35);

    // Blend zenith → horizon based on view elevation angle
    // pow(saturate(rayDir.y), 0.4) gives a natural falloff without harsh line at horizon
    float elevT = pow(saturate(rayDir.y * 1.5 + 0.05), 0.45);
    float3 skyColor = lerp(horizonDay, zenithDay, elevT);

    // Night sky: dark blue
    float3 nightSky = float3(0.01, 0.02, 0.06);
    skyColor = lerp(nightSky, skyColor, dayFade);

    // ---- Stars (visible at night, fade out at dawn) -----------------------
    // Only draw stars above the horizon (rayDir.y > 0).
    // Hash the sky direction into a dense grid of pseudo-random points.
    if (rayDir.y > 0.0)
    {
        // Map hemisphere to a 2D grid for stable hashing
        float3 rd_n  = normalize(rayDir);
        // Spherical coords scaled — 120×80 = ~9600 star cells (was 60k, causing artifacts)
        float2 starUV = float2(atan2(rd_n.z, rd_n.x), acos(rd_n.y)) * float2(120.0, 80.0);
        float2 cell   = floor(starUV);
        float2 frac_  = frac(starUV);

        // Hash cell to a random position within it and a random brightness
        float2 h1 = frac(sin(cell * float2(127.1, 311.7) + 19.19) * 43758.5453);
        float2 h2 = frac(sin(cell * float2(269.5, 183.3) + 7.31)  * 73856.7891);
        float  brightness = frac(sin(dot(cell, float2(41.0, 289.0))) * 43758.5453);

        // Distance from frag to star centre
        float2 starPos = h1 * 0.8 + 0.1;
        float  d = length(frac_ - starPos);

        // Smaller, sharper disc — reduces bleed between adjacent cells
        float  starDisc  = smoothstep(0.05, 0.0, d);
        float  starGlow  = exp(-d * d * 120.0) * 0.08;
        // Raise cutoff: only top ~40% of cells show a star
        float  starVal   = (starDisc + starGlow) * saturate(brightness * 1.5 - 0.55);

        // Twinkle: slight flicker driven by wind time (reuse g_windTime from FoliageData,
        // but sky doesn't have that CB — use g_timeOfDay which ticks once per second slowly.
        // Use a stable per-star random phase instead.
        float twinkle = 0.75 + 0.25 * sin(g_timeOfDay * 3.0 + h2.x * 62.83);

        // Mix: bluer tiny stars, whiter brighter ones
        float3 starColor = lerp(float3(0.7, 0.8, 1.0), float3(1.0, 0.98, 0.9), brightness);
        // Night HDRI already contains photographed stars; keep only a faint
        // procedural twinkle layer on top so they appear to shimmer.
        float  starFade  = saturate(1.0 - dayFade * 2.5) * saturate(rayDir.y * 6.0);

        skyColor += starColor * starVal * twinkle * starFade * 0.15;
    }

    // ---- HDRI sky panoramas (day=t12, night=t13) --------------------------------
    // Sample both HDRI panoramas and blend between them based on dayFade so the
    // sky transitions naturally from photographed daylight to photographed night sky.
    float2 skyUV;
    skyUV.x = atan2(rayDir.z, rayDir.x) / (2.0 * PI) + 0.5;
    skyUV.y = acos(clamp(rayDir.y, -1.0, 1.0)) / PI;
    float3 dayPano   = g_skyboxDay.SampleLevel(g_wrapSampler, skyUV, 0).rgb;
    float3 nightPano = g_skyboxNight.SampleLevel(g_wrapSampler, skyUV, 0).rgb;
    // Smooth transition: full night sky below dayFade=0.25, full day above dayFade=0.65
    float hdriBlend = smoothstep(0.25, 0.65, dayFade);
    float3 panorama = lerp(nightPano, dayPano, hdriBlend);
    // HDRI is the primary sky color — drive it strongly, leaving room for sun/fog
    float panoLum     = dot(panorama, float3(0.299, 0.587, 0.114));
    float panoStrength = saturate(panoLum * 5.0) * 0.92;
    skyColor = lerp(skyColor, panorama, panoStrength);

    // ---- Mie scattering glow around sun --------------------------------------
    // Multi-lobe Mie: tight limb corona + wide aerial perspective haze
    float mie1  = MiePhase(cosAngle, 0.92) * 0.04;   // tight haze ring
    float mie2  = MiePhase(cosAngle, 0.70) * 0.008;  // broad atmospheric glow
    float3 mieColor = g_sunColor * (mie1 + mie2) * g_sunIntensity;
    skyColor += mieColor * dayFade;

    // ---- Sun disc + limb darkening -------------------------------------------
    float sunDisc = saturate(pow(max(0.0, cosAngle - 0.9997) / 0.0003, 2.0));
    // Chromatic limb: edge is redder / cooler than core
    float3 sunCore = g_sunColor * float3(1.5, 1.4, 1.0);
    float3 sunEdge = g_sunColor * float3(1.2, 0.75, 0.40);
    skyColor += lerp(sunEdge, sunCore, sunDisc) * sunDisc * 22.0
                * g_sunIntensity * dayFade;

    // ---- Below-horizon ground fog --------------------------------------------
    if (rayDir.y < 0.0)
    {
        float t = saturate(-rayDir.y / 0.10);
        skyColor = lerp(skyColor, g_fogColor * 0.6, t * t);
    }

    // ---- Horizon aerial perspective (fog at 0 elevation) ---------------------
    float horizBand = pow(saturate(1.0 - abs(rayDir.y) / 0.15), 4.0);
    skyColor = lerp(skyColor, g_horizonColor * 0.7 * dayFade, horizBand * 0.25);

    return float4(max(float3(0, 0, 0), skyColor), 1.0);
}

