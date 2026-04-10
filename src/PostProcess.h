#pragma once
#include "Types.h"
#include "Shader.h"
#include "RenderTarget.h"
#include "ConstantBuffer.h"

// ---------------------------------------------------------------------------
//  PostProcess
//  Owns the HDR offscreen scene RT.
//  Pass chain: SSGI → GodRays → Bloom (4-level Kawase) → FXAA → Tonemap
//  The scene depth DSV is kept OM-bound at tonemap time so ReShade can use it.
// ---------------------------------------------------------------------------
class PostProcess {
public:
    void Create(ID3D11Device* device, UINT screenW, UINT screenH);
    void Resize(ID3D11Device* device, UINT w, UINT h);

    // Call before rendering scene content
    void BeginSceneCapture(ID3D11DeviceContext* ctx);

    // Execute full post-process chain and output to the provided backbuffer RTV.
    void Execute(ID3D11DeviceContext* ctx,
                 ID3D11RenderTargetView* backBufferRTV,
                 UINT screenW, UINT screenH);

    // GBuffer normals RT (used by SSAO)
    RenderTarget& NormalsRT()   { return m_normalsRT; }
    // HDR scene RT (terrain + water rendered here)
    RenderTarget& SceneHDRRT()  { return m_sceneHDRRT; }
    // Scene depth (R32_TYPELESS: DSV for rendering, SRV for SSAO + ReShade)
    RenderTarget& SceneDepthRT(){ return m_sceneDepthRT; }

    void HotReloadShaders(ID3D11Device* device);

    void UpdateCB(ID3D11DeviceContext* ctx) { m_ppCB.Update(ctx, m_data); }
    void PSBindCB(ID3D11DeviceContext* ctx, UINT slot) const { m_ppCB.PSBind(ctx, slot); }

    PostProcessData& Params() { return m_data; }

private:
    void DrawFullscreen(ID3D11DeviceContext* ctx) const;

    // Existing passes
    ShaderProgram m_fxaaShader;
    ShaderProgram m_tonemapShader;

    // SSGI (screen-space global illumination)
    ShaderProgram m_ssgiShader;
    RenderTarget  m_ssgiRT;

    // God rays (radial blur toward the sun)
    ShaderProgram m_godRayShader;
    RenderTarget  m_godRayRT;

    // Bloom: bright-pass → 4× downsample → 4× upsample (Dual-Kawase)
    ShaderProgram m_bloomBrightShader;
    ShaderProgram m_bloomDownShader;
    ShaderProgram m_bloomUpShader;
    static constexpr int k_bloomLevels = 4;
    RenderTarget  m_bloomBrightRT;
    RenderTarget  m_bloomDownRT[k_bloomLevels];
    RenderTarget  m_bloomUpRT  [k_bloomLevels];

    // Core RTs
    RenderTarget m_sceneHDRRT;
    RenderTarget m_sceneDepthRT;
    RenderTarget m_normalsRT;
    RenderTarget m_fxaaRT;

    ConstantBuffer<PostProcessData> m_ppCB;
    PostProcessData m_data{
        1.0f, 1, 1, 0,     // exposure, ssaoEnabled, fxaaEnabled, underwaterEnabled
        1.0f, 0.8f, 0.4f, 0.5f, // bloomThreshold, bloomIntensity, godRayIntensity, lensFlareIntensity
        0.12f, 0.0f, 0.0f, 0.0f  // ssgiIntensity, pad[3]
    };

    ComPtr<ID3D11SamplerState>      m_linearSampler;
    ComPtr<ID3D11DepthStencilState> m_dssNoDepth;
    ComPtr<ID3D11RasterizerState>   m_rsCullNone;
};
