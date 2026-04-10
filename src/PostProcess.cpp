#include "PostProcess.h"
#include <algorithm>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static void CreateHalfRT(ID3D11Device* dev, UINT w, UINT h, UINT level,
                          RenderTarget& rt, DXGI_FORMAT fmt = DXGI_FORMAT_R16G16B16A16_FLOAT)
{
    UINT mipW = std::max(1u, w >> level);
    UINT mipH = std::max(1u, h >> level);
    rt.Create(dev, mipW, mipH, fmt);
}

// ---------------------------------------------------------------------------
void PostProcess::Create(ID3D11Device* device, UINT screenW, UINT screenH) {

    m_fxaaShader.Create(device,
        L"shaders/fullscreen_vs.hlsl", "VSMain",
        L"shaders/fxaa_ps.hlsl",       "PSMain");

    m_tonemapShader.Create(device,
        L"shaders/fullscreen_vs.hlsl", "VSMain",
        L"shaders/tonemap_ps.hlsl",    "PSMain");

    m_ssgiShader.Create(device,
        L"shaders/fullscreen_vs.hlsl", "VSMain",
        L"shaders/ssgi_ps.hlsl",       "PSMain");

    m_godRayShader.Create(device,
        L"shaders/fullscreen_vs.hlsl", "VSMain",
        L"shaders/godrays_ps.hlsl",    "PSMain");

    m_bloomBrightShader.Create(device,
        L"shaders/fullscreen_vs.hlsl", "VSMain",
        L"shaders/bloom_ps.hlsl",      "PSBright");

    m_bloomDownShader.Create(device,
        L"shaders/fullscreen_vs.hlsl", "VSMain",
        L"shaders/bloom_ps.hlsl",      "PSDownsample");

    m_bloomUpShader.Create(device,
        L"shaders/fullscreen_vs.hlsl", "VSMain",
        L"shaders/bloom_ps.hlsl",      "PSUpsample");

    m_sceneHDRRT  .Create(device, screenW, screenH, DXGI_FORMAT_R16G16B16A16_FLOAT);
    m_sceneDepthRT.CreateDepth(device, screenW, screenH);
    m_normalsRT   .Create(device, screenW, screenH, DXGI_FORMAT_R16G16B16A16_FLOAT);
    m_fxaaRT      .Create(device, screenW, screenH, DXGI_FORMAT_R8G8B8A8_UNORM);

    m_ssgiRT  .Create(device, screenW / 2, screenH / 2, DXGI_FORMAT_R16G16B16A16_FLOAT);
    m_godRayRT.Create(device, screenW / 2, screenH / 2, DXGI_FORMAT_R16G16B16A16_FLOAT);
    m_bloomBrightRT.Create(device, screenW / 2, screenH / 2, DXGI_FORMAT_R16G16B16A16_FLOAT);
    for (int i = 0; i < k_bloomLevels; ++i) {
        CreateHalfRT(device, screenW / 2, screenH / 2, i, m_bloomDownRT[i]);
        CreateHalfRT(device, screenW / 2, screenH / 2, i, m_bloomUpRT[i]);
    }

    m_ppCB.Create(device);

    D3D11_SAMPLER_DESC sd{};
    sd.Filter   = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD   = D3D11_FLOAT32_MAX;
    HR(device->CreateSamplerState(&sd, &m_linearSampler));

    D3D11_DEPTH_STENCIL_DESC dsd{};
    dsd.DepthEnable = FALSE;
    HR(device->CreateDepthStencilState(&dsd, &m_dssNoDepth));

    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode        = D3D11_FILL_SOLID;
    rd.CullMode        = D3D11_CULL_NONE;
    rd.DepthClipEnable = FALSE;
    HR(device->CreateRasterizerState(&rd, &m_rsCullNone));
}

