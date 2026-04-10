// terrain_ps.hlsl
// Physically-based terrain shading.
// Biome blending is driven by Florinsky (1998) morphometric attributes
// computed on the CPU (TerrainGenerator::ComputeFlorinsky) and stored in
// two RGBA16F morpho textures (t1/t2).  Five real albedo textures are
// sampled with triplanar projection and blended according to the morpho data.
// Shading model: GGX Cook-Torrance PBR, 3-cascade PCF or RT soft shadows,
// screen-space SSAO, exponential per-pixel fog.
//
// Texture registers:
//   t0  : heightmap (float, re-used by RT shadow marcher)
//   t1  : Florinsky morpho1  (slope, aspect, kh, kv)  — RGBA16F
//   t2  : Florinsky morpho2  (kM, TWI, excessH, excessV) — RGBA16F
//   t3  : grass albedo (sRGB)
//   t4  : dirt  albedo (sRGB)
//   t5  : mud   albedo (sRGB)
//   t6  : rock  albedo (sRGB)
//   t7  : snow  albedo (sRGB)
//   t8-t10 : shadow cascade maps
//   t11 : sand normal map   (EXR, OpenGL convention; terrain passes only)
//   t12 : sand albedo        (terrain passes only)
//   t13 : sand roughness     (terrain passes only)
//   t14 : sand displacement  (terrain passes only)
//   t15 : snow normal map   (EXR, OpenGL convention; terrain passes only)
//   t16 : snow roughness     (terrain passes only)
//
// Sampler registers (bound globally by Renderer):
//   s0  : anisotropic wrap  (terrain albedo textures)
//   s1  : PCF comparison    (shadow maps)
//   s2  : linear clamp      (heightmap / morpho)
//   s3  : point clamp       (SSAO)

#include "Common.hlsli"

// Heightmap (RT shadow marcher)
Texture2D<float>   g_hmPS     : register(t0);

// Florinsky morphometric attribute maps
Texture2D<float4>  g_morpho1  : register(t1);   // slope_n, aspect_n, kh_n, kv_n
Texture2D<float4>  g_morpho2  : register(t2);   // kM_n, TWI_n, exH_n, exV_n

// Terrain albedo textures (sRGB, triplanar-projected)
Texture2D<float4>  g_texGrass : register(t3);
Texture2D<float4>  g_texDirt  : register(t4);
Texture2D<float4>  g_texMud   : register(t5);
Texture2D<float4>  g_texRock  : register(t6);
Texture2D<float4>  g_texSnow  : register(t7);

// Shadow maps (t8-t10)
Texture2D<float>   g_shadow0  : register(t8);
Texture2D<float>   g_shadow1  : register(t9);
Texture2D<float>   g_shadow2  : register(t10);

// SSAO mask (t11) — REMOVED; slot is now sand normal map
// Sand normal map (t11, EXR, OpenGL convention)
Texture2D<float4>  g_texSandNor  : register(t11);

// Sand PBR maps (t12-t14, bound during terrain passes only)
Texture2D<float4>  g_texSandDiff  : register(t12);
Texture2D<float>   g_texSandRough : register(t13);
Texture2D<float>   g_texSandDisp  : register(t14);

// Snow PBR maps (t15-t16, bound during terrain passes only)
Texture2D<float4>  g_texSnowNor   : register(t15);
Texture2D<float>   g_texSnowRough : register(t16);

// Samplers
SamplerState           g_anisoSampler  : register(s0);   // anisotropic wrap
SamplerComparisonState g_shadowSampler : register(s1);   // PCF comparison
SamplerState           g_hmSampler     : register(s2);   // linear clamp
SamplerState           g_pointSampler  : register(s3);   // point clamp (kept for CB compat)

// ---------------------------------------------------------------------------
// Procedural noise functions  (no texture dependencies)
// ---------------------------------------------------------------------------
float Hash21(float2 p)
{
    p = frac(p * float2(127.1, 311.7));
    p += dot(p, p + 19.19);
    return frac(p.x * p.y);
}

float2 Hash22(float2 p)
{
    return float2(Hash21(p), Hash21(p + float2(1.7, 3.1)));
}

float ValueNoise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    f = f*f*(3.0 - 2.0*f);
    return lerp(lerp(Hash21(i),              Hash21(i + float2(1,0)), f.x),
                lerp(Hash21(i + float2(0,1)), Hash21(i + float2(1,1)), f.x), f.y);
}

