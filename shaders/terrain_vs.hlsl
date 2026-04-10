// terrain_vs.hlsl
// Pass-through vertex shader for tessellated terrain patches.
// The actual vertex placement happens in terrain_ds.hlsl.

#include "Common.hlsli"

struct VSIn {
    float2 posXZ : POSITION;    // world-space (X, Z) — Y is 0 until DS stage
    float2 uv    : TEXCOORD0;   // normalised 0..1 across the whole terrain
};

struct VSOut {
    float2 posXZ : POSITION;
    float2 uv    : TEXCOORD0;
};

VSOut VSMain(VSIn i)
{
    VSOut o;
    o.posXZ = i.posXZ;
    o.uv    = i.uv;
    return o;
}
