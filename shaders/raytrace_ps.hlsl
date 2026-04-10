// raytrace_ps.hlsl
// Screen-space heightmap ray marching for water reflections and refractions.
//
// Reads: scene depth (t0), scene normals (t1), terrain heightmap (t2)
// Writes: ray-traced colour contribution with alpha used as blend weight.
//         The GPU hardware blend unit composites this on top of the scene HDR RT:
//            final = raytrace.rgb * raytrace.a  +  existing_scene * (1 - raytrace.a)
//
// Enabled / disabled via g_rtEnabled in RayTraceData (b9).
// Water surface is identified by world-Y within 2 m of g_seaLevel.
//
// Terrain collision uses linear march + 8-step binary refinement on heightmap.
// Water refraction bends the view ray through Snell's law (air→water IOR).

#include "Common.hlsli"

// ---------------------------------------------------------------------------
// Resources
// ---------------------------------------------------------------------------
Texture2D<float>  g_rtDepth    : register(t0);
Texture2D<float4> g_rtNormals  : register(t1);
Texture2D<float>  g_rtHeightmap: register(t2);

SamplerState g_rtSampler : register(s2);   // linearClamp (bound globally)

// ---------------------------------------------------------------------------
// Heightmap helpers
// ---------------------------------------------------------------------------

// Returns terrain world-Y at a given world XZ position, or -99999 if outside bounds.
float SampleTerrainH(float2 worldXZ)
{
    float2 uv = worldXZ / g_terrainSize + 0.5;
    if (any(saturate(uv) != uv)) return -99999.0;
    float raw = g_rtHeightmap.SampleLevel(g_rtSampler, uv, 0).r;
    return (raw - 0.3) * g_heightScale;
}

// Terrain surface normal estimated from heightmap central differences.
float3 TerrainNormal(float3 worldPos)
{
    float eps = 2.0;
    float hC = SampleTerrainH(worldPos.xz);
    float hX = SampleTerrainH(worldPos.xz + float2(eps, 0));
    float hZ = SampleTerrainH(worldPos.xz + float2(0, eps));
    return normalize(float3(hC - hX, eps, hC - hZ));
}

// Very fast approximate terrain albedo for reflected / refracted hits.
// Matches the 4-zone logic in terrain_ps (sand near sea level, grass, rock, snow).
float3 TerrainAlbedo(float3 hitPos)
{
    float hNorm        = hitPos.y / g_heightScale;
    float seaLevelNorm = g_seaLevel / g_heightScale;

    // Sand band around the waterline
    float sand = 1.0 - smoothstep(seaLevelNorm - 0.01, seaLevelNorm + 0.10, hNorm);
    float rock = saturate((hNorm - g_thresholds.y) / 0.08) * (1.0 - sand);
    float snow = saturate((hNorm - g_thresholds.z) / 0.08);

    float3 sandC  = float3(0.76, 0.70, 0.50);
    float3 grass  = float3(0.30, 0.44, 0.17);
    float3 rockC  = float3(0.46, 0.40, 0.34);
    float3 snowC  = float3(0.88, 0.92, 0.97);

    float3 albedo = lerp(grass, sandC, sand);
    albedo = lerp(albedo, rockC, rock);
    return lerp(albedo, snowC, snow);
}