float FBM(float2 p)
{
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 4; ++i) { v += a * ValueNoise(p); p *= 2.1; a *= 0.5; }
    return v;
}

// Voronoi: returns distance to nearest cell edge (0 = on crack, >0 = interior).
// Used to add angular fracture lines to rock surfaces.
float VoronoiEdge(float2 p)
{
    float2 ip = floor(p);
    float2 fp = frac(p);
    float minD1 = 10.0, minD2 = 10.0;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    [unroll]
    for (int x = -1; x <= 1; ++x)
    {
        float2 cell = float2(x, y);
        float2 pt   = Hash22(ip + cell) + cell - fp;
        float  d    = dot(pt, pt);
        if (d < minD1) { minD2 = minD1; minD1 = d; }
        else if (d < minD2) minD2 = d;
    }
    return sqrt(minD2) - sqrt(minD1);   // 0 on fracture edge
}

// ---------------------------------------------------------------------------
// Triplanar texture sampling
// Blends texture samples from three orthogonal projections weighted by the
// absolute surface normal.  Sharp^4 blending avoids seams on near-vertical
// faces.  tileSize is in world-space metres.
// ---------------------------------------------------------------------------
float3 TriplanarAlbedo(Texture2D<float4> tex, float3 wp, float3 N, float tileSize)
{
    float3 wt = abs(N);
    wt = wt * wt * wt * wt;                         // sharpen blend
    wt /= (wt.x + wt.y + wt.z + 0.001);

    float invT = 1.0 / tileSize;
    float3 cx = tex.Sample(g_anisoSampler, wp.yz * invT).rgb;
    float3 cy = tex.Sample(g_anisoSampler, wp.xz * invT).rgb;
    float3 cz = tex.Sample(g_anisoSampler, wp.xy * invT).rgb;
    return cx * wt.x + cy * wt.y + cz * wt.z;
}

// ---------------------------------------------------------------------------
// Triplanar normal map sampling using Whiteout blending (RTR4 §6.7.2).
// Input maps use OpenGL convention: values in [0,1] range, G = Y-up.
// The Y channel is flipped here to convert GL → DX convention.
// tileSize: world-space tile size (match the albedo tile).
// ---------------------------------------------------------------------------
float3 TriplanarNormal(Texture2D<float4> tex, float3 wp, float3 N, float tileSize)
{
    float3 wt = pow(abs(N), 4.0);
    wt /= dot(wt, float3(1.0, 1.0, 1.0)) + 0.001;
    float inv = 1.0 / tileSize;

    // Sample and unpack [0,1] → [-1,1]; flip Y for GL→DX convention
    float3 tnX = tex.Sample(g_anisoSampler, wp.zy * inv).rgb * 2.0 - 1.0;
    float3 tnY = tex.Sample(g_anisoSampler, wp.xz * inv).rgb * 2.0 - 1.0;
    float3 tnZ = tex.Sample(g_anisoSampler, wp.xy * inv).rgb * 2.0 - 1.0;
    tnX.y = -tnX.y; tnY.y = -tnY.y; tnZ.y = -tnZ.y;

    // Whiteout blend: absorb geometric normal orientation into each sample
    tnX = float3(tnX.xy + N.zy, abs(N.x));
    tnY = float3(tnY.xy + N.xz, abs(N.y));
    tnZ = float3(tnZ.xy + N.xy, abs(N.z));

    // Re-swizzle to world space and blend
    return normalize(tnX.zxy * wt.x + tnY.xzy * wt.y + tnZ.xyz * wt.z);
}

// ---------------------------------------------------------------------------
// PCF shadow (3×3 filter)
// bias: slope-scaled depth bias passed from PSMain to avoid acne on steep faces
// ---------------------------------------------------------------------------
float SampleShadow(Texture2D<float> tex, float4 uvz, float invMapSize, float bias)
{
    if (abs(uvz.x) > 1.0 || abs(uvz.y) > 1.0) return 1.0;   // outside cascade

    float2 uv    = uvz.xy * float2(0.5, -0.5) + 0.5;
    float  depth = uvz.z - bias;
    float  sum   = 0.0;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    [unroll]
    for (int x = -1; x <= 1; ++x)
        sum += tex.SampleCmpLevelZero(g_shadowSampler, uv + float2(x,y)*invMapSize, depth);
    return sum / 9.0;
}