void PostProcess::Resize(ID3D11Device* device, UINT w, UINT h) {
    m_sceneHDRRT  .Create(device, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT);
    m_sceneDepthRT.CreateDepth(device, w, h);
    m_normalsRT   .Create(device, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT);
    m_fxaaRT      .Create(device, w, h, DXGI_FORMAT_R8G8B8A8_UNORM);

    m_ssgiRT  .Create(device, w / 2, h / 2, DXGI_FORMAT_R16G16B16A16_FLOAT);
    m_godRayRT.Create(device, w / 2, h / 2, DXGI_FORMAT_R16G16B16A16_FLOAT);
    m_bloomBrightRT.Create(device, w / 2, h / 2, DXGI_FORMAT_R16G16B16A16_FLOAT);
    for (int i = 0; i < k_bloomLevels; ++i) {
        CreateHalfRT(device, w / 2, h / 2, i, m_bloomDownRT[i]);
        CreateHalfRT(device, w / 2, h / 2, i, m_bloomUpRT[i]);
    }
}

void PostProcess::BeginSceneCapture(ID3D11DeviceContext* ctx) {
    ID3D11RenderTargetView* rtvs[2] = {
        m_sceneHDRRT.RTV(),
        m_normalsRT.RTV()
    };
    ctx->OMSetRenderTargets(2, rtvs, m_sceneDepthRT.DSV());
}

void PostProcess::DrawFullscreen(ID3D11DeviceContext* ctx) const {
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->IASetInputLayout(nullptr);
    ctx->Draw(3, 0);
}

