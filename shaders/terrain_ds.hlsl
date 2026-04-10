// terrain_ds.hlsl
// Domain shader: bilinear-interpolates patch corners, samples heightmap,
// displaces Y, computes analytical normal, and outputs shadow UVs + clip dist.

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
    float4 svPos       : SV_Position;
    float3 worldPos    : POSITION0;
    float2 uv          : TEXCOORD0;
    float3 normal      : NORMAL0;
    float4 shadowUV0   : TEXCOORD1;
    float4 shadowUV1   : TEXCOORD2;
    float4 shadowUV2   : TEXCOORD3;
    float  clipDist    : SV_ClipDistance0;
};

[domain("quad")]
DSOut DSMain(PatchTess pt,
           float2 domainUV : SV_DomainLocation,
           const OutputPatch<HSOut, 4> patch)
{
    // Bilinear interpolation of patch corners
    // Patch order: [0]=BL, [1]=BR, [2]=TL, [3]=TR
    float2 posXZ = lerp(lerp(patch[0].posXZ, patch[1].posXZ, domainUV.x),
                        lerp(patch[2].posXZ, patch[3].posXZ, domainUV.x),
                        domainUV.y);
    float2 uv    = lerp(lerp(patch[0].uv, patch[1].uv, domainUV.x),
                        lerp(patch[2].uv, patch[3].uv, domainUV.x),
                        domainUV.y);

    // Sample heightmap
    float h = g_heightmap.SampleLevel(g_heightSampler, uv, 0).r;

    // Offset by -0.3 so the terrain spans roughly y=-66..+154, sea level at y=0
    float3 worldPos = float3(posXZ.x, (h - 0.3f) * g_heightScale, posXZ.y);

    // Analytical normal from heightmap gradient
    // Sample at neighbouring texels (1 texel = 1/1024 of UV space for 1025-sample heightmap)
    float delta = 1.0 / 1024.0;
    float hR = g_heightmap.SampleLevel(g_heightSampler, uv + float2(delta, 0),    0).r;
    float hU = g_heightmap.SampleLevel(g_heightSampler, uv + float2(0,    delta), 0).r;
    // tangentX: from current to hR (+X direction)
    // tangentZ: from current to hU (+Z direction)
    float3 tangentX = normalize(float3(g_terrainSize * delta, (hR - h) * g_heightScale, 0));
    float3 tangentZ = normalize(float3(0, (hU - h) * g_heightScale, g_terrainSize * delta));
    float3 normal   = normalize(cross(tangentZ, tangentX));

    // Output
    DSOut o;
    o.worldPos = worldPos;
    o.uv       = uv;
    o.normal   = normal;
    o.svPos    = mul(float4(worldPos, 1.0), g_viewProj);

    // Shadow UVs (NDC for each cascade)
    float4 wp4 = float4(worldPos, 1.0);
    o.shadowUV0 = mul(wp4, g_lightViewProj[0]);
    o.shadowUV1 = mul(wp4, g_lightViewProj[1]);
    o.shadowUV2 = mul(wp4, g_lightViewProj[2]);

    // Water clip plane: dot(worldPos, plane.xyz) + plane.w
    //   positive = inside (keep), negative = clip
    o.clipDist = dot(float4(worldPos, 1.0), g_clipPlane);

    return o;
}