// ---------------------------------------------------------------------------
// RT shadow helpers (used when g_rtEnabled=1 to replace PCF shadow maps)
// ---------------------------------------------------------------------------

// Sample terrain height from the heightmap (PS stage).
float SampleHMPS(float2 worldXZ)
{
    float2 uv = worldXZ / g_terrainSize + 0.5;
    if (any(saturate(uv) != uv)) return -99999.0;
    float raw = g_hmPS.SampleLevel(g_hmSampler, uv, 0).r;
    return (raw - 0.3) * g_heightScale;
}

// Soft shadow ray: march from worldPos toward sunDir.
// Returns 1.0 = fully lit, 0.0 = fully occluded.
// Uses the "min clearance" estimator to produce smooth penumbra.
float RayTracedShadow(float3 worldPos, float3 sunDir, int nSteps)
{
    float maxDist   = 600.0;
    float dt        = maxDist / max(nSteps, 4);
    float t         = max(dt, 3.0);  // start slightly above surface
    float minClear  = 1.0;
    float k         = 8.0;           // penumbra sharpness (larger = harder shadow)

    [loop]
    for (int i = 0; i < nSteps; ++i)
    {
        float3 p      = worldPos + sunDir * t;
        float  terrH  = SampleHMPS(p.xz);
        if (terrH < -9998.0) { t += dt; continue; }  // outside terrain
        float  clear  = p.y - terrH;
        if (clear < 0.0) return 0.0;                  // hard block
        minClear = min(minClear, k * clear / t);
        t += dt;
    }
    return saturate(minClear);
}

// ---------------------------------------------------------------------------
// RT Ambient Occlusion — Fibonacci hemisphere samples, range 25 m.
// Uses the same heightmap (g_hmPS / SampleHMPS) as the RT shadow marcher.
// Returns 1.0 = fully unoccluded, 0.0 = fully occluded.
// ---------------------------------------------------------------------------
float RayTracedAO(float3 worldPos, float3 N)
{
    const int   kRays   = 8;
    const float kDist   = 25.0;
    const int   kSteps  = 8;
    const float dt      = kDist / kSteps;
    const float kGolden = 2.399963;   // golden angle (radians)

    float3 up = abs(N.y) < 0.99 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 T  = normalize(cross(up, N));
    float3 B  = cross(N, T);

    float occ = 0.0;
    [loop]
    for (int i = 0; i < kRays; ++i)
    {
        float  theta = kGolden * i;
        float  r     = sqrt((float)(i + 0.5) / kRays);
        float2 sc;  sincos(theta, sc.x, sc.y);
        float3 dir = normalize(T * sc.x * r + B * sc.y * r + N * sqrt(max(0.0, 1.0 - r * r)));

        float t = dt;
        [loop]
        for (int j = 0; j < kSteps; ++j)
        {
            float3 p = worldPos + dir * t;
            float  h = SampleHMPS(p.xz);
            if (h > -9998.0 && p.y < h) { occ += 1.0; break; }
            t += dt;
        }
    }
    return saturate(1.0 - occ / kRays);
}

// ---------------------------------------------------------------------------
// GGX PBR helpers
// ---------------------------------------------------------------------------
float DistributionGGX(float NdotH, float roughness)
{
    float a  = roughness * roughness;
    float a2 = a*a;
    float d  = NdotH*NdotH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 0.0001);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = r*r / 8.0;
    return NdotV / max(NdotV*(1.0-k)+k, 0.0001);
}

