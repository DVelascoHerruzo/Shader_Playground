// ssgi_ps.hlsl
// Screen-Space Global Illumination — one-bounce diffuse indirect lighting.
// Reads the current HDR scene and GBuffer normals to estimate indirect light
// arriving at each pixel from nearby surfaces.  Runs at half-resolution.
//
// Inputs:
//   t0 : HDR scene  (R16G16B16A16_FLOAT)
//   t1 : GBuffer normals (R16G16B16A16_FLOAT, encoded [0,1] → [-1,+1])
//
// Outputs a half-res SSGI contribution that is additively composited in tonemap.

#include "Common.hlsli"

Texture2D<float4> g_scene   : register(t0);
Texture2D<float4> g_normals : register(t1);

SamplerState g_linearSampler : register(s0);

struct PSIn {
    float4 svPos : SV_Position;
    float2 uv    : TEXCOORD0;
};

// Pre-computed cosine-weighted hemisphere kernel (16 samples, unit vectors)
static const float3 k_kernel[16] = {
    float3( 0.215f,  0.215f,  0.953f),
    float3(-0.546f,  0.121f,  0.829f),
    float3( 0.688f,  0.492f,  0.534f),
    float3(-0.213f,  0.960f,  0.185f),
    float3( 0.534f,  0.748f,  0.393f),
    float3(-0.742f,  0.534f,  0.404f),
    float3( 0.108f,  0.868f,  0.486f),
    float3(-0.456f,  0.847f,  0.274f),
    float3( 0.861f,  0.322f,  0.395f),
    float3(-0.637f,  0.454f,  0.623f),
    float3( 0.336f,  0.675f,  0.656f),
    float3(-0.109f,  0.540f,  0.834f),
    float3( 0.465f,  0.233f,  0.854f),
    float3(-0.331f,  0.234f,  0.915f),
    float3( 0.077f,  0.436f,  0.897f),
    float3(-0.503f,  0.131f,  0.854f),
};

// Build TBN from a normal (simple arbitrary-tangent approach)
float3x3 TBN(float3 N)
{
    float3 up  = abs(N.y) < 0.999f ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 T   = normalize(cross(up, N));
    float3 B   = cross(N, T);
    return float3x3(T, B, N);
}

float4 PSMain(PSIn input) : SV_Target0
{
    // Decode surface normal
    float4 nSample = g_normals.SampleLevel(g_linearSampler, input.uv, 0);
    float3 N = normalize(nSample.xyz * 2.0f - 1.0f);
    if (dot(N, N) < 0.1f) return float4(0, 0, 0, 0);   // sky/empty pixel

    // Noise-based rotation from pixel position — breaks up ring artefacts
    float  hash = frac(sin(dot(input.svPos.xy, float2(127.1f, 311.7f))) * 43758.5453f);
    float  angle = hash * 6.2832f;
    float2 rot   = float2(cos(angle), sin(angle));

    float3x3 tbn = TBN(N);

    // Sampling radius in UV space (~2 % of screen half-width)
    float2 uvRadius = float2(0.035f, 0.035f * (g_screenW / g_screenH));

    float3 indirect = float3(0, 0, 0);
    float  sum      = 0.001f;

    [unroll]
    for (int i = 0; i < 16; ++i)
    {
        // Rotate kernel sample in world space
        float3 worldSample = mul(k_kernel[i], tbn);

        // Only sample where the kernel faces the surface (cosine term)
        float cosW = max(dot(worldSample, N), 0.0f);

        // Project sample to screen UV — use sample direction in XZ plane for 2D offset
        float2 sampleOffset = float2(dot(worldSample, float3(1,0,0)),
                                     dot(worldSample, float3(0,0,1)));
        // Tilt rotation by hash
        sampleOffset = float2(sampleOffset.x * rot.x - sampleOffset.y * rot.y,
                              sampleOffset.x * rot.y + sampleOffset.y * rot.x);
        float2 sampleUV = saturate(input.uv + sampleOffset * uvRadius);

        // Fetch neighbour radiance
        float3 neighbourColor = g_scene.SampleLevel(g_linearSampler, sampleUV, 1).rgb;

        // Neighbour facing check — reject samples facing away from us
        float4 nN = g_normals.SampleLevel(g_linearSampler, sampleUV, 0);
        float3 nNorm = normalize(nN.xyz * 2.0f - 1.0f);
        float  nCosW = max(-dot(nNorm, worldSample), 0.0f);

        indirect += neighbourColor * cosW * nCosW;
        sum      += cosW;
    }

    indirect /= sum;

    // Clamp to avoid blow-out from bright direct-lit faces
    indirect = min(indirect, float3(4.0f, 4.0f, 4.0f));

    return float4(indirect, 1.0f);
}
