// fullscreen_vs.hlsl
// Generates a full-screen triangle from three SV_VertexID values (0,1,2).
// No vertex buffer is needed — bind nullptr and draw 3 vertices.
// Used by sky, SSAO, blur, FXAA, and tonemap passes.

struct VSOut {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

VSOut VSMain(uint id : SV_VertexID)
{
    VSOut o;
    // UV:  id=0 → (0,0), id=1 → (2,0), id=2 → (0,2)
    o.uv.x = (float)((id << 1) & 2);
    o.uv.y = (float)(id & 2);
    // Clip-space:  expand UV [0..1] to [-1..1], flip Y
    o.pos  = float4(o.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}
