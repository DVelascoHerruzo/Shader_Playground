#include "SSAO.h"
#include <random>
#include <cmath>
#include <algorithm>
#include <DirectXPackedVector.h>
using namespace DirectX::PackedVector;

static constexpr float PI = 3.14159265358979f;

void SSAO::GenerateSamples() {
    std::mt19937 rng(1234);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    std::uniform_real_distribution<float> distSigned(-1.0f, 1.0f);

    for (int i = 0; i < 64; i++) {
        // Random point in unit hemisphere (cosine-weighted)
        float angle   = dist(rng) * 2.0f * PI;
        float cosine  = dist(rng);
        float sine    = sqrtf(1.0f - cosine * cosine);
        XMFLOAT3 sample = {
            sinf(angle) * sine,
            cosf(angle) * sine,
            cosine   // z > 0 = hemisphere above surface
        };

        // Accelerating interpolation for clustering near the origin
        float scale = (float)i / 64.0f;
        scale = 0.1f + scale * scale * 0.9f;

        m_data.samples[i] = {
            sample.x * scale,
            sample.y * scale,
            sample.z * scale,
            0.0f
        };
    }
    m_data.radius    = 1.2f;
    m_data.bias      = 0.025f;
    m_data.intensity = 1.5f;
}

void SSAO::CreateNoiseTex(ID3D11Device* device) {
    // 4x4 noise texture of random rotations around Z axis
    std::mt19937 rng(5678);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

    const int NOISE_SIZE = 4;
    std::vector<uint16_t> noiseData(NOISE_SIZE * NOISE_SIZE * 4);  // RGBA16F
    for (int i = 0; i < NOISE_SIZE * NOISE_SIZE; i++) {
        float rx = dist(rng), ry = dist(rng);
        noiseData[i*4+0] = XMConvertFloatToHalf(rx);
        noiseData[i*4+1] = XMConvertFloatToHalf(ry);
        noiseData[i*4+2] = XMConvertFloatToHalf(0.0f);
        noiseData[i*4+3] = XMConvertFloatToHalf(0.0f);
    }
    D3D11_TEXTURE2D_DESC td{};
    td.Width     = NOISE_SIZE;
    td.Height    = NOISE_SIZE;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format    = DXGI_FORMAT_R16G16B16A16_FLOAT;
    td.SampleDesc = { 1, 0 };
    td.Usage     = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sr{ noiseData.data(), (UINT)(NOISE_SIZE * 4 * sizeof(uint16_t)) };
    HR(device->CreateTexture2D(&td, &sr, &m_noiseTex));
    HR(device->CreateShaderResourceView(m_noiseTex.Get(), nullptr, &m_noiseSRV));
}

void SSAO::Create(ID3D11Device* device, UINT screenW, UINT screenH) {
    m_w = screenW; m_h = screenH;

    m_ssaoShader.Create(device,
        L"shaders/fullscreen_vs.hlsl", "VSMain",
        L"shaders/ssao_ps.hlsl",       "PSMain");

    m_blurShader.Create(device,
        L"shaders/fullscreen_vs.hlsl", "VSMain",
        L"shaders/ssao_blur_ps.hlsl",  "PSMain");

    // Half-res SSAO RTs (R8_UNORM)
    UINT hw = std::max(1u, screenW / 2);
    UINT hh = std::max(1u, screenH / 2);
    m_ssaoRawRT    .Create(device, hw, hh, DXGI_FORMAT_R8_UNORM);
    m_ssaoBlurredRT.Create(device, hw, hh, DXGI_FORMAT_R8_UNORM);

    m_ssaoCB.Create(device);
    GenerateSamples();

    CreateNoiseTex(device);

    // Samplers
    D3D11_SAMPLER_DESC sd{};
    sd.Filter   = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.MaxLOD   = D3D11_FLOAT32_MAX;
    HR(device->CreateSamplerState(&sd, &m_pointSampler));
    sd.Filter   = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
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

void SSAO::Resize(ID3D11Device* device, UINT w, UINT h) {
    m_w = w; m_h = h;
    UINT hw = std::max(1u, w / 2), hh = std::max(1u, h / 2);
    m_ssaoRawRT    .Create(device, hw, hh, DXGI_FORMAT_R8_UNORM);
    m_ssaoBlurredRT.Create(device, hw, hh, DXGI_FORMAT_R8_UNORM);
}

void SSAO::Compute(ID3D11DeviceContext* ctx,
                   ID3D11ShaderResourceView* depthSRV,
                   ID3D11ShaderResourceView* normalSRV)
{
    ctx->RSSetState(m_rsCullNone.Get());
    ctx->OMSetDepthStencilState(m_dssNoDepth.Get(), 0);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->IASetInputLayout(nullptr);

    // Update SSAO CB
    m_ssaoCB.Update(ctx, m_data);
    m_ssaoCB.PSBind(ctx, 5);

    ID3D11SamplerState* samplers[2] = { m_pointSampler.Get(), m_linearSampler.Get() };
    ctx->PSSetSamplers(0, 2, samplers);

    // ---- Pass 1: Raw SSAO ----
    m_ssaoRawRT.BindAsRTV(ctx, nullptr);

    ID3D11ShaderResourceView* srvs[3] = { depthSRV, normalSRV, m_noiseSRV.Get() };
    ctx->PSSetShaderResources(0, 3, srvs);

    m_ssaoShader.Bind(ctx);
    ctx->Draw(3, 0);

    // Unbind RTV before using as SRV
    ID3D11RenderTargetView* nullRTV = nullptr;
    ctx->OMSetRenderTargets(1, &nullRTV, nullptr);

    // Unbind input SRVs
    ID3D11ShaderResourceView* nullSRVs[3] = {};
    ctx->PSSetShaderResources(0, 3, nullSRVs);

    // ---- Pass 2: Bilateral blur ----
    m_ssaoBlurredRT.BindAsRTV(ctx, nullptr);

    ID3D11ShaderResourceView* blurSRVs[2] = { m_ssaoRawRT.SRV(), depthSRV };
    ctx->PSSetShaderResources(0, 2, blurSRVs);

    m_blurShader.Bind(ctx);
    ctx->Draw(3, 0);

    ctx->OMSetRenderTargets(1, &nullRTV, nullptr);
    ID3D11ShaderResourceView* nullSRVs2[2] = {};
    ctx->PSSetShaderResources(0, 2, nullSRVs2);

    // Restore default states
    ctx->RSSetState(nullptr);
    ctx->OMSetDepthStencilState(nullptr, 0);
}

void SSAO::HotReloadShaders(ID3D11Device* device) {
    m_ssaoShader.HotReload(device);
    m_blurShader.HotReload(device);
}
