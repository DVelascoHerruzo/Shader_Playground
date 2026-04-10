#pragma once
#include "Types.h"
#include "Shader.h"
#include "RenderTarget.h"
#include "ConstantBuffer.h"
#include "Camera.h"

// ---------------------------------------------------------------------------
//  Water
//  Renders a flat water plane at SEA_LEVEL with reflection and refraction.
//  Reflection = scene re-rendered with camera flipped about water plane.
//  Refraction = scene rendered with clip plane cutting above water.
// ---------------------------------------------------------------------------
class Water {
public:
    void Create(ID3D11Device* device, UINT screenW, UINT screenH);
    void Resize(ID3D11Device* device, UINT w, UINT h);

    // Call once per frame with the current game time for wave animation
    void Update(float totalTime, float waveSpeed, float waveStrength, float ior);

    // Call BEFORE the reflection render pass — sets the reflection clip plane CB
    void BeginReflectionPass(ID3D11DeviceContext* ctx) const;
    void EndReflectionPass(ID3D11DeviceContext* ctx) const;

    // Call BEFORE the refraction render pass
    void BeginRefractionPass(ID3D11DeviceContext* ctx) const;
    void EndRefractionPass(ID3D11DeviceContext* ctx) const;

    // Draw the water quad
    void Draw(ID3D11DeviceContext* ctx,
              ID3D11ShaderResourceView* sceneDepthSRV,
              ID3D11ShaderResourceView* ssaoSRV) const;

    void HotReloadShaders(ID3D11Device* device) { m_shader.HotReload(device); }

    // Reflection camera params (used by Renderer to set up the reflected camera)
    void GetReflectionCamera(const Camera& mainCam,
                             XMFLOAT3& outPos, float& outYaw, float& outPitch) const;

    RenderTarget& ReflectionRT()  { return m_reflectionRT; }
    RenderTarget& RefractionRT()  { return m_refractionRT; }
    RenderTarget& ReflDepthRT()   { return m_reflDepthRT; }
    RenderTarget& RefrDepthRT()   { return m_refrDepthRT; }

    // Public wave parameters – set by UI, read each frame from Renderer
    float waveSpeed     = 0.50f;
    float waveStrength  = 0.018f;
    float ior           = 1.33f;
    float foamThreshold = 0.40f;

    // CB for clip plane control (set by terrain/sky shaders during reflection/refraction passes)
    void BindClipPlaneCB(ID3D11DeviceContext* ctx, UINT slot) const { m_clipPlaneCB.AllBind(ctx, slot); }
    void BindWaterCB(ID3D11DeviceContext* ctx, UINT slot) const     { m_waterCB.AllBind(ctx, slot); }

private:
    void BuildGrid(ID3D11Device* device);

    ShaderProgram m_shader;

    RenderTarget m_reflectionRT;    // Color
    RenderTarget m_reflDepthRT;     // Depth
    RenderTarget m_refractionRT;    // Color
    RenderTarget m_refrDepthRT;     // Depth

    UINT m_indexCount = 0;

    ComPtr<ID3D11Buffer>           m_vb, m_ib;
    ConstantBuffer<WaterData>      m_waterCB;
    ConstantBuffer<ClipPlaneData>  m_clipPlaneCB;

    ComPtr<ID3D11SamplerState>     m_linearSampler;
    ComPtr<ID3D11BlendState>       m_alphaBlend;
    ComPtr<ID3D11DepthStencilState> m_dssDepthTest;
    ComPtr<ID3D11RasterizerState>  m_rsCullNone;   // water visible from above and below

    WaterData m_waterData{};
    UINT m_screenW = 0, m_screenH = 0;
};
