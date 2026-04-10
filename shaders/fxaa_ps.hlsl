// fxaa_ps.hlsl
// Fast Approximate Anti-Aliasing.
// Reads the tonemapped LDR scene, detects high-contrast edges, and
// samples along them to sub-pixel precision.

#include "Common.hlsli"

Texture2D    g_sceneTex : register(t0);
SamplerState g_sampler  : register(s2);   // linearClamp

struct PSIn {
    float4 svPos : SV_Position;
    float2 uv    : TEXCOORD0;
};

// Rec.709 luma weights
float Luma(float3 c) { return dot(c, float3(0.299, 0.587, 0.114)); }

float4 PSMain(PSIn input) : SV_Target0
{
    float2 texel = 1.0 / float2(g_screenW, g_screenH);
    float2 uv    = input.uv;

    // Sample 5-tap neighbourhood
    float3 cM  = g_sceneTex.Sample(g_sampler, uv).rgb;
    float3 cN  = g_sceneTex.Sample(g_sampler, uv + float2( 0,-1)*texel).rgb;
    float3 cS  = g_sceneTex.Sample(g_sampler, uv + float2( 0, 1)*texel).rgb;
    float3 cE  = g_sceneTex.Sample(g_sampler, uv + float2( 1, 0)*texel).rgb;
    float3 cW  = g_sceneTex.Sample(g_sampler, uv + float2(-1, 0)*texel).rgb;

    float lumaM = Luma(cM);
    float lumaN = Luma(cN);
    float lumaS = Luma(cS);
    float lumaE = Luma(cE);
    float lumaW = Luma(cW);

    float lumaMin = min(lumaM, min(min(lumaN, lumaS), min(lumaE, lumaW)));
    float lumaMax = max(lumaM, max(max(lumaN, lumaS), max(lumaE, lumaW)));
    float contrast = lumaMax - lumaMin;

    // Below threshold — no AA needed
    static const float THRESHOLD_MIN = 0.0312;
    static const float THRESHOLD     = 0.125;
    if (contrast < max(THRESHOLD_MIN, lumaMax * THRESHOLD))
        return float4(cM, 1.0);

    // Diagonal samples for direction
    float3 cNW = g_sceneTex.Sample(g_sampler, uv + float2(-1,-1)*texel).rgb;
    float3 cNE = g_sceneTex.Sample(g_sampler, uv + float2( 1,-1)*texel).rgb;
    float3 cSW = g_sceneTex.Sample(g_sampler, uv + float2(-1, 1)*texel).rgb;
    float3 cSE = g_sceneTex.Sample(g_sampler, uv + float2( 1, 1)*texel).rgb;

    float lumaNW = Luma(cNW), lumaNE = Luma(cNE);
    float lumaSW = Luma(cSW), lumaSE = Luma(cSE);

    // Determine edge direction
    float edgeH = abs(lumaNW + 2*lumaN + lumaNE - lumaSW - 2*lumaS - lumaSE);
    float edgeV = abs(lumaNW + 2*lumaW + lumaSW - lumaNE - 2*lumaE - lumaSE);
    bool  isHoriz = edgeH >= edgeV;

    // Sub-pixel blending weight
    float lumaAvg   = (lumaN + lumaS + lumaE + lumaW +
                       lumaNW + lumaNE + lumaSW + lumaSE) / 8.0;
    float subPixel  = abs(lumaAvg - lumaM) / contrast;
    float blendFact = smoothstep(0.0, 1.0, subPixel) * 0.75;

    // Step perpendicular to edge
    float2 step = isHoriz ? float2(0, texel.y) : float2(texel.x, 0);
    float2 uvBlend = uv + step * (sign(isHoriz ? (lumaN - lumaS) : (lumaE - lumaW)) * 0.5);

    float3 blended = g_sceneTex.Sample(g_sampler, uvBlend).rgb;
    return float4(lerp(cM, blended, blendFact), 1.0);
}
