// terrain_hs.hlsl
// Adaptive hull shader for terrain tessellation (quad patches).
// Tessellation factor is driven by camera distance to the edge midpoint.

#include "Common.hlsli"

struct HSIn {
    float2 posXZ : POSITION;
    float2 uv    : TEXCOORD0;
};

struct HSOut {
    float2 posXZ : POSITION;
    float2 uv    : TEXCOORD0;
};

struct PatchTess {
    float edgeTess[4]   : SV_TessFactor;
    float insideTess[2] : SV_InsideTessFactor;
};

// ---------------------------------------------------------------------------
// Compute tessellation factor from world-space XZ distance to camera
// ---------------------------------------------------------------------------
float TessFromDist(float2 midXZ)
{
    float d = distance(g_camPos.xz, midXZ);
    // Exponential: 32 near camera, falls to 1 beyond ~800 units
    return clamp(32.0 * exp(-0.004 * d), 1.0, 32.0);
}

// ---------------------------------------------------------------------------
// Patch-constant function (runs once per patch, not per control point)
// ---------------------------------------------------------------------------
PatchTess PatchConstantHS(InputPatch<HSIn, 4> patch, uint patchID : SV_PrimitiveID)
{
    PatchTess pt;

    // Patch centre for distance-based cull
    float2 centre = (patch[0].posXZ + patch[1].posXZ + patch[2].posXZ + patch[3].posXZ) * 0.25;
    float distCentre = distance(g_camPos.xz, centre);

    // Cull patches beyond draw distance
    if (distCentre > 3200.0)
    {
        pt.edgeTess[0] = pt.edgeTess[1] = pt.edgeTess[2] = pt.edgeTess[3] = 0.0;
        pt.insideTess[0] = pt.insideTess[1] = 0.0;
        return pt;
    }

    // Edge midpoints (match DX11 quad patch edge order)
    // Edge 0: points[0]–points[1] (bottom)
    // Edge 1: points[1]–points[2] (right)
    // Edge 2: points[2]–points[3] (top)
    // Edge 3: points[3]–points[0] (left)
    pt.edgeTess[0] = TessFromDist((patch[0].posXZ + patch[1].posXZ) * 0.5);
    pt.edgeTess[1] = TessFromDist((patch[1].posXZ + patch[2].posXZ) * 0.5);
    pt.edgeTess[2] = TessFromDist((patch[2].posXZ + patch[3].posXZ) * 0.5);
    pt.edgeTess[3] = TessFromDist((patch[3].posXZ + patch[0].posXZ) * 0.5);

    pt.insideTess[0] = max(pt.edgeTess[1], pt.edgeTess[3]);
    pt.insideTess[1] = max(pt.edgeTess[0], pt.edgeTess[2]);

    return pt;
}

// ---------------------------------------------------------------------------
// Per-control-point function (pass-through)
// ---------------------------------------------------------------------------
[domain("quad")]
[partitioning("fractional_even")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(4)]
[patchconstantfunc("PatchConstantHS")]
HSOut HSMain(InputPatch<HSIn, 4> patch,
           uint i : SV_OutputControlPointID,
           uint patchID : SV_PrimitiveID)
{
    HSOut o;
    o.posXZ = patch[i].posXZ;
    o.uv    = patch[i].uv;
    return o;
}