float GeometrySmith(float NdotV, float NdotL, float roughness)
{
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

// ---------------------------------------------------------------------------
// Input/Output
// ---------------------------------------------------------------------------
struct PSIn {
    float4 svPos     : SV_Position;
    float3 worldPos  : POSITION0;
    float2 uv        : TEXCOORD0;
    float3 normal    : NORMAL0;
    float4 shadowUV0 : TEXCOORD1;
    float4 shadowUV1 : TEXCOORD2;
    float4 shadowUV2 : TEXCOORD3;
    float  clipDist  : SV_ClipDistance0;
};

struct PSOut {
    float4 hdrColor : SV_Target0;   // HDR scene
    float4 gbNormal : SV_Target1;   // GBuffer world-space normals (for SSAO)
};

// ---------------------------------------------------------------------------
PSOut PSMain(PSIn input)
{
    float3 N = normalize(input.normal);
    float3 N_geom = N;   // geometric normal — used for slope-scaled shadow bias
    float3 V = normalize(g_camPos - input.worldPos);
    float3 L_sun = normalize(g_dirToSun);   // pre-compute for bias + lighting

    float heightNorm = input.worldPos.y / g_heightScale;
    float slope      = dot(N, float3(0,1,0));   // 1=flat, 0=vertical

    // --- Sample Florinsky morphometric maps --------------------------------
    // t1: (slope_n, aspect_n, kh_n, kv_n)  all in [0,1]
    // t2: (kM_n, TWI_n, exH_n, exV_n)      all in [0,1]
    float4 morph1 = g_morpho1.SampleLevel(g_hmSampler, input.uv, 0);
    float4 morph2 = g_morpho2.SampleLevel(g_hmSampler, input.uv, 0);

    float slopeNorm = morph1.x;              // 0=flat, 1=steepest
    // kh_n < 0.5 → convergent (hollow), >0.5 → divergent (ridge)
    float khSigned  = morph1.z * 2.0 - 1.0; // [-1, +1]
    float TWI       = morph2.y;              // 0=driest, 1=wettest

    // --- Biome weights: height-primary zones + Florinsky refinement -----------
    // Height zones guarantee all textures appear at the expected altitudes.
    // Florinsky morphometry then modulates boundaries and adds natural variety.

    float kMSigned   = morph2.x * 2.0 - 1.0;          // positive = ridge/dome
    float convergence = saturate(0.5 - khSigned * 0.8); // 1=convergent hollow

    // --- Height zones (always produce visible coverage) --------------------
    // Sand: anchor to sea level so the sandy shore always hugs the waterline.
    // g_seaLevel is world-Y; normalise to match heightNorm.
    float seaLevelNorm = g_seaLevel / g_heightScale;
    float zSand = smoothstep(seaLevelNorm + 0.10, seaLevelNorm - 0.01, heightNorm);

    // Snow cap: above rock/snow threshold — direct height mapping
    float wSnow = smoothstep(g_thresholds.z - 0.06, g_thresholds.z + 0.14, heightNorm);

    // --- Rock: steep slope faces + altitude band between grass and snow ------
    // Height-based rock: fills the mid-altitude gap so rock always separates
    // grass from snow (uses thresholds.y = grass/rock edge, .z = rock/snow edge).
    float wRockH = smoothstep(g_thresholds.y, g_thresholds.y + 0.08, heightNorm)
                 * smoothstep(g_thresholds.z + 0.03, g_thresholds.z - 0.04, heightNorm);
    float wRock = smoothstep(0.30, 0.58, slopeNorm)
                + smoothstep(0.0, 0.40, kMSigned) * smoothstep(0.40, 0.20, slopeNorm) * 0.50
                + wRockH * 0.90;
    wRock = saturate(wRock) * (1.0 - wSnow);

    // --- Mud: wet convergent hollows (TWI-driven, replaces grass in valleys) -
    float wetScore = saturate(TWI * 2.2 - 0.5);
    float wMud = saturate(wetScore * convergence * smoothstep(0.18, 0.04, slopeNorm) * 2.0)
               * (1.0 - wRock) * (1.0 - wSnow);

    // --- Sand: coastal zone anchored to sea level ---------------------------
    float wSand = saturate(zSand)
                * (1.0 - wRock) * (1.0 - wMud) * (1.0 - wSnow);

    // --- Dirt: dry exposed areas — large-scale FBM gives coherent patches ----
    // Previously used plan-curvature (divergence) which flips sign every ~12 m
    // causing tiny patchy blocks.  Low-frequency FBM (~300 m scale) gives
    // natural broad zones of exposed, slightly drier terrain.
    float dryScore  = saturate(1.0 - TWI * 2.0);
    float dirtNoise = FBM(input.worldPos.xz * 0.003);   // ~300 m coherence
    float flDirt    = dryScore * dirtNoise * smoothstep(0.18, 0.03, slopeNorm) * 1.5;
    float wDirt = saturate(flDirt) * (1.0 - wSand)
                * (1.0 - wRock) * (1.0 - wMud) * (1.0 - wSnow);

    // --- Grass: remainder ---------------------------------------------------
    float wGrass = saturate(1.0 - wSnow - wRock - wMud - wDirt - wSand);

    // Renormalise to ensure weights sum to 1
    float wTotal = wSnow + wRock + wMud + wDirt + wSand + wGrass + 0.001;
    wSnow  /= wTotal;
    wRock  /= wTotal;
    wMud   /= wTotal;
    wDirt  /= wTotal;
    wSand  /= wTotal;
    wGrass /= wTotal;

    // --- Triplanar albedo sampling -----------------------------------------
    float3 colGrass    = TriplanarAlbedo(g_texGrass,    input.worldPos, N, 12.0);
    float3 colDirt     = TriplanarAlbedo(g_texDirt,     input.worldPos, N, 8.0);
    float3 colMud      = TriplanarAlbedo(g_texMud,      input.worldPos, N, 6.0);
    float3 colRock     = TriplanarAlbedo(g_texRock,     input.worldPos, N, 16.0);
    float3 colSnow     = TriplanarAlbedo(g_texSnow,     input.worldPos, N, 20.0);
    float3 colSandDiff = TriplanarAlbedo(g_texSandDiff, input.worldPos, N, 7.0);
    float  sandRough   = g_texSandRough.Sample(g_anisoSampler, input.worldPos.xz / 7.0).r;
    float  snowRough   = g_texSnowRough.Sample(g_anisoSampler, input.worldPos.xz / 20.0).r;

    // Voronoi rock fracture darkening baked into rock albedo
    float  rockCrack = VoronoiEdge(input.worldPos.xz * 0.2);
    float  crackDark = saturate(1.0 - rockCrack * 4.5);
    colRock *= (1.0 - crackDark * 0.35);

    float3 albedo;
    float  roughness;
    {
        albedo    = colGrass     * wGrass
                  + colSandDiff  * wSand
                  + colDirt      * wDirt
                  + colMud       * wMud
                  + colRock      * wRock
                  + colSnow      * wSnow;

        // Biome-driven roughness (sand = loose grit, snow from PBR map, mud = very rough)
        roughness = 0.86*wGrass + sandRough*wSand + 0.91*wDirt + 0.96*wMud + 0.93*wRock + snowRough*wSnow;
    }

    // --- Multi-scale normal perturbation ---------------------------------
    // Coarse FBM breaks up smooth rounded mounds; fine noise / Voronoi
    // adds micro-surface detail on rock faces.
    {
        // Coarse FBM (large-scale surface tilt break-up)
        float2 mUV = input.worldPos.xz * 0.08;
        float  mC  = FBM(mUV);
        float  mDx = FBM(mUV + float2(0.10, 0.0)) - mC;
        float  mDz = FBM(mUV + float2(0.0,  0.10)) - mC;
        float  coarseStr = 0.28 * (1.0 - wSnow * 0.9);

        // Fine value-noise (micro-surface grit, boosted on rock)
        float2 fUV = input.worldPos.xz * 0.55;
        float  fC  = ValueNoise(fUV);
        float  fDx = ValueNoise(fUV + float2(0.5, 0.0)) - fC;
        float  fDz = ValueNoise(fUV + float2(0.0, 0.5)) - fC;
        float  fineStr = (0.16 + wRock * 0.42 * (1.0 - slope)) * (1.0 - wSnow * 0.9);

        // Voronoi rock fractures
        float  rcDx = VoronoiEdge((input.worldPos.xz + float2(0.3, 0.0)) * 0.2) - rockCrack;
        float  rcDz = VoronoiEdge((input.worldPos.xz + float2(0.0, 0.3)) * 0.2) - rockCrack;
        float  crackStr = wRock * 0.60 * (1.0 - slope);

        N = normalize(N + float3(
            -(mDx * coarseStr + fDx * fineStr + rcDx * crackStr),
            0.0,
            -(mDz * coarseStr + fDz * fineStr + rcDz * crackStr)));

        // Sand PBR normal map (RNM blend, RTR4 §6.7.2) — replaces displacement bump
        if (wSand > 0.005)
        {
            float3 sandN = TriplanarNormal(g_texSandNor, input.worldPos, N, 7.0);
            N = normalize(lerp(N, sandN, wSand * 0.90));
        }

        // Snow PBR normal map (RNM blend)
        if (wSnow > 0.005)
        {
            float3 snowN = TriplanarNormal(g_texSnowNor, input.worldPos, N, 20.0);
            N = normalize(lerp(N, snowN, wSnow * 0.80));
        }
    }

    // --- Shadow --- choose RT ray-march or classic PCF cascade --------
    float shadow;
    if (g_rtEnabled)
    {
        // Ray-trace a soft shadow toward the sun.
        // Offset start point along the geometric normal to avoid self-intersection.
        int shadowSteps = max(g_rtShadowSteps, 8);
        shadow = RayTracedShadow(input.worldPos + N_geom * 1.5, L_sun, shadowSteps);
    }
    else
    {
        // Slope-scaled bias: steeper surfaces (NdotL near 0) need a larger offset
        // to avoid self-shadowing (acne). This was the main cause of random dark spots.
        float  NdotL_geom  = max(dot(N_geom, L_sun), 0.0);
        float  shadowBias  = 0.0015 + 0.0060 * (1.0 - NdotL_geom * NdotL_geom);

        float3 eyeVec   = input.worldPos - g_camPos;
        float  eyeDepth = dot(eyeVec, g_camDir);
        float  invShadowSz = 1.0 / 2048.0;

        // Blend cascades over a narrow depth range to hide the seam
        float  blendHalf = 3.0;   // blend zone half-width in view-space metres
        if (eyeDepth < g_cascadeSplits.x - blendHalf)
            shadow = SampleShadow(g_shadow0, input.shadowUV0, invShadowSz, shadowBias);
        else if (eyeDepth < g_cascadeSplits.x + blendHalf) {
            float t = saturate((eyeDepth - g_cascadeSplits.x + blendHalf) / (2.0 * blendHalf));
            shadow = lerp(SampleShadow(g_shadow0, input.shadowUV0, invShadowSz, shadowBias),
                          SampleShadow(g_shadow1, input.shadowUV1, invShadowSz, shadowBias), t);
        } else if (eyeDepth < g_cascadeSplits.y - blendHalf)
            shadow = SampleShadow(g_shadow1, input.shadowUV1, invShadowSz, shadowBias);
        else if (eyeDepth < g_cascadeSplits.y + blendHalf) {
            float t = saturate((eyeDepth - g_cascadeSplits.y + blendHalf) / (2.0 * blendHalf));
            shadow = lerp(SampleShadow(g_shadow1, input.shadowUV1, invShadowSz, shadowBias),
                          SampleShadow(g_shadow2, input.shadowUV2, invShadowSz, shadowBias), t);
        } else
            shadow = SampleShadow(g_shadow2, input.shadowUV2, invShadowSz, shadowBias);
    }

    // --- Ambient Occlusion (RTAO when RT is enabled; else no AO) -----------
    float ao = 1.0;
    if (g_rtEnabled)
        ao = RayTracedAO(input.worldPos + N_geom * 0.5, N);

    // --- PBR Lighting ----------------------------------------------------
    float3 L    = L_sun;
    float3 H    = normalize(L + V);
    float  NdotL = max(dot(N, L), 0.0);
    float  NdotV = max(dot(N, V), 0.0001);
    float  NdotH = max(dot(N, H), 0.0);

    // Cook-Torrance BRDF (dielectric F0 = 0.04 for terrain)
    float3 F0  = float3(0.04, 0.04, 0.04);
    float3 F   = FresnelSchlick(max(dot(H, V), 0.0), F0);
    float  D   = DistributionGGX(NdotH, roughness);
    float  G   = GeometrySmith(NdotV, NdotL, roughness);
    float3 spec = (D * G * F) / max(4.0 * NdotV * NdotL, 0.0001);
    float3 kD   = (1.0 - F) * (1.0 - 0.0);   // metalness = 0

    float3 radiance = g_sunColor * g_sunIntensity;
    float3 direct   = (kD * albedo / PI + spec) * radiance * NdotL * shadow;

    // Ambient (hemisphere light + SSAO)
    float3 ambient  = g_ambientColor * albedo * g_ambientIntensity * ao;

    float3 color = direct + ambient;

    // Fog
    color = ApplyFog(color, input.worldPos);

    PSOut o;
    o.hdrColor = float4(color, 1.0);
    o.gbNormal = float4(N * 0.5 + 0.5, 0.0);
    return o;
}
