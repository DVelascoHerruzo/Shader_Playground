#include "ShadowMap.h"
#include <algorithm>
#include <cmath>

void ShadowMap::Create(ID3D11Device* device) {
    // Create 3 shadow cascade RTs
    for (int i = 0; i < NUM_CASCADES; i++)
        m_cascades[i].CreateShadow(device, SHADOW_MAP_SIZE);

    m_shadowVP.Width    = (float)SHADOW_MAP_SIZE;
    m_shadowVP.Height   = (float)SHADOW_MAP_SIZE;
    m_shadowVP.MaxDepth = 1.0f;

    // Rasterizer with shadow bias (reduces shadow acne)
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode              = D3D11_FILL_SOLID;
    rd.CullMode              = D3D11_CULL_BACK;
    rd.DepthClipEnable       = TRUE;
    rd.DepthBias             = 5000;
    rd.DepthBiasClamp        = 0.0f;
    rd.SlopeScaledDepthBias  = 2.0f;
    HR(device->CreateRasterizerState(&rd, &m_shadowRS));

    // PCF shadow comparison sampler
    D3D11_SAMPLER_DESC sd{};
    sd.Filter         = D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
    sd.AddressU       = D3D11_TEXTURE_ADDRESS_BORDER;
    sd.AddressV       = D3D11_TEXTURE_ADDRESS_BORDER;
    sd.AddressW       = D3D11_TEXTURE_ADDRESS_BORDER;
    sd.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
    sd.BorderColor[0] = sd.BorderColor[1] = sd.BorderColor[2] = sd.BorderColor[3] = 1.0f;
    sd.MinLOD         = 0;
    sd.MaxLOD         = D3D11_FLOAT32_MAX;
    HR(device->CreateSamplerState(&sd, &m_shadowSampler));

    m_cb.Create(device);
}

// ---------------------------------------------------------------------------
//  Compute a light view-projection matrix for one cascade sub-frustum
// ---------------------------------------------------------------------------
void ShadowMap::ComputeCascadeFrustum(const Camera& cam, float aspect, float nearZ, float farZ,
                                       const XMFLOAT3& sunDir, XMFLOAT4X4& outLightVP)
{
    // 8 NDC corners of the full view frustum (used to reconstruct world-space corners)
    XMFLOAT3 ndcCorners[8] = {
        {-1,-1, 0}, { 1,-1, 0}, {-1, 1, 0}, { 1, 1, 0},
        {-1,-1, 1}, { 1,-1, 1}, {-1, 1, 1}, { 1, 1, 1}
    };

    XMMATRIX fullInvVP = XMMatrixInverse(nullptr,
        XMMatrixMultiply(cam.GetViewMatrix(), cam.GetProjMatrix(aspect)));

    // Compute 8 world-space frustum corners
    XMVECTOR corners[8];
    for (int i = 0; i < 8; i++) {
        XMVECTOR c = XMVectorSet(ndcCorners[i].x, ndcCorners[i].y,
            (i < 4) ? 0.0f : 1.0f, 1.0f);
        c = XMVector4Transform(c, fullInvVP);
        c = XMVectorDivide(c, XMVectorSplatW(c));
        corners[i] = c;
    }
    // Sub-frustum corners via lerp with near/far ratios
    float nRatio = nearZ / CAMERA_FAR;
    float fRatio = farZ  / CAMERA_FAR;

    // For a proper cascade we lerp the frustum corners
    // near slice corners = lerp(nearFull, farFull, nRatio)
    // far  slice corners = lerp(nearFull, farFull, fRatio)
    XMVECTOR subCorners[8];
    for (int i = 0; i < 4; i++) {
        subCorners[i]     = XMVectorLerp(corners[i], corners[i+4], nRatio);
        subCorners[i + 4] = XMVectorLerp(corners[i], corners[i+4], fRatio);
    }

    // Compute center of sub-frustum
    XMVECTOR center = XMVectorZero();
    for (auto& c : subCorners) center = XMVectorAdd(center, c);
    center = XMVectorScale(center, 1.0f / 8.0f);

    // Light space orho matrix
    XMVECTOR sunDirV = XMVector3Normalize(
        XMVectorSet(sunDir.x, sunDir.y, sunDir.z, 0));
    XMVECTOR lightPos  = XMVectorSubtract(center, XMVectorScale(sunDirV, 500.0f));
    XMVECTOR up = fabs(XMVectorGetY(sunDirV)) > 0.99f
        ? XMVectorSet(0,0,1,0) : XMVectorSet(0,1,0,0);
    XMMATRIX lightView = XMMatrixLookAtLH(lightPos, center, up);

    // Transform corners to light view space and compute AABB
    float minX =  1e9f, maxX = -1e9f;
    float minY =  1e9f, maxY = -1e9f;
    float minZ =  1e9f, maxZ = -1e9f;
    for (auto& c : subCorners) {
        XMVECTOR lc = XMVector3Transform(c, lightView);
        float lx = XMVectorGetX(lc), ly = XMVectorGetY(lc), lz = XMVectorGetZ(lc);
        minX = std::min(minX, lx); maxX = std::max(maxX, lx);
        minY = std::min(minY, ly); maxY = std::max(maxY, ly);
        minZ = std::min(minZ, lz); maxZ = std::max(maxZ, lz);
    }
    // Extend Z behind light to capture shadow casters above the frustum
    minZ -= TERRAIN_HEIGHT * 2.0f;

    XMMATRIX lightProj = XMMatrixOrthographicOffCenterLH(minX, maxX, minY, maxY, minZ, maxZ);
    XMMATRIX lightVP   = XMMatrixMultiply(lightView, lightProj);
    XMStoreFloat4x4(&outLightVP, XMMatrixTranspose(lightVP));
}

