#pragma once
#include "Types.h"

// ---------------------------------------------------------------------------
//  RenderTarget
//  Wraps a D3D11 Texture2D with optional RTV, SRV, DSV faces.
//  For depth: create with isDepth=true → R32_TYPELESS, DSV=D32_FLOAT, SRV=R32_FLOAT
// ---------------------------------------------------------------------------
class RenderTarget {
public:
    // Create as a color render target
    void Create(ID3D11Device* device, UINT width, UINT height,
                DXGI_FORMAT format = DXGI_FORMAT_R16G16B16A16_FLOAT,
                bool hasSRV = true);

    // Create as a depth-stencil target (R32_TYPELESS, so both DSV and SRV can be created)
    void CreateDepth(ID3D11Device* device, UINT width, UINT height);

    // For shadow cascades: D32_FLOAT only (array or separate instances)
    void CreateShadow(ID3D11Device* device, UINT size);

    void Resize(ID3D11Device* device, UINT width, UINT height);

    void BindAsRTV(ID3D11DeviceContext* ctx, ID3D11DepthStencilView* dsv = nullptr) const;
    void BindAsSRV(ID3D11DeviceContext* ctx, UINT slot, UINT stage_flags = 0xFF) const;
    void Clear(ID3D11DeviceContext* ctx, const float color[4] = nullptr) const;
    void ClearDepth(ID3D11DeviceContext* ctx, float depthVal = 1.0f) const;

    ID3D11RenderTargetView*   RTV()         const { return m_rtv.Get(); }
    ID3D11ShaderResourceView* SRV()         const { return m_srv.Get(); }
    ID3D11DepthStencilView*   DSV()         const { return m_dsv.Get(); }
    // Read-only DSV: allows the texture to be simultaneously bound as SRV
    ID3D11DepthStencilView*   DSVReadOnly() const { return m_dsvReadOnly.Get(); }
    ID3D11Texture2D*          Texture() const { return m_texture.Get(); }
    UINT Width()  const { return m_width; }
    UINT Height() const { return m_height; }

private:
    void Reset();

    UINT m_width  = 0;
    UINT m_height = 0;
    bool m_isDepth  = false;
    DXGI_FORMAT m_colorFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

    ComPtr<ID3D11Texture2D>          m_texture;
    ComPtr<ID3D11RenderTargetView>   m_rtv;
    ComPtr<ID3D11ShaderResourceView> m_srv;
    ComPtr<ID3D11DepthStencilView>   m_dsv;
    ComPtr<ID3D11DepthStencilView>   m_dsvReadOnly;  // D3D11_DSV_FLAG_READ_ONLY_DEPTH
};
