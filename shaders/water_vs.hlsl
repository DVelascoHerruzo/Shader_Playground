// water_vs.hlsl
// Vertex shader for the water surface mesh at SEA_LEVEL.
// Animates vertex positions using 4 overlapping Gerstner waves.
// Outputs an analytical wave normal for per-pixel lighting in the PS.

#include "Common.hlsli"

struct VSIn {
    float3 pos : POSITION;
    float2 uv  : TEXCOORD0;
};

struct VSOut {
    float4 svPos     : SV_Position;
    float3 worldPos  : POSITION0;
    float2 uv        : TEXCOORD0;
    float3 waveNormal: NORMAL0;
};

// ---------------------------------------------------------------------------
// One Gerstner wave contribution.
// Accumulates XYZ displacement and the normal perturbation vector.
// dir:   normalised horizontal wave direction
// freq:  2*PI / wavelength  (higher = shorter wave)
// amp:   wave amplitude (metres)
// steep: horizontal trochoidal steepness [0..1]
// speed: phase speed multiplier (scaled by g_waveSpeed from CB)
// ---------------------------------------------------------------------------
void GerstnerContrib(float2 xz,
                     float2 dir, float freq, float amp, float steep, float speed,
                     inout float3 disp, inout float3 nAcc)
{
    float phase = dot(dir, xz) * freq + g_time * speed * g_waveSpeed * 2.5;
    float S = sin(phase);
    float C = cos(phase);

    // Horizontal trochoid offset + vertical sine
    disp.x += steep * amp * dir.x * C;
    disp.z += steep * amp * dir.y * C;
    disp.y += amp * S;

    // Analytical normal accumulation (Gerstner formula)
    nAcc.x -= dir.x * freq * amp * C;
    nAcc.z -= dir.y * freq * amp * C;
    nAcc.y -= steep * freq * amp * S;
}

// ---------------------------------------------------------------------------
VSOut VSMain(VSIn i)
{
    // Total amplitude scales with waveStrength (default 0.018 * 80 ≈ 1.4m)
    float A = g_waveStrength * 80.0;

    float3 disp = float3(0, 0, 0);
    float3 nAcc = float3(0, 1, 0);   // start from up, accumulate perturbations

    // Wave 1: primary long swell
    GerstnerContrib(i.pos.xz, normalize(float2( 1.0,  0.3)), 0.14, A * 0.40, 0.40, 1.00, disp, nAcc);
    // Wave 2: crossing wave
    GerstnerContrib(i.pos.xz, normalize(float2( 0.4,  1.0)), 0.21, A * 0.30, 0.35, 1.30, disp, nAcc);
    // Wave 3: short choppy
    GerstnerContrib(i.pos.xz, normalize(float2(-0.7,  0.7)), 0.37, A * 0.16, 0.50, 0.85, disp, nAcc);
    // Wave 4: counter-direction detail
    GerstnerContrib(i.pos.xz, normalize(float2( 0.6, -0.8)), 0.27, A * 0.22, 0.30, 1.18, disp, nAcc);

    float3 worldPos = i.pos + disp;

    VSOut o;
    o.worldPos  = worldPos;
    o.uv        = i.uv;
    o.waveNormal = normalize(nAcc);
    o.svPos      = mul(float4(worldPos, 1.0), g_viewProj);
    return o;
}

