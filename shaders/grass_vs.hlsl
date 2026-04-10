// grass_vs.hlsl
// Instanced grass blade vertex shader.
//
// Slot 0 (per-vertex):   local unit-cross position + UV + normal (12 verts per clump)
// Slot 1 (per-instance): posXZ, worldY, rotation, spread, height, tint
//
// Wind displacement is applied proportionally to UV.y (blade tip sways more).
// Blades beyond drawDistance are moved off-screen so the rasterizer clips them.
//
// Reference techniques from GarrettGunnell/Grass (Unity):
//  - Unified wind direction with secondary flutter harmonic (vs. independent XZ sinusoids)
//  - Wang hash per-blade RNG for wind variance / droop
//  - Blade curvature: forward lean at tip proportional to uv.y^2 * height

#include "Common.hlsli"

// ---------------------------------------------------------------------------
struct VSIn {
    // Per-vertex (slot 0)
    float3 localPos    : POSITION;
    float2 uv          : TEXCOORD0;
    float3 localNormal : NORMAL;
    // Per-instance (slot 1, step rate = 1)
    float2 instPosXZ   : TEXCOORD1;
    float  instWorldY  : TEXCOORD2;
    float  instRot     : TEXCOORD3;
    float  instSpread  : TEXCOORD4;
    float  instHeight  : TEXCOORD5;
    float  instTint    : TEXCOORD6;
};

struct VSOut {
    float4 svPos    : SV_Position;
    float3 worldPos : POSITION0;
    float2 uv       : TEXCOORD0;
    float3 normal   : NORMAL0;
    float  tint     : TEXCOORD1;
    float  alpha    : TEXCOORD2;
    float2 instXZ   : TEXCOORD3;   // passed to PS for fog
};

// ---------------------------------------------------------------------------
// Wang hash (from GarrettGunnell/Grass Random.cginc) — deterministic per-blade
// pseudo-random scalar in [0, 1] from a float seed.
float WangHash(float seed)
{
    uint s = (uint)(seed * 1000000.0);
    s = (s ^ 61u) ^ (s >> 16u);
    s *= 9u;
    s ^= s >> 4u;
    s *= 0x27d4eb2du;
    s ^= s >> 15u;
    return (float)s * (1.0 / 4294967296.0);
}

// Amount of blade curvature: tip leans forward along the blade's facing direction.
// Reference ModelGrass: localPos.y += _Scale * uv.y^3 (forward lean at cubic rate)
//                        + _Droop * lerp(0.5,1,idH) * uv.y^2 * _Scale * animDir
// We express this as a single droop constant relative to blade height.
// 0.28 gives a noticeable but not droopy look matching the reference example images.
static const float BLADE_DROOP = 0.28f;

// ---------------------------------------------------------------------------
VSOut VSMain(VSIn i)
{
    VSOut o;

    // Distance-based fade and cull using dynamic draw distance from CB
    float dist      = distance(g_camPos.xz, i.instPosXZ);
    float fadeStart = g_foliageDrawDist * 0.75;
    float fadeEnd   = g_foliageDrawDist;
    o.alpha = saturate(1.0 - (dist - fadeStart) / max(fadeEnd - fadeStart, 0.01));

    // Clip fully-distant instances by sending them off-screen
    if (dist > fadeEnd + 1.0) {
        o.svPos    = float4(9999.0, 9999.0, 9999.0, 1.0);
        o.worldPos = float3(0, 0, 0);
        o.uv       = float2(0, 0);
        o.normal   = float3(0, 1, 0);
        o.tint     = 0;
        o.instXZ   = i.instPosXZ;
        return o;
    }

    // Per-blade random hash (Wang hash from reference) for wind variance and droop
    float idHash = WangHash(abs(i.instPosXZ.x * 127.1 + i.instPosXZ.y * 311.7));
    idHash       = WangHash(idHash * 100000.0);           // second hash pass

    // Scale local position by instance spread (XZ) and height (Y).
    // Taper: blade is widest at the base (uv.y=0) and narrows to a point at the tip.
    // Quadratic taper gives a natural triangular silhouette without a texture mask.
    float taper = max(0.04f, 1.0f - i.uv.y * i.uv.y);
    float3 lp = float3(
        i.localPos.x * i.instSpread * taper,
        i.localPos.y * i.instHeight,
        i.localPos.z * i.instSpread);   // Z=0 for flat quad

    // Camera-facing billboard: rotate the quad vertices so the face always looks at
    // the camera.  This only affects vertex positions — droop uses instRot below.
    float2 toCam   = g_camPos.xz - i.instPosXZ;
    float  billRot = atan2(-toCam.x, toCam.y);   // neg X so right-vector is ⊥ to view dir
    float  bc = cos(billRot),  bs = sin(billRot);
    lp = float3(lp.x * bc - lp.z * bs,  lp.y,  lp.x * bs + lp.z * bc);

    // Normal follows the billboard (faces camera).
    float c = bc, s = bs;

    // Billboard's world-space horizontal axis (right vector after camera-facing rotation).
    // Wind displacement is applied along this axis so the blade always sways
    // left/right as seen from the camera, regardless of viewing angle.
    float3 bladeRight   = float3(bc, 0.0f, bs);

    // Blade curvature / droop — forward lean uses instRot so each blade leans in its
    // own original direction regardless of where the camera is.
    float  ic = cos(i.instRot),  is_ = sin(i.instRot);
    float3 bladeForward = float3(-is_, 0.0f, ic);
    float  droopAmt     = i.uv.y * i.uv.y * i.instHeight * BLADE_DROOP * lerp(0.6f, 1.0f, idHash);

    // -----------------------------------------------------------------------
    // Wind — sway the tip along the billboard's horizontal axis so it always
    // looks like proper side-to-side bending from any camera direction.
    // -----------------------------------------------------------------------
    float phase    = frac(dot(i.instPosXZ, float2(127.1, 311.7)) * 0.01) * 6.28318;
    float localWindVar = lerp(0.40f, 0.75f, idHash);

    float mainFreq = g_windTime * g_windFrequency;
    float cosTime;
    if (localWindVar > 0.6f)
        cosTime = cos(mainFreq + phase);
    else
        cosTime = cos(mainFreq + phase + localWindVar * 0.1f);

    float trigVal = (cosTime * cosTime * 0.65f) - localWindVar * 0.5f;

    float windScl = i.instHeight * g_windStrength;
    float windAmt = i.uv.y * trigVal * windScl * localWindVar;
    lp.x += bladeRight.x * windAmt;
    lp.z += bladeRight.z * windAmt;

    // Droop: lean blade tip in its facing direction (purely aesthetic)
    float3 worldPos = float3(
        i.instPosXZ.x + lp.x + bladeForward.x * droopAmt,
        i.instWorldY  + lp.y,
        i.instPosXZ.y + lp.z + bladeForward.z * droopAmt);

    // Rotate local normal by instRot
    float3 ln = i.localNormal;
    float3 wn = normalize(float3(ln.x * c - ln.z * s,  ln.y,  ln.x * s + ln.z * c));

    o.svPos    = mul(float4(worldPos, 1.0), g_viewProj);
    o.worldPos = worldPos;
    o.uv       = i.uv;
    o.normal   = wn;
    o.tint     = i.instTint;
    o.instXZ   = i.instPosXZ;
    return o;
}
