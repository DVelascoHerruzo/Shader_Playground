// grass_ps.hlsl
// Grass blade pixel shader – uses real billboard textures from GarrettGunnell/Grass.
//
// Textures bound by Foliage::Draw():
//   t0  g_grassAlbedo  – base_grass5.png   (sRGB, R+G+B albedo, A = alpha cutout)
//   t1  g_grassNormal  – base_grass5n.png  (linear, tangent-space normal map)
//
// Reference techniques carried over from GarrettGunnell/Grass (Unity shaders):
//   - Alpha cutout from texture A channel replaces procedural cubic taper
//   - Base AO:  lerp(AOColor, 1, uv.y^0.5)     – moist/dark root, reference _AOColor
//   - Tip highlight: additive lerp(0, TipColor, uv.y^2*(1+scale)) – reference _TipColor
//   - Per-instance saturation tweak (reference: col.r /= sat at root)
//   - Lighting: NdotL blended 55% toward fixed world-up for stable shadow-free grass
//   - TBN built analytically from face normal so tangent-space normals work on rotated blades
//   - Two-sided: back-face normals flipped consistently in both TBN and lighting

#include "Common.hlsli"

// ---------------------------------------------------------------------------
// Grass billboard textures (bound by Foliage::Draw at t0, t1)
Texture2D<float4> g_grassAlbedo : register(t0);   // sRGB albedo + alpha cut-out
Texture2D<float4> g_grassNormal : register(t1);   // linear tangent-space normals
SamplerState      g_wrapSampler : register(s0);   // aniso wrap (global s0)

// ---------------------------------------------------------------------------
struct PSIn {
    float4 svPos    : SV_Position;
    float3 worldPos : POSITION0;
    float2 uv       : TEXCOORD0;
    float3 normal   : NORMAL0;     // world-space face normal (already rotated in VS)
    float  tint     : TEXCOORD1;
    float  alpha    : TEXCOORD2;   // distance fade [0,1]
    float2 instXZ   : TEXCOORD3;
    bool   isFront  : SV_IsFrontFace;
};

struct PSOut {
    float4 color  : SV_Target0;   // HDR scene colour
    float4 normal : SV_Target1;   // world-space normal for SSAO (encoded 0..1)
};

// ---------------------------------------------------------------------------
PSOut PSMain(PSIn i)
{
    // ---- Distance fade cutoff -----------------------------------------------
    clip(i.alpha - 0.01);

    // ---- Albedo + alpha cutout (reference: BillboardGrass _MainTex + clip) ----
    float4 albedoTex = g_grassAlbedo.Sample(g_wrapSampler, i.uv);
    // Alpha cutout: removes sky/background from the billboard sprite.
    // Reference uses clip(-(0.5 - col.a)) which is clip(col.a - 0.5).
    clip(albedoTex.a - 0.50);

    // ---- Two-sided geometry -------------------------------------------------
    float3 faceN = normalize(i.isFront ? i.normal : -i.normal);

    // ---- Analytical TBN construction ----------------------------------------
    // Our blades stand vertically, so world-up (0,1,0) is the blade-height direction
    // = bitangent B, and the tangent T runs across the blade width.
    //
    //  For Quad 0  (normal = rotated +Z):  T = cross(+Y, N) = rotated +X   ✓
    //  For Quad 1  (normal = rotated +X):  T = cross(+Y, N) = rotated +Z   ✓
    //  (sign is consistent for both quads relative to the UV layout)
    //
    float3 B = float3(0.0, 1.0, 0.0);          // bitangent = world up = blade height
    float3 T = normalize(cross(B, faceN));      // tangent = blade width direction

    // Back-face: mirror the tangent so normal-map shading stays consistent
    if (!i.isFront) T = -T;

    // Sample tangent-space normal map (reference: base_grass5n.png)
    float3 tsN = g_grassNormal.Sample(g_wrapSampler, i.uv).xyz * 2.0 - 1.0;
    // Reconstruct world-space shading normal
    float3 worldN = normalize(T * tsN.x + B * tsN.y + faceN * tsN.z);

    // ---- Ambient Occlusion at base ------------------------------------------
    // Reference: lerp(_AOColor, 1, uv.y)  –  root darkened, brightens toward tip.
    // Raised to 0.5 power for a slightly softer gradient (vs reference's linear).
    static const float3 AO_COLOR = float3(0.04, 0.10, 0.02);
    float3 ao = lerp(AO_COLOR, (float3)1.0, pow(saturate(i.uv.y), 0.5));

    // ---- Albedo from texture ------------------------------------------------
    // Reference ModelGrass: col = lerp(albedo1, albedo2, uv.y)
    // We composite the sampled texture over the AO and per-instance tint.
    float3 albedo = albedoTex.rgb;
    albedo *= ao;

    // Per-instance colour variation (reference: col.r /= saturation at base).
    // tint is in [-1,1]; map to a saturation-shift multiplier.
    float satVar = 0.82 + 0.28 * i.tint;
    albedo *= satVar;

    // ---- Additive tip highlight (reference _TipColor) -----------------------
    // Subtle pale-yellow shimmer at the very tip only.
    static const float3 TIP_COLOR = float3(0.55, 0.68, 0.28);
    float tipBlend = i.uv.y * i.uv.y * i.uv.y;   // cubic – concentrated at tip
    albedo += TIP_COLOR * tipBlend * 0.10;

    // ---- Lighting -----------------------------------------------------------
    // NdotL with world-up blend for stability (reference: ndotl against float3(0,1,0)).
    float3 L        = normalize(g_dirToSun);
    float  ndotlN   = dot(worldN, L);                      // from normal map
    float  ndotlUp  = saturate(dot(float3(0.0, 1.0, 0.0), L)); // fixed world-up (reference)
    // 55% weight toward world-up prevents flickering as blade normals rotate with wind.
    float  ndotl    = lerp(ndotlN, ndotlUp, 0.55);
    // Wrap-around diffuse: models thin-leaf subsurface (reference: ndotl is DotClamped).
    // Adding 0.45 offset so the underside is never fully black.
    float  wrap     = max(ndotl * 0.55 + 0.45, 0.0);

    float3 radiance = g_sunColor * g_sunIntensity;
    float3 direct   = albedo * wrap * radiance * 0.65;
    float3 ambient  = g_ambientColor * albedo * g_ambientIntensity * 1.1;

    float3 color = direct + ambient;

    // ---- Fog ----------------------------------------------------------------
    color = ApplyFog(color, i.worldPos);

    // ---- Output -------------------------------------------------------------
    PSOut o;
    // Alpha: texture cutout (already clipped) × distance fade
    o.color  = float4(color, saturate(i.alpha));

    // Encode world-space shading normal for SSAO
    o.normal = float4(worldN * 0.5 + 0.5, 0.0);
    return o;
}
