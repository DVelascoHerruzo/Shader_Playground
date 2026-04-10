#pragma once
#include "Types.h"
#include "Shader.h"
#include "ConstantBuffer.h"

// ---------------------------------------------------------------------------
//  RayTracer
//
//  Screen-space heightmap ray marcher.  Runs as a full-screen quad pass that
//  composites ray-traced water reflections and refractions on top of the
//  existing scene HDR render target using hardware alpha blending:
//
//      final = raytraced.rgb * raytraced.a + existing_scene * (1 - raytraced.a)
//
//  The pass is a no-op (outputs alpha=0) for non-water pixels, so it is safe
//  to always run it — enabling/disabling just changes the g_rtEnabled flag.
//
//  Resources the caller must bind before Execute():
//    b0  CameraData      (Renderer binds this every frame)
//    b1  SunLightData    (Renderer binds this every frame)
//    b3  TerrainData     (bound in DoMainPass, still live)
//    b4  WaterData       (bound in DoMainPass, still live)
//  Execute() binds:
//    b9  RayTraceData    (owns the CB)
//    t0  scene depth SRV
//    t1  scene normals SRV
//    t2  heightmap SRV
//  Render target set by Execute():
//    SceneHDR RTV (passed in) + alpha-blend state
// ---------------------------------------------------------------------------
class RayTracer {
public:
    void Create(ID3D11Device* device);

    // Update and bind RayTraceData CB to all stages at slot 9.
    // Call this before DoMainPass so terrain_ps can read g_rtEnabled/g_rtShadowSteps.
    void BindCB(ID3D11DeviceContext* ctx);

    // Run the ray trace pass.
    // sceneHDRRTV: the existing scene HDR render target to blend on top of.
    // depthSRV / normalsSRV / heightmapSRV: read-only scene resources.
    void Execute(ID3D11DeviceContext* ctx, UINT width, UINT height,
                 ID3D11RenderTargetView*   sceneHDRRTV,
                 ID3D11ShaderResourceView* depthSRV,
                 ID3D11ShaderResourceView* normalsSRV,
                 ID3D11ShaderResourceView* heightmapSRV);

    void HotReloadShaders(ID3D11Device* device);

    // --- Tweakable parameters (exposed to UI) ---
    bool  enabled         = true;
    bool  reflectEnabled  = true;
    bool  refractEnabled  = true;
    int   numSteps        = 64;     // ray march steps for reflection/refraction
    int   shadowSteps     = 32;     // ray march steps for terrain sun shadows
    float maxDistance     = 250.0f; // max ray travel distance in world metres

private:
    ShaderProgram               m_shader;
    ConstantBuffer<RayTraceData> m_rtCB;
    ComPtr<ID3D11BlendState>     m_blendState;   // SRC_ALPHA / INV_SRC_ALPHA
    ComPtr<ID3D11DepthStencilState> m_dssNoDepth; // no depth test for fullscreen quad
    ComPtr<ID3D11RasterizerState>   m_rsNoCull;
};
