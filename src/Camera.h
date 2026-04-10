#pragma once
#include "Types.h"
#include "ConstantBuffer.h"

// ---------------------------------------------------------------------------
//  Camera
//  Free-moving FPS camera. Right-click + drag = look. WASD = move.
//  Shift = 5x speed boost. Scroll = FOV zoom.
// ---------------------------------------------------------------------------
class Camera {
public:
    void Init();
    void Update(float dt, class D3DContext* ctx);

    // Fills in a CameraData struct and uploads to the CB
    void UpdateCB(ID3D11DeviceContext* dxCtx, float aspect, UINT screenW = 0, UINT screenH = 0);
    void CreateCB(ID3D11Device* device) { m_cb.Create(device); }

    void AllBindCB(ID3D11DeviceContext* ctx, UINT slot) const { m_cb.AllBind(ctx, slot); }
    const ConstantBuffer<CameraData>& GetCB() const { return m_cb; }
    const CameraData& GetData() const { return m_data; }

    // Position / orientation accessors
    XMFLOAT3 GetPosition() const { return m_pos; }
    float     GetYaw()     const { return m_yaw; }
    float     GetPitch()   const { return m_pitch; }
    float     GetFOV()     const { return m_fov; }
    void      SetFOV(float f) { m_fov = f; }
    XMVECTOR  GetDirVec()  const;
    XMMATRIX  GetViewMatrix() const;
    XMMATRIX  GetProjMatrix(float aspect) const;

    // For reflection camera (water)
    void SetPositionAndAngles(XMFLOAT3 pos, float yaw, float pitch) {
        m_pos = pos; m_yaw = yaw; m_pitch = pitch;
    }

    // Editable parameters (tweaked via UI)
    float moveSpeed     = 30.0f;
    float sensitivity   = 0.15f;   // degrees per pixel
    float scrollZoomAmt = 2.0f;

private:
    XMFLOAT3 m_pos   = { 0.0f, 150.0f, -500.0f };
    float    m_yaw   = 0.0f;      // degrees, 0 = looking +Z
    float    m_pitch = -20.0f;    // degrees, negative = looking down
    float    m_fov   = 65.0f;     // vertical FOV in degrees

    XMFLOAT3  m_velocity = { 0, 0, 0 };  // smoothed velocity

    ConstantBuffer<CameraData> m_cb;
    CameraData m_data{};
};
