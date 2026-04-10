#include "Water.h"
#include <cmath>

void Water::Create(ID3D11Device* device, UINT screenW, UINT screenH) {
    m_screenW = screenW;
    m_screenH = screenH;

    BuildGrid(device);

    m_shader.Create(device,
        L"shaders/water_vs.hlsl", "VSMain",
        L"shaders/water_ps.hlsl", "PSMain");

    D3D11_INPUT_ELEMENT_DESC elems[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    m_shader.CreateInputLayout(device, elems, 2);

    // Reflection / refraction RTs at half resolution for performance
    UINT halfW = std::max(1u, screenW / 2);
    UINT halfH = std::max(1u, screenH / 2);
    m_reflectionRT.Create(device, halfW, halfH, DXGI_FORMAT_R16G16B16A16_FLOAT);
    m_reflDepthRT.CreateDepth(device, halfW, halfH);
    m_refractionRT.Create(device, halfW, halfH, DXGI_FORMAT_R16G16B16A16_FLOAT);
    m_refrDepthRT.CreateDepth(device, halfW, halfH);

    m_waterCB.Create(device);
    m_clipPlaneCB.Create(device);

    // Linear wrap sampler
    D3D11_SAMPLER_DESC sd{};
    sd.Filter   = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.MaxLOD   = D3D11_FLOAT32_MAX;
    HR(device->CreateSamplerState(&sd, &m_linearSampler));

    // Default depth-stencil
    D3D11_DEPTH_STENCIL_DESC dsd{};
    dsd.DepthEnable    = TRUE;
    dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dsd.DepthFunc      = D3D11_COMPARISON_LESS;
    HR(device->CreateDepthStencilState(&dsd, &m_dssDepthTest));

    // Rasterizer: cull-none so water is visible from above and below the surface
    D3D11_RASTERIZER_DESC rrd{};
    rrd.FillMode        = D3D11_FILL_SOLID;
    rrd.CullMode        = D3D11_CULL_NONE;
    rrd.DepthClipEnable = TRUE;
    HR(device->CreateRasterizerState(&rrd, &m_rsCullNone));

    // Default water data
    m_waterData.seaLevel      = SEA_LEVEL;
    m_waterData.waveSpeed     = 0.05f;
    m_waterData.waveStrength  = 0.02f;
    m_waterData.foamThreshold = 0.8f;
    m_waterData.ior           = 1.33f;
    m_waterData.reflectBias   = 0.1f;
}

void Water::Resize(ID3D11Device* device, UINT w, UINT h) {
    m_screenW = w; m_screenH = h;
    UINT halfW = std::max(1u, w / 2);
    UINT halfH = std::max(1u, h / 2);
    m_reflectionRT.Resize(device, halfW, halfH);
    m_reflDepthRT.CreateDepth(device, halfW, halfH);
    m_refractionRT.Resize(device, halfW, halfH);
    m_refrDepthRT.CreateDepth(device, halfW, halfH);
}

void Water::BuildGrid(ID3D11Device* device) {
    // 64x64 subdivided flat grid — VS displaces Y via Gerstner waves
    const int G  = 64;
    const float hs   = TERRAIN_WORLD_SIZE * 0.6f;
    const float step = (2.0f * hs) / G;

    std::vector<WaterVertex> verts;
    verts.reserve((G + 1) * (G + 1));
    for (int z = 0; z <= G; ++z) {
        for (int x = 0; x <= G; ++x) {
            float wx = -hs + x * step;
            float wz = -hs + z * step;
            verts.push_back({{ wx, SEA_LEVEL, wz }, { (float)x / G, (float)z / G }});
        }
    }

    std::vector<uint32_t> idx;
    idx.reserve(G * G * 6);
    for (int z = 0; z < G; ++z) {
        for (int x = 0; x < G; ++x) {
            uint32_t bl = z * (G + 1) + x;
            uint32_t br = bl + 1;
            uint32_t tl = (z + 1) * (G + 1) + x;
            uint32_t tr = tl + 1;
            idx.push_back(bl); idx.push_back(br); idx.push_back(tl);
            idx.push_back(tl); idx.push_back(br); idx.push_back(tr);
        }
    }
    m_indexCount = (UINT)idx.size();

    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = (UINT)(verts.size() * sizeof(WaterVertex));
    bd.Usage     = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA sr{ verts.data() };
    HR(device->CreateBuffer(&bd, &sr, &m_vb));

    bd.ByteWidth = (UINT)(idx.size() * sizeof(uint32_t));
    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    sr.pSysMem   = idx.data();
    HR(device->CreateBuffer(&bd, &sr, &m_ib));
}

void Water::Update(float totalTime, float waveSpeed, float waveStrength, float ior) {
    m_waterData.time         = totalTime;
    m_waterData.waveSpeed    = waveSpeed;
    m_waterData.waveStrength = waveStrength;
    m_waterData.ior          = ior;
}

void Water::BeginReflectionPass(ID3D11DeviceContext* ctx) const {
    // Clip: discard fragments BELOW water plane (y - seaLevel < 0)
    ClipPlaneData cp;
    cp.clipPlane = { 0.0f, 1.0f, 0.0f, -SEA_LEVEL + 0.1f };
    // NOTE: m_clipPlaneCB is mutable logically — cast away
    const_cast<ConstantBuffer<ClipPlaneData>&>(m_clipPlaneCB)
        .Update(ctx, cp);
}
void Water::EndReflectionPass(ID3D11DeviceContext*) const {}

void Water::BeginRefractionPass(ID3D11DeviceContext* ctx) const {
    ClipPlaneData cp;
    cp.clipPlane = { 0.0f, -1.0f, 0.0f, SEA_LEVEL + 0.15f };
    const_cast<ConstantBuffer<ClipPlaneData>&>(m_clipPlaneCB)
        .Update(ctx, cp);
}
void Water::EndRefractionPass(ID3D11DeviceContext*) const {}

void Water::GetReflectionCamera(const Camera& mainCam,
                                XMFLOAT3& outPos, float& outYaw, float& outPitch) const {
    XMFLOAT3 cp = mainCam.GetPosition();
    outPos   = { cp.x, 2.0f * SEA_LEVEL - cp.y, cp.z };
    outYaw   = mainCam.GetYaw();
    outPitch = -mainCam.GetPitch();
}

void Water::Draw(ID3D11DeviceContext* ctx,
                 ID3D11ShaderResourceView* sceneDepthSRV,
                 ID3D11ShaderResourceView* ssaoSRV) const
{
    // Bind WaterData CB
    const_cast<ConstantBuffer<WaterData>&>(m_waterCB).Update(ctx, m_waterData);
    m_waterCB.AllBind(ctx, 4);

    // Bind reflection / refraction textures
    ID3D11ShaderResourceView* srvs[4] = {
        m_reflectionRT.SRV(),
        m_refractionRT.SRV(),
        sceneDepthSRV,
        ssaoSRV,
    };
    ctx->PSSetShaderResources(0, 4, srvs);

    ID3D11SamplerState* samp = m_linearSampler.Get();
    ctx->PSSetSamplers(0, 1, &samp);

    ctx->RSSetState(m_rsCullNone.Get());
    ctx->OMSetDepthStencilState(m_dssDepthTest.Get(), 0);

    UINT stride = sizeof(WaterVertex), offset = 0;
    ID3D11Buffer* vb = m_vb.Get();
    ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
    ctx->IASetIndexBuffer(m_ib.Get(), DXGI_FORMAT_R32_UINT, 0);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    m_shader.Bind(ctx);
    ctx->DrawIndexed(m_indexCount, 0, 0);

    // Unbind SRVs to avoid resource hazards
    ID3D11ShaderResourceView* nullSRVs[4] = {};
    ctx->PSSetShaderResources(0, 4, nullSRVs);
    ctx->OMSetDepthStencilState(nullptr, 0);
    ctx->RSSetState(nullptr);
}
