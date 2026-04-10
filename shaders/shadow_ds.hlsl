// shadow_ds.hlsl
// Domain shader for the shadow-map render passes.
// Identical heightmap displacement to terrain_ds but outputs positions
// in light-space clip coordinates for the current cascade.
// Bound alongside terrain_hs.hlsl and terrain_vs.hlsl; no PS is used.

#include "Common.hlsli"

Texture2D<float> g_heightmap     : register(t0);
SamplerState     g_heightSampler : register(s2);   // linearClamp

struct HSOut {
    float2 posXZ : POSITION;
    float2 uv    : TEXCOORD0;
};

struct PatchTess {
    float edgeTess[4]   : SV_TessFactor;
    float insideTess[2] : SV_InsideTessFactor;
};

struct DSOut {
    float4 svPos   : SV_Position;
    float  clipDist : SV_ClipDistance0;   // always 1.0 (never clipped in shadow pass)
};

[domain("quad")]
DSOut DSMain(PatchTess pt,
           float2 domainUV : SV_DomainLocation,
           const OutputPatch<HSOut, 4> patch)
{
    // Bilinear interpolation — patch order: [0]=BL, [1]=BR, [2]=TL, [3]=TR
    float2 posXZ = lerp(lerp(patch[0].posXZ, patch[1].posXZ, domainUV.x),
                        lerp(patch[2].posXZ, patch[3].posXZ, domainUV.x),
                        domainUV.y);
    float2 uv    = lerp(lerp(patch[0].uv, patch[1].uv, domainUV.x),
                        lerp(patch[2].uv, patch[3].uv, domainUV.x),
                        domainUV.y);

    float h = g_heightmap.SampleLevel(g_heightSampler, uv, 0).r;
    // Match terrain_ds offset so shadow geometry is accurate
    float3 worldPos = float3(posXZ.x, (h - 0.3f) * g_heightScale, posXZ.y);

    DSOut o;
    // Project into the current cascade's light space
    o.svPos   = mul(float4(worldPos, 1.0), g_lightViewProj[g_cascadeIndex]);
    o.clipDist = 1.0;    // never clip in shadow pass
    return o;
}
