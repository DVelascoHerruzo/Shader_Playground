#include "RenderTarget.h"

static constexpr UINT STAGE_VS = 1, STAGE_PS = 2, STAGE_DS = 4, STAGE_ALL = 0xFF;

void RenderTarget::Reset() {
    m_rtv.Reset();
    m_srv.Reset();
    m_dsv.Reset();
    m_dsvReadOnly.Reset();
    m_texture.Reset();
}

void RenderTarget::Create(ID3D11Device* device, UINT width, UINT height,
                          DXGI_FORMAT format, bool hasSRV)
{
    Reset();
    m_width  = width;
    m_height = height;
    m_isDepth = false;
    m_colorFormat = format;

    D3D11_TEXTURE2D_DESC td{};
    td.Width            = width;
    td.Height           = height;
    td.MipLevels        = 1;
    td.ArraySize        = 1;
    td.Format           = format;
    td.SampleDesc       = { 1, 0 };
    td.Usage            = D3D11_USAGE_DEFAULT;
    td.BindFlags        = D3D11_BIND_RENDER_TARGET | (hasSRV ? D3D11_BIND_SHADER_RESOURCE : 0);
    HR(device->CreateTexture2D(&td, nullptr, &m_texture));

    HR(device->CreateRenderTargetView(m_texture.Get(), nullptr, &m_rtv));
    if (hasSRV)
        HR(device->CreateShaderResourceView(m_texture.Get(), nullptr, &m_srv));
}

void RenderTarget::CreateDepth(ID3D11Device* device, UINT width, UINT height) {
    Reset();
    m_width  = width;
    m_height = height;
    m_isDepth = true;

    D3D11_TEXTURE2D_DESC td{};
    td.Width      = width;
    td.Height     = height;
    td.MipLevels  = 1;
    td.ArraySize  = 1;
    td.Format     = DXGI_FORMAT_R32_TYPELESS;  // Allows both DSV (D32_FLOAT) and SRV (R32_FLOAT)
    td.SampleDesc = { 1, 0 };
    td.Usage      = D3D11_USAGE_DEFAULT;
    td.BindFlags  = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    HR(device->CreateTexture2D(&td, nullptr, &m_texture));

    // DSV as D32_FLOAT (writeable)
    D3D11_DEPTH_STENCIL_VIEW_DESC dsvd{};
    dsvd.Format        = DXGI_FORMAT_D32_FLOAT;
    dsvd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    HR(device->CreateDepthStencilView(m_texture.Get(), &dsvd, &m_dsv));

    // Read-only DSV: allows simultaneous SRV binding (no depth writes)
    dsvd.Flags = D3D11_DSV_READ_ONLY_DEPTH;
    HR(device->CreateDepthStencilView(m_texture.Get(), &dsvd, &m_dsvReadOnly));
    dsvd.Flags = 0;  // reset for any future use

    // SRV as R32_FLOAT (for SSAO and ReShade depth access)
    D3D11_SHADER_RESOURCE_VIEW_DESC srvd{};
    srvd.Format              = DXGI_FORMAT_R32_FLOAT;
    srvd.ViewDimension       = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvd.Texture2D.MipLevels = 1;
    HR(device->CreateShaderResourceView(m_texture.Get(), &srvd, &m_srv));
}

void RenderTarget::CreateShadow(ID3D11Device* device, UINT size) {
    Reset();
    m_width = m_height = size;
    m_isDepth = true;

    D3D11_TEXTURE2D_DESC td{};
    td.Width      = size;
    td.Height     = size;
    td.MipLevels  = 1;
    td.ArraySize  = 1;
    td.Format     = DXGI_FORMAT_R32_TYPELESS;
    td.SampleDesc = { 1, 0 };
    td.Usage      = D3D11_USAGE_DEFAULT;
    td.BindFlags  = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    HR(device->CreateTexture2D(&td, nullptr, &m_texture));

    D3D11_DEPTH_STENCIL_VIEW_DESC dsvd{};
    dsvd.Format        = DXGI_FORMAT_D32_FLOAT;
    dsvd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    HR(device->CreateDepthStencilView(m_texture.Get(), &dsvd, &m_dsv));

    D3D11_SHADER_RESOURCE_VIEW_DESC srvd{};
    srvd.Format              = DXGI_FORMAT_R32_FLOAT;
    srvd.ViewDimension       = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvd.Texture2D.MipLevels = 1;
    HR(device->CreateShaderResourceView(m_texture.Get(), &srvd, &m_srv));
}

void RenderTarget::Resize(ID3D11Device* device, UINT width, UINT height) {
    if (m_isDepth) CreateDepth(device, width, height);
    else           Create(device, width, height, m_colorFormat, m_srv != nullptr);
}

void RenderTarget::BindAsRTV(ID3D11DeviceContext* ctx, ID3D11DepthStencilView* dsv) const {
    ID3D11RenderTargetView* rtv = m_rtv.Get();
    ctx->OMSetRenderTargets(1, &rtv, dsv);
    D3D11_VIEWPORT vp{};
    vp.Width    = (float)m_width;
    vp.Height   = (float)m_height;
    vp.MaxDepth = 1.0f;
    ctx->RSSetViewports(1, &vp);
}

void RenderTarget::BindAsSRV(ID3D11DeviceContext* ctx, UINT slot, UINT) const {
    ID3D11ShaderResourceView* srv = m_srv.Get();
    ctx->PSSetShaderResources(slot, 1, &srv);
    ctx->DSSetShaderResources(slot, 1, &srv);
}

void RenderTarget::Clear(ID3D11DeviceContext* ctx, const float color[4]) const {
    if (!m_rtv) return;
    static const float black[4] = { 0, 0, 0, 1 };
    ctx->ClearRenderTargetView(m_rtv.Get(), color ? color : black);
}

void RenderTarget::ClearDepth(ID3D11DeviceContext* ctx, float depthVal) const {
    if (m_dsv)
        ctx->ClearDepthStencilView(m_dsv.Get(), D3D11_CLEAR_DEPTH, depthVal, 0);
}
