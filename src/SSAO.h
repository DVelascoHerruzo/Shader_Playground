#pragma once
#include "Types.h"
#include "Shader.h"
#include "RenderTarget.h"
#include "ConstantBuffer.h"

// ---------------------------------------------------------------------------
//  SSAO
//  Screen-Space Ambient Occlusion.
//  Input:  scene depth SRV (R32_FLOAT), GBuffer normals SRV (R16G16B16A16_FLOAT)
//  Output: half-res SSAO mask (R8_UNORM), blurred with bilateral Gaussian
// ---------------------------------------------------------------------------
class SSAO {
public:
    void Create(ID3D11Device* device, UINT screenW, UINT screenH);
    void Resize(ID3D11Device* device, UINT w, UINT h);

    // Compute SSAO + blur. Outputs mask into m_ssaoBlurredRT.
    void Compute(ID3D11DeviceContext* ctx,
                 ID3D11ShaderResourceView* depthSRV,
                 ID3D11ShaderResourceView* normalSRV);

    ID3D11ShaderResourceView* GetSRV() const { return m_ssaoBlurredRT.SRV(); }

    void HotReloadShaders(ID3D11Device* device);

    void UpdateCB(ID3D11DeviceContext* ctx) { m_ssaoCB.Update(ctx, m_data); }

    // Tweakable parameters (exposed to UI)
    SSAOData& Params() { return m_data; }

    bool enabled = true;

private:
    void GenerateSamples();
    void CreateNoiseTex(ID3D11Device* device);

    ShaderProgram m_ssaoShader;
    ShaderProgram m_blurShader;

    RenderTarget m_ssaoRawRT;
    RenderTarget m_ssaoBlurredRT;

    ConstantBuffer<SSAOData> m_ssaoCB;
    SSAOData m_data{};

    ComPtr<ID3D11ShaderResourceView> m_noiseSRV;
    ComPtr<ID3D11Texture2D>          m_noiseTex;
    ComPtr<ID3D11SamplerState>       m_pointSampler;
    ComPtr<ID3D11SamplerState>       m_linearSampler;

    ComPtr<ID3D11DepthStencilState>  m_dssNoDepth;
    ComPtr<ID3D11RasterizerState>    m_rsCullNone;

    UINT m_w = 0, m_h = 0;
};
