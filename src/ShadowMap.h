#pragma once
#include "Types.h"
#include "Shader.h"
#include "RenderTarget.h"
#include "ConstantBuffer.h"
#include "Camera.h"

// ---------------------------------------------------------------------------
//  ShadowMap
//  3-cascade CSM (Cascaded Shadow Maps).
//  Each cascade has its own 2048x2048 depth-only RT.
// ---------------------------------------------------------------------------
class ShadowMap {
public:
    void Create(ID3D11Device* device);

    // Compute light matrices from camera frustum + sun direction
    void Update(const Camera& camera, float aspectRatio, const XMFLOAT3& dirToSun);

    // Get CB data (call UpdateCB to push to GPU)
    void UpdateCB(ID3D11DeviceContext* ctx, int cascadeIdx);
    void UpdateCBAllCascades(ID3D11DeviceContext* ctx);

    const ConstantBuffer<ShadowData>& GetCB() const { return m_cb; }
    void AllBindCB(ID3D11DeviceContext* ctx, UINT slot) const { m_cb.AllBind(ctx, slot); }

    // Bind DSV for shadow rendering of a specific cascade
    void BeginCascade(ID3D11DeviceContext* ctx, int cascade);
    void EndCascade(ID3D11DeviceContext* ctx);

    // Bind all cascade SRVs for terrain/water PS consumption
    void BindSRVs(ID3D11DeviceContext* ctx, UINT startSlot) const;
    void UnbindSRVs(ID3D11DeviceContext* ctx, UINT startSlot) const;

    // Sampler states
    ID3D11SamplerState* GetShadowSampler() const { return m_shadowSampler.Get(); }

    // Expose cascade data
    const ShadowData& GetData() const { return m_data; }

private:
    void ComputeCascadeFrustum(const Camera& cam, float aspect, float nearZ, float farZ,
                               const XMFLOAT3& sunDir, XMFLOAT4X4& outLightVP);

    RenderTarget m_cascades[NUM_CASCADES];

    ShadowData m_data{};
    ConstantBuffer<ShadowData> m_cb;

    D3D11_VIEWPORT m_shadowVP{};

    ComPtr<ID3D11RasterizerState>  m_shadowRS;
    ComPtr<ID3D11DepthStencilState> m_shadowDSS;
    ComPtr<ID3D11SamplerState>     m_shadowSampler;

    // Cascade split lambda (blend factor between log/linear partitioning)
    static constexpr float LAMBDA = 0.75f;
};