// ---------------------------------------------------------------------------
void PostProcess::Execute(ID3D11DeviceContext* ctx,
                          ID3D11RenderTargetView* backBufferRTV,
                          UINT screenW, UINT screenH)
{
    ctx->RSSetState(m_rsCullNone.Get());
    ctx->OMSetDepthStencilState(m_dssNoDepth.Get(), 0);

    ID3D11SamplerState* samp = m_linearSampler.Get();
    ctx->PSSetSamplers(0, 1, &samp);

    m_ppCB.Update(ctx, m_data);
    m_ppCB.PSBind(ctx, 6);

    auto SetFullVP = [&](UINT w, UINT h) {
        D3D11_VIEWPORT vp{};
        vp.Width = (float)w;  vp.Height = (float)h;  vp.MaxDepth = 1.0f;
        ctx->RSSetViewports(1, &vp);
    };

    auto UnbindSRVs = [&](int n) {
        ID3D11ShaderResourceView* nulls[8] = {};
        ctx->PSSetShaderResources(0, n, nulls);
    };

    // -----------------------------------------------------------------------
    // 1. SSGI pass (half-res) — screen-space indirect bounce
    // -----------------------------------------------------------------------
    if (m_data.ssgiIntensity > 0.001f) {
        SetFullVP(screenW / 2, screenH / 2);
        m_ssgiRT.BindAsRTV(ctx, nullptr);
        ID3D11ShaderResourceView* srv[2] = { m_sceneHDRRT.SRV(), m_normalsRT.SRV() };
        ctx->PSSetShaderResources(0, 2, srv);
        m_ssgiShader.Bind(ctx);
        DrawFullscreen(ctx);
        UnbindSRVs(2);
        ID3D11RenderTargetView* nullRTV = nullptr;
        ctx->OMSetRenderTargets(1, &nullRTV, nullptr);
    }

    // -----------------------------------------------------------------------
    // 2. God rays pass (half-res) — radial blur toward sun
    // -----------------------------------------------------------------------
    if (m_data.godRayIntensity > 0.001f) {
        SetFullVP(screenW / 2, screenH / 2);
        m_godRayRT.BindAsRTV(ctx, nullptr);
        ID3D11ShaderResourceView* srv[2] = { m_sceneHDRRT.SRV(), m_sceneDepthRT.SRV() };
        ctx->PSSetShaderResources(0, 2, srv);
        m_godRayShader.Bind(ctx);
        DrawFullscreen(ctx);
        UnbindSRVs(2);
        ID3D11RenderTargetView* nullRTV = nullptr;
        ctx->OMSetRenderTargets(1, &nullRTV, nullptr);
    }

    // -----------------------------------------------------------------------
    // 3. Bloom — bright pass (half-res) → 4-level downsample → 4-level upsample
    // -----------------------------------------------------------------------
    if (m_data.bloomIntensity > 0.001f) {
        // 3a. Bright pass
        SetFullVP(screenW / 2, screenH / 2);
        m_bloomBrightRT.BindAsRTV(ctx, nullptr);
        ID3D11ShaderResourceView* hdrSRV = m_sceneHDRRT.SRV();
        ctx->PSSetShaderResources(0, 1, &hdrSRV);
        m_bloomBrightShader.Bind(ctx);
        DrawFullscreen(ctx);
        UnbindSRVs(1);
        { ID3D11RenderTargetView* n = nullptr; ctx->OMSetRenderTargets(1, &n, nullptr); }

        // 3b. Downsample chain
        for (int i = 0; i < k_bloomLevels; ++i) {
            UINT mw = std::max(1u, (screenW / 2) >> i);
            UINT mh = std::max(1u, (screenH / 2) >> i);
            SetFullVP(mw, mh);
            m_bloomDownRT[i].BindAsRTV(ctx, nullptr);
            ID3D11ShaderResourceView* src = (i == 0) ? m_bloomBrightRT.SRV() : m_bloomDownRT[i-1].SRV();
            ctx->PSSetShaderResources(0, 1, &src);
            m_bloomDownShader.Bind(ctx);
            DrawFullscreen(ctx);
            UnbindSRVs(1);
            ID3D11RenderTargetView* n = nullptr; ctx->OMSetRenderTargets(1, &n, nullptr);
        }

        // 3c. Upsample chain (additive tent blur back up the pyramid)
        for (int i = k_bloomLevels - 1; i >= 0; --i) {
            UINT mw = std::max(1u, (screenW / 2) >> i);
            UINT mh = std::max(1u, (screenH / 2) >> i);
            SetFullVP(mw, mh);
            m_bloomUpRT[i].BindAsRTV(ctx, nullptr);
            ID3D11ShaderResourceView* src = (i == k_bloomLevels - 1)
                ? m_bloomDownRT[i].SRV()
                : m_bloomUpRT[i+1].SRV();
            ctx->PSSetShaderResources(0, 1, &src);
            m_bloomUpShader.Bind(ctx);
            DrawFullscreen(ctx);
            UnbindSRVs(1);
            ID3D11RenderTargetView* n = nullptr; ctx->OMSetRenderTargets(1, &n, nullptr);
        }
    }

    // -----------------------------------------------------------------------
    // 4. FXAA: HDR scene → fxaaRT
    // -----------------------------------------------------------------------
    if (m_data.fxaaEnabled) {
        SetFullVP(screenW, screenH);
        m_fxaaRT.BindAsRTV(ctx, nullptr);
        ID3D11ShaderResourceView* hdrSRV = m_sceneHDRRT.SRV();
        ctx->PSSetShaderResources(0, 1, &hdrSRV);
        m_fxaaShader.Bind(ctx);
        DrawFullscreen(ctx);
        UnbindSRVs(1);
        ID3D11RenderTargetView* nullRTV = nullptr;
        ctx->OMSetRenderTargets(1, &nullRTV, nullptr);
    }

    // -----------------------------------------------------------------------
    // 5. Tonemap: scene + bloom + godRays + SSGI → backbuffer
    //    Keep scene depth DSV bound for ReShade depth provider.
    // -----------------------------------------------------------------------
    {
        SetFullVP(screenW, screenH);
        ctx->OMSetRenderTargets(1, &backBufferRTV, m_sceneDepthRT.DSV());

        ID3D11ShaderResourceView* srcs[4] = {
            m_data.fxaaEnabled ? m_fxaaRT.SRV() : m_sceneHDRRT.SRV(),
            (m_data.bloomIntensity > 0.001f)  ? m_bloomUpRT[0].SRV() : nullptr,
            (m_data.godRayIntensity > 0.001f) ? m_godRayRT.SRV()     : nullptr,
            (m_data.ssgiIntensity > 0.001f)   ? m_ssgiRT.SRV()       : nullptr,
        };
        ctx->PSSetShaderResources(0, 4, srcs);

        m_tonemapShader.Bind(ctx);
        DrawFullscreen(ctx);
        UnbindSRVs(4);
    }

    // Restore states
    ctx->RSSetState(nullptr);
    ctx->OMSetDepthStencilState(nullptr, 0);
}

void PostProcess::HotReloadShaders(ID3D11Device* device) {
    m_fxaaShader      .HotReload(device);
    m_tonemapShader   .HotReload(device);
    m_ssgiShader      .HotReload(device);
    m_godRayShader    .HotReload(device);
    m_bloomBrightShader.HotReload(device);
    m_bloomDownShader .HotReload(device);
    m_bloomUpShader   .HotReload(device);
}