// ---------------------------------------------------------------------------
//  Compute all 3 cascade matrices using practical split scheme
// ---------------------------------------------------------------------------
void ShadowMap::Update(const Camera& camera, float aspect, const XMFLOAT3& dirToSun) {
    // Practical split scheme: blend of logarithmic and uniform
    const float zNear = CAMERA_NEAR, zFar = CAMERA_FAR;
    float splits[NUM_CASCADES + 1];
    splits[0] = zNear;

    for (int i = 1; i <= NUM_CASCADES; i++) {
        float ratio = (float)i / NUM_CASCADES;
        float logSplit    = zNear * powf(zFar / zNear, ratio);
        float linearSplit = zNear + (zFar - zNear) * ratio;
        splits[i] = LAMBDA * logSplit + (1.0f - LAMBDA) * linearSplit;
    }
    // Clamp cascade ranges to reasonable values for a terrain scene
    splits[1] = std::min(splits[1],  80.0f);
    splits[2] = std::min(splits[2], 300.0f);
    splits[3] = std::min(splits[3], zFar);

    for (int i = 0; i < NUM_CASCADES; i++) {
        m_data.cascadeSplits[i] = splits[i + 1];
        ComputeCascadeFrustum(camera, aspect, splits[i], splits[i+1],
                              dirToSun, m_data.lightViewProj[i]);
    }
}

void ShadowMap::UpdateCB(ID3D11DeviceContext* ctx, int cascadeIdx) {
    m_data.cascadeIndex = cascadeIdx;
    m_cb.Update(ctx, m_data);
}

void ShadowMap::UpdateCBAllCascades(ID3D11DeviceContext* ctx) {
    m_data.cascadeIndex = -1;
    m_cb.Update(ctx, m_data);
}

void ShadowMap::BeginCascade(ID3D11DeviceContext* ctx, int cascade) {
    ctx->RSSetState(m_shadowRS.Get());
    ctx->RSSetViewports(1, &m_shadowVP);
    m_cascades[cascade].ClearDepth(ctx, 1.0f);
    ID3D11RenderTargetView* nullRTV = nullptr;
    ctx->OMSetRenderTargets(0, &nullRTV, m_cascades[cascade].DSV());
}

void ShadowMap::EndCascade(ID3D11DeviceContext* ctx) {
    ID3D11RenderTargetView* nullRTV = nullptr;
    ctx->OMSetRenderTargets(0, &nullRTV, nullptr);
}

void ShadowMap::BindSRVs(ID3D11DeviceContext* ctx, UINT startSlot) const {
    for (int i = 0; i < NUM_CASCADES; i++) {
        ID3D11ShaderResourceView* srv = m_cascades[i].SRV();
        ctx->PSSetShaderResources(startSlot + i, 1, &srv);
    }
    // Also bind the shadow comparison sampler at slot 1
    ID3D11SamplerState* samp = m_shadowSampler.Get();
    ctx->PSSetSamplers(1, 1, &samp);
}

void ShadowMap::UnbindSRVs(ID3D11DeviceContext* ctx, UINT startSlot) const {
    ID3D11ShaderResourceView* nullSRV = nullptr;
    for (int i = 0; i < NUM_CASCADES; i++)
        ctx->PSSetShaderResources(startSlot + i, 1, &nullSRV);
}
