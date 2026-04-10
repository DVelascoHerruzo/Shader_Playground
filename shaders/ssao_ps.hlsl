// ssao_ps.hlsl
// Screen-Space Ambient Occlusion.
// Input:  scene depth (R32_FLOAT), GBuffer world normals (RGBA)
// Output: raw occlusion value [0..1] into a half-res R8_UNORM RT.

#include "Common.hlsli"

Texture2D<float>  g_depthTex  : register(t0);
Texture2D<float4> g_normalTex : register(t1);
Texture2D<float4> g_noiseTex  : register(t2);   // 4×4 RGBA random rotation vectors

SamplerState g_pointSampler  : register(s3);   // pointClamp
SamplerState g_linearSampler : register(s2);   // linearClamp (for normals)

struct PSIn {
    float4 svPos : SV_Position;
    float2 uv    : TEXCOORD0;
};

float PSMain(PSIn input) : SV_Target0
{
    float2 uv = input.uv;

    // Reconstruct view-space position for this fragment
    float  depth    = g_depthTex.SampleLevel(g_pointSampler, uv, 0).r;
    if (depth >= 1.0) return 1.0;   // skybox — no occlusion

    float3 viewPos  = ReconstructViewPos(uv, depth);

    // Get world-space normal → convert to view-space
    float3 worldNorm = g_normalTex.SampleLevel(g_linearSampler, uv, 0).xyz * 2.0 - 1.0;
    // mul(float4(n,0), g_view) transforms world→view (because matrices are stored transposed)
    float3 viewNorm  = normalize(mul(float4(worldNorm, 0.0), g_view).xyz);

    // ---- Build TBN from random noise rotation ----
    // Scale the noise UV to repeat every 4px
    float2 noiseUV = input.svPos.xy / 4.0;
    float3 rndVec  = g_noiseTex.SampleLevel(g_pointSampler, noiseUV, 0).xyz * 2.0 - 1.0;
    rndVec = normalize(float3(rndVec.xy, 0));   // rotation in XY plane

    float3 tangent   = normalize(rndVec - viewNorm * dot(rndVec, viewNorm));
    float3 bitangent = cross(viewNorm, tangent);
    float3x3 TBN     = float3x3(tangent, bitangent, viewNorm);

    // ---- Accumulate occlusion ----
    float occlusion = 0.0;
    [loop]
    for (int i = 0; i < 64; ++i)
    {
        // Sample direction in view-space hemisphere
        float3 sampleDir = mul(g_ssaoSamples[i].xyz, TBN);
        float3 samplePos = viewPos + sampleDir * g_ssaoRadius;

        // Project sample position to texture UVs
        float4 projected = mul(float4(samplePos, 1.0), g_proj);
        projected.xyz   /= projected.w;
        float2 sampleUV  = projected.xy * float2(0.5, -0.5) + 0.5;

        // Sample depth at projected position
        float sampleDepth = g_depthTex.SampleLevel(g_pointSampler, sampleUV, 0).r;
        float3 sampleVP   = ReconstructViewPos(sampleUV, sampleDepth);

        // Range-check — avoids incorrect occlusion from far-away surfaces
        float rangeCheck = smoothstep(0.0, 1.0, g_ssaoRadius / max(abs(viewPos.z - sampleVP.z), 0.0001));

        // Occlude if the sample position is behind scene geometry
        float occBit = (sampleVP.z <= samplePos.z - g_ssaoBias) ? 1.0 : 0.0;
        occlusion += occBit * rangeCheck;
    }

    occlusion = 1.0 - (occlusion / 64.0) * g_ssaoIntensity;
    return saturate(occlusion);
}
