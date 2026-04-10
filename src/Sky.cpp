#include "Sky.h"

void Sky::Create(ID3D11Device* device) {
    // Full-screen triangle: VS uses SV_VertexID — no input layout needed
    m_shader.Create(device,
        L"shaders/fullscreen_vs.hlsl", "VSMain",
        L"shaders/sky_ps.hlsl",        "PSMain");

    // Depth-stencil: read depth but do NOT write (sky is behind everything)
    D3D11_DEPTH_STENCIL_DESC dsd{};
    dsd.DepthEnable    = TRUE;
    dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;  // no depth write
    dsd.DepthFunc      = D3D11_COMPARISON_LESS_EQUAL;
    HR(device->CreateDepthStencilState(&dsd, &m_dssNoDepthWrite));

    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode        = D3D11_FILL_SOLID;
    rd.CullMode        = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    HR(device->CreateRasterizerState(&rd, &m_rsCullNone));
}

void Sky::Draw(ID3D11DeviceContext* ctx) const {
    ctx->RSSetState(m_rsCullNone.Get());
    ctx->OMSetDepthStencilState(m_dssNoDepthWrite.Get(), 0);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->IASetInputLayout(nullptr);

    // Bind day panorama at t12, night panorama at t13
    ID3D11ShaderResourceView* srvs[2] = { m_skyboxSRV, m_nightSkyboxSRV };
    ctx->PSSetShaderResources(12, 2, srvs);

    m_shader.Bind(ctx);
    ctx->Draw(3, 0);     // 3 vertices → 1 full-screen triangle

    // Unbind t12 and t13
    ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
    ctx->PSSetShaderResources(12, 2, nullSRVs);

    // Restore defaults
    ctx->OMSetDepthStencilState(nullptr, 0);
    ctx->RSSetState(nullptr);
}