// ---------------------------------------------------------------------------
// Ray marcher: linear scan + binary search within terrain heightmap.
// Returns true when a surface is hit; hitPos is the intersection point.
// ---------------------------------------------------------------------------
bool MarchTerrain(float3 ro, float3 rd, float maxDist, int steps, out float3 hitPos)
{
    hitPos = float3(0, 0, 0);
    if (steps < 4) steps = 4;
    float dt = maxDist / (float)steps;
    float t  = dt * 0.5;

    for (int i = 0; i < steps; ++i)
    {
        float3 p       = ro + rd * t;
        float  terrainH = SampleTerrainH(p.xz);
        if (terrainH < -9998.0) { t += dt; continue; }   // out of bounds

        if (p.y < terrainH)
        {
            // Binary search refinement
            float tA = t - dt;
            float tB = t;
            [unroll(8)]
            for (int j = 0; j < 8; ++j)
            {
                float  tM = (tA + tB) * 0.5;
                float3 pm = ro + rd * tM;
                if (pm.y < SampleTerrainH(pm.xz)) tB = tM;
                else                              tA = tM;
            }
            hitPos = ro + rd * ((tA + tB) * 0.5);
            return true;
        }
        t += dt;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Approximate sky colour for ray-missed reflections.
// ---------------------------------------------------------------------------
float3 SkyColor(float3 dir)
{
    float  upT   = saturate(dir.y * 1.5);
    float3 sky   = lerp(g_horizonColor, g_zenithColor, upT);
    float  sunDot = saturate(dot(dir, normalize(g_dirToSun)));
    sky += g_sunColor * (pow(sunDot, 48.0) * 0.9 + pow(sunDot, 8.0) * 0.08)
           * g_sunIntensity;
    return sky;
}

// ---------------------------------------------------------------------------
// Input / Output
// ---------------------------------------------------------------------------
struct PSIn {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

float4 PSMain(PSIn input) : SV_Target
{
    // If ray tracing disabled output transparent (blend factor = 0 → no change)
    if (!g_rtEnabled)
        return float4(0, 0, 0, 0);

    // -----------------------------------------------------------------------
    // Reconstruct surface world position
    // -----------------------------------------------------------------------
    float  depth    = g_rtDepth.SampleLevel(g_rtSampler, input.uv, 0).r;
    if (depth >= 1.0) return float4(0, 0, 0, 0);   // sky pixel — skip

    // -----------------------------------------------------------------------
    // Read GBuffer normal early.  w=1 is the water-surface flag written by
    // water_ps.  Water draws with a read-only depth stencil so the depth
    // buffer holds terrain depth, NOT water surface depth.  We therefore
    // detect water by the flag and override worldPos.y to g_seaLevel so the
    // reflection ray starts on the actual water surface.
    // -----------------------------------------------------------------------
    float4 nSample = g_rtNormals.SampleLevel(g_rtSampler, input.uv, 0);
    bool   isWater = nSample.w > 0.5;
    if (!isWater) return float4(0, 0, 0, 0);

    float3 worldPos = ReconstructWorldPos(input.uv, depth);
    worldPos.y      = g_seaLevel;    // water surface (Gerstner waves are ±0.5 m)
    float3 V        = normalize(worldPos - g_camPos);

    // -----------------------------------------------------------------------
    // Perturbed water normal from GBuffer (already encoded 0..1 → –1..1)
    // -----------------------------------------------------------------------
    float3 N = normalize(nSample.xyz * 2.0 - 1.0);
    // Clamp to upper hemisphere (water normals are always upward-facing)
    N = normalize(float3(N.x, max(N.y, 0.05), N.z));

    float3 rtColor = float3(0, 0, 0);
    float  alpha   = 0.0;

    // -----------------------------------------------------------------------
    // Reflection ray
    // -----------------------------------------------------------------------
    if (g_rtReflect)
    {
        float3 reflDir = reflect(V, N);

        // Lower angle gate slightly and blend out near-horizontal rays to prevent hard line
        if (reflDir.y > 0.005)
        {
            float3 hitPos;
            float3 reflColor;

            if (MarchTerrain(worldPos + N * 0.8, reflDir, g_rtMaxDist, g_rtSteps, hitPos))
            {
                float3 hitN  = TerrainNormal(hitPos);
                float  NdotL = saturate(dot(hitN, normalize(g_dirToSun)));
                float3 alb   = TerrainAlbedo(hitPos);
                reflColor    = alb * (NdotL * g_sunColor * g_sunIntensity
                                      + g_ambientColor * g_ambientIntensity);
                reflColor    = ApplyFog(reflColor, hitPos);

                // Fade terrain hit toward sky as the ray approaches max range,
                // preventing a hard colour discontinuity at the heightmap boundary.
                float hitDist  = length(hitPos - worldPos);
                float edgeFade = 1.0 - smoothstep(g_rtMaxDist * 0.55, g_rtMaxDist * 0.92, hitDist);
                reflColor      = lerp(SkyColor(reflDir), reflColor, edgeFade);
            }
            else
            {
                reflColor = SkyColor(reflDir);
            }

            // Fresnel: physically accurate for air/water interface (F0 ≈ 0.02, RTR4 §14.4.3)
            float cosAngle   = saturate(dot(-V, N));
            float F0         = 0.02;
            float fresnel    = F0 + (1.0 - F0) * pow(1.0 - cosAngle, 5.0);
            // Fade alpha for near-horizontal rays to remove the sharp horizontal line
            float rayAngFade = smoothstep(0.005, 0.05, reflDir.y);

            rtColor = lerp(rtColor, reflColor, fresnel);
            alpha   = max(alpha, fresnel * rayAngFade);
        }
    }

    // -----------------------------------------------------------------------
    // Refraction ray — chromatic aberration splits R/G/B across slightly
    // different IOR values (water dispersion ~0.005).
    // -----------------------------------------------------------------------
    if (g_rtRefract)
    {
        float  iorRatio = 1.0 / g_ior;           // air → water, green channel

        // Dispersion offsets for R (+) and B (−) channels
        float  iorR    = 1.0 / (g_ior - 0.005);
        float  iorB    = 1.0 / (g_ior + 0.005);

        float3 refrDirG = refract(V, N, iorRatio);
        float3 refrDirR = refract(V, N, iorR);
        float3 refrDirB = refract(V, N, iorB);

        // Use green direction validity as the master gate
        if (length(refrDirG) > 0.5 && refrDirG.y < -0.01)
        {
            float refrDist = min(g_rtMaxDist * 0.35, 120.0);

            float3 hitR = float3(0.03, 0.12, 0.20);
            float3 hitG = float3(0.02, 0.15, 0.22);
            float3 hitB = float3(0.01, 0.12, 0.26);
            {
                float3 hPos;
                if (MarchTerrain(worldPos - N * 0.5, refrDirR, refrDist, g_rtSteps, hPos))
                {
                    float  wd   = length(hPos - worldPos);
                    float  at   = exp(-wd * 0.06);
                    float3 hitN = TerrainNormal(hPos);
                    float  nl   = saturate(dot(hitN, normalize(g_dirToSun))) * 0.5 + 0.15;
                    hitR = lerp(float3(0.05, 0.22, 0.30), TerrainAlbedo(hPos) * nl, at * 0.85);
                }
                else hitR = float3(0.03, 0.12, 0.20);
            }
            {
                float3 hPos;
                if (MarchTerrain(worldPos - N * 0.5, refrDirG, refrDist, g_rtSteps, hPos))
                {
                    float  wd   = length(hPos - worldPos);
                    float  at   = exp(-wd * 0.06);
                    float3 hitN = TerrainNormal(hPos);
                    float  nl   = saturate(dot(hitN, normalize(g_dirToSun))) * 0.5 + 0.15;
                    hitG = lerp(float3(0.04, 0.26, 0.36), TerrainAlbedo(hPos) * nl, at * 0.85);
                }
                else hitG = float3(0.02, 0.15, 0.22);
            }
            {
                float3 hPos;
                if (MarchTerrain(worldPos - N * 0.5, refrDirB, refrDist, g_rtSteps, hPos))
                {
                    float  wd   = length(hPos - worldPos);
                    float  at   = exp(-wd * 0.06);
                    float3 hitN = TerrainNormal(hPos);
                    float  nl   = saturate(dot(hitN, normalize(g_dirToSun))) * 0.5 + 0.15;
                    hitB = lerp(float3(0.02, 0.20, 0.34), TerrainAlbedo(hPos) * nl, at * 0.85);
                }
                else hitB = float3(0.01, 0.12, 0.26);
            }

            float3 refrColor = float3(hitR.r, hitG.g, hitB.b);

            // At near-normal incidence you see through; at glancing you mostly reflect
            float cosAngle   = saturate(dot(-V, N));
            float refrWeight = cosAngle * cosAngle * 0.65;

            rtColor = lerp(rtColor, refrColor, refrWeight * (1.0 - alpha));
            alpha   = max(alpha, refrWeight * 0.65);
        }
    }

    // -----------------------------------------------------------------------
    // Fog on the combined ray-traced contribution
    // -----------------------------------------------------------------------
    rtColor = ApplyFog(rtColor, worldPos);

    // Alpha represents "how much the scene colour is replaced by the ray trace".
    // GPU blend: dst = src.rgb * src.a + dst.rgb * (1 - src.a)
    return float4(rtColor, saturate(alpha));
}
