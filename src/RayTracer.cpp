#include "RayTracer.h"

// ---------------------------------------------------------------------------
void RayTracer::Create(ID3D11Device* device)
{
    // Shader: reuse the shared fullscreen triangle VS; new ray-marching PS
    m_shader.Create(device,
        L"shaders/fullscreen_vs.hlsl", "VSMain",
        L"shaders/raytrace_ps.hlsl",   "PSMain");
    // No input layout needed — fullscreen_vs uses SV_VertexID

    m_rtCB.Create(device);

    // Alpha-blend state: dst = src.rgb * src.a + dst.rgb * (1 - src.a)
    // This lets the shader output alpha=0 for non-water pixels (no change)
    // and alpha=Fresnel for water pixels (ray-traced contribution blended in).
    {
        D3D11_BLEND_DESC bd{};
        bd.RenderTarget[0].BlendEnable    = TRUE;
        bd.RenderTarget[0].SrcBlend       = D3D11_BLEND_SRC_ALPHA;
        bd.RenderTarget[0].DestBlend      = D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOp        = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].SrcBlendAlpha  = D3D11_BLEND_ONE;
        bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
        bd.RenderTarget[0].BlendOpAlpha   = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        HR(device->CreateBlendState(&bd, &m_blendState));
    }

    // Depth-stencil: no depth test/write (fullscreen post-process quad)
    {
        D3D11_DEPTH_STENCIL_DESC dd{};
        dd.DepthEnable   = FALSE;
        dd.StencilEnable = FALSE;
        HR(device->CreateDepthStencilState(&dd, &m_dssNoDepth));
    }

    // Rasterizer: cull none so the single fullscreen triangle is always visible
    {
        D3D11_RASTERIZER_DESC rd{};
        rd.FillMode              = D3D11_FILL_SOLID;
        rd.CullMode              = D3D11_CULL_NONE;
        rd.FrontCounterClockwise = FALSE;
        rd.DepthClipEnable       = FALSE;
        HR(device->CreateRasterizerState(&rd, &m_rsNoCull));
    }
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
void RayTracer::BindCB(ID3D11DeviceContext* ctx)
{
    RayTraceData rtd{};
    rtd.enabled        = enabled         ? 1 : 0;
    rtd.reflectEnabled = reflectEnabled  ? 1 : 0;
    rtd.refractEnabled = refractEnabled  ? 1 : 0;
    rtd.stepsPerRay    = numSteps;
    rtd.shadowSteps    = shadowSteps;
    rtd.maxDistance    = maxDistance;
    m_rtCB.Update(ctx, rtd);
    m_rtCB.AllBind(ctx, 9);   // all stages so terrain_ps reads g_rtEnabled
}

// ---------------------------------------------------------------------------
void RayTracer::Execute(ID3D11DeviceContext* ctx, UINT width, UINT height,
                        ID3D11RenderTargetView*   sceneHDRRTV,
                        ID3D11ShaderResourceView* depthSRV,
                        ID3D11ShaderResourceView* normalsSRV,
                        ID3D11ShaderResourceView* heightmapSRV)
{
    // CB already updated by BindCB() call in DoMainPass; re-bind to PS in case cleared
    m_rtCB.PSBind(ctx, 9);

    // Output RT FIRST — this unbinds NormalsRT from RTV slot 1.
    // D3D11 silently nulls any SRV that conflicts with an active RTV, so we must
    // release the NormalsRT RTV binding before binding it as normalsSRV below.
    ctx->OMSetRenderTargets(1, &sceneHDRRTV, nullptr);

    // Bind read-only inputs (NormalsRT is now free from RTV, safe to bind as SRV)
    ID3D11ShaderResourceView* srvs[3] = { depthSRV, normalsSRV, heightmapSRV };
    ctx->PSSetShaderResources(0, 3, srvs);

    // Blend onto existing scene colour
    const float blendFactor[4] = { 1,1,1,1 };
    ctx->OMSetBlendState(m_blendState.Get(), blendFactor, 0xFFFFFFFF);
    ctx->OMSetDepthStencilState(m_dssNoDepth.Get(), 0);
    ctx->RSSetState(m_rsNoCull.Get());

    // Viewport
    D3D11_VIEWPORT vp{};
    vp.Width  = (float)width;
    vp.Height = (float)height;
    vp.MaxDepth = 1.0f;
    ctx->RSSetViewports(1, &vp);

    // Draw fullscreen triangle (no VB/IB — uses SV_VertexID)
    ctx->IASetInputLayout(nullptr);
    ctx->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_shader.Bind(ctx);
    ctx->Draw(3, 0);

    // Restore blend state to opaque and clear SRV slots
    ctx->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
    ctx->OMSetDepthStencilState(nullptr, 0);
    ctx->RSSetState(nullptr);
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
    ID3D11ShaderResourceView* nullSRVs[3] = {};
    ctx->PSSetShaderResources(0, 3, nullSRVs);
}

// ---------------------------------------------------------------------------
void RayTracer::HotReloadShaders(ID3D11Device* device)
{
    m_shader.HotReload(device);
}
