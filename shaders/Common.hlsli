// Common.hlsli  —  shared constant buffer declarations for all shaders.
// Must match the C++ struct layouts in Types.h exactly.

// ---------------------------------------------------------------------------
//  Constant Buffers
// ---------------------------------------------------------------------------
cbuffer CameraData : register(b0)
{
    float4x4 g_view;
    float4x4 g_proj;
    float4x4 g_viewProj;
    float4x4 g_invProj;
    float4x4 g_invViewProj;
    float3   g_camPos;      float g_nearPlane;
    float3   g_camDir;      float g_farPlane;
    float    g_screenW;
    float    g_screenH;
    float2   g_camPad;
};

cbuffer SunLightData : register(b1)
{
    float3   g_dirToSun;       float g_sunIntensity;
    float3   g_sunColor;       float g_ambientIntensity;
    float3   g_ambientColor;   float g_fogDensity;
    float3   g_fogColor;       float g_timeOfDay;
    float3   g_zenithColor;    float g_sunPad0;
    float3   g_horizonColor;   float g_sunPad1;
};

cbuffer ShadowData : register(b2)
{
    float4x4 g_lightViewProj[3];
    float4   g_cascadeSplits;     // xyz = 3 split depths (view-space)
    int      g_cascadeIndex;
    float3   g_shadowPad;
};

cbuffer TerrainData : register(b3)
{
    float    g_heightScale;
    float    g_terrainSize;
    int      g_gridSize;
    float    g_uvTile;
    float4   g_thresholds;        // x=sand/grass, y=grass/rock, z=rock/snow, w=snowLine
    float    g_slopeThreshold;
    float3   g_terrainPad;
};

cbuffer WaterData : register(b4)
{
    float g_seaLevel;
    float g_waveSpeed;
    float g_waveStrength;
    float g_foamThreshold;
    float g_time;
    float g_ior;
    float g_reflectBias;
    float g_waterPad;
};

cbuffer SSAOData : register(b5)
{
    float4 g_ssaoSamples[64];
    float  g_ssaoRadius;
    float  g_ssaoBias;
    float  g_ssaoIntensity;
    float  g_ssaoPad;
};

cbuffer PostProcessData : register(b6)
{
    float g_exposure;
    int   g_ppSsaoEnabled;
    int   g_fxaaEnabled;
    int   g_ppUnderwater;         // 1 when camera is below sea level
    float g_bloomThreshold;       // luminance threshold for bloom
    float g_bloomIntensity;       // bloom blend strength
    float g_godRayIntensity;      // light shaft intensity
    float g_lensFlareIntensity;   // lens flare intensity
    float g_ssgiIntensity;        // indirect GI contribution
    float3 g_ppPad;
};

cbuffer ClipPlaneData : register(b7)
{
    float4 g_clipPlane;   // ax + by + cz + d  (positive = inside)
};

cbuffer FoliageData : register(b8)
{
    float g_windTime;
    float g_windStrength;
    float g_windFrequency;
    float g_foliageDrawDist;      // grass fade-out distance
    float g_rotationOffset;       // extra Y-rotation for this draw pass (radians)
    float3 g_foliagePad;
};

cbuffer RayTraceData : register(b9)
{
    int   g_rtEnabled;
    int   g_rtReflect;
    int   g_rtRefract;
    int   g_rtSteps;
    float g_rtMaxDist;
    int   g_rtShadowSteps;  // steps for terrain shadow ray march
    float g_rtPad1;
    float g_rtPad2;
};

// ---------------------------------------------------------------------------
//  Utility
// ---------------------------------------------------------------------------
static const float PI = 3.14159265358979f;

// Linearise reversed or forward depth to view-space depth
float LinearDepth(float depth)
{
    return (g_nearPlane * g_farPlane) / (g_farPlane - depth * (g_farPlane - g_nearPlane));
}

// Reconstruct view-space position from screen UV + raw depth
float3 ReconstructViewPos(float2 uv, float depth)
{
    float ndcX =  uv.x * 2.0 - 1.0;
    float ndcY = -uv.y * 2.0 + 1.0;   // Y flipped
    float4 vp  = mul(float4(ndcX, ndcY, depth, 1.0), g_invProj);
    return vp.xyz / vp.w;
}

// Reconstruct world-space position from screen UV + raw depth
float3 ReconstructWorldPos(float2 uv, float depth)
{
    float ndcX =  uv.x * 2.0 - 1.0;
    float ndcY = -uv.y * 2.0 + 1.0;
    float4 wp  = mul(float4(ndcX, ndcY, depth, 1.0), g_invViewProj);
    return wp.xyz / wp.w;
}

// Apply atmospheric-scattering fog.
// Blends toward a sky-tinted inscatter colour that shifts with sun elevation,
// exactly like aerial perspective: distant objects take on the horizon hue.
float3 ApplyFog(float3 color, float3 worldPos)
{
    float3 viewVec  = worldPos - g_camPos;
    float  dist     = length(viewVec);
    float3 viewDir  = viewVec / max(dist, 0.001);

    // Exponential density (same as before)
    float f = exp(-dist * g_fogDensity);

    // Inscatter color: blend g_fogColor with horizon sky at low angles,
    // and tint toward the sun direction for forward-scatter sun glare.
    float  sinElev     = g_dirToSun.y;
    float  dayFade     = saturate(sinElev * 4.0 + 0.4);
    float  cosAngle    = dot(viewDir, normalize(g_dirToSun));
    float  sunGlow     = saturate(pow(max(cosAngle, 0.0), 6.0)) * 0.35 * dayFade;
    float3 sunGlowCol  = lerp(float3(1.0, 0.5, 0.15), g_sunColor, saturate(sinElev * 4.0));

    // Horizon tint: stronger near the horizon (low viewDir.y)
    float  horizT      = pow(saturate(1.0 - abs(viewDir.y) * 2.0), 2.0);
    float3 inscatter   = lerp(g_fogColor, g_horizonColor * dayFade * 0.85, horizT * 0.65);
    inscatter         += sunGlowCol * sunGlow;

    return lerp(inscatter, color, saturate(f));
}

// ACES filmic tonemap
float3 ACESFilmic(float3 x)
{
    float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return saturate((x*(a*x+b))/(x*(c*x+d)+e));
}
