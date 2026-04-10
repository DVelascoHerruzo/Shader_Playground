#pragma once
#include "Types.h"
#include "ConstantBuffer.h"

// ---------------------------------------------------------------------------
//  Sun
//  Computes solar position and sky/light colors from a time-of-day value.
//  Uses a simplified equatorial solar model (not geographically accurate but
//  visually correct for a game demo).
// ---------------------------------------------------------------------------
class Sun {
public:
    void Init();
    void Update(float timeOfDay);    // 0.0 – 24.0

    void CreateCB(ID3D11Device* device)  { m_cb.Create(device); }
    void UpdateCB(ID3D11DeviceContext* ctx) { m_cb.Update(ctx, m_data); }
    void AllBindCB(ID3D11DeviceContext* ctx, UINT slot) const { m_cb.AllBind(ctx, slot); }
    const SunLightData& GetData() const { return m_data; }

    // Exposed for UI
    float timeOfDay    = 12.0f;   // 0..24
    float timeSpeed    = 1.0f;    // 0=paused, 1=1 game-hour/real-minute, max 10
    float fogDensity   = 0.0003f;
    float sunIntensity = 1.0f;
    XMFLOAT3 fogColor  = { 0.7f, 0.75f, 0.82f };

private:
    // Convert Kelvin to approximate RGB
    static XMFLOAT3 KelvinToRGB(float K);

    // Compute Preetham zenith/horizon colors for sky
    static XMFLOAT3 ComputeZenithColor(float elevation, float turbidity);
    static XMFLOAT3 ComputeHorizonColor(float elevation, float turbidity);

    SunLightData m_data{};
    ConstantBuffer<SunLightData> m_cb;
};
