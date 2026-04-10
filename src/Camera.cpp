#include "Camera.h"
#include "D3DContext.h"

void Camera::Init() {
    // Defaults are set in the header initializers
}

XMVECTOR Camera::GetDirVec() const {
    float yawR   = XMConvertToRadians(m_yaw);
    float pitchR = XMConvertToRadians(m_pitch);
    XMVECTOR dir = XMVectorSet(
        cosf(pitchR) * sinf(yawR),
        sinf(pitchR),
        cosf(pitchR) * cosf(yawR),
        0.0f);
    return XMVector3Normalize(dir);
}

XMMATRIX Camera::GetViewMatrix() const {
    XMVECTOR pos     = XMLoadFloat3(&m_pos);
    XMVECTOR forward = GetDirVec();
    XMVECTOR up      = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    return XMMatrixLookToLH(pos, forward, up);
}

XMMATRIX Camera::GetProjMatrix(float aspect) const {
    return XMMatrixPerspectiveFovLH(
        XMConvertToRadians(m_fov), aspect, CAMERA_NEAR, CAMERA_FAR);
}

void Camera::Update(float dt, D3DContext* ctx) {
    // --- Mouse look (right-click held) ---
    if (ctx->IsRMBDown()) {
        int dx, dy;
        ctx->ConsumeMouseDelta(dx, dy);
        m_yaw   += dx * sensitivity;
        m_pitch -= dy * sensitivity;
        m_pitch  = std::clamp(m_pitch, -89.0f, 89.0f);
    }

    // --- Mouse scroll → FOV ---
    // Scroll is handled via WM_MOUSEWHEEL; for simplicity, poll a shared var
    // (We'll drive this via UI as well)

    // --- WASD + QE movement ---
    float yawR = XMConvertToRadians(m_yaw);
    XMVECTOR forward = XMVectorSet(sinf(yawR), 0, cosf(yawR), 0); // Horizontal only
    XMVECTOR right   = XMVectorSet(cosf(yawR), 0,-sinf(yawR), 0);
    XMVECTOR up      = XMVectorSet(0, 1, 0, 0);

    float speed = moveSpeed * (ctx->IsKeyDown(VK_SHIFT) ? 5.0f : 1.0f);
    if (ctx->IsKeyDown(VK_CONTROL)) speed *= 0.2f;

    XMVECTOR targetVel = XMVectorZero();
    if (ctx->IsKeyDown('W')) targetVel = XMVectorAdd(targetVel, XMVectorScale(forward,  speed));
    if (ctx->IsKeyDown('S')) targetVel = XMVectorAdd(targetVel, XMVectorScale(forward, -speed));
    if (ctx->IsKeyDown('A')) targetVel = XMVectorAdd(targetVel, XMVectorScale(right,   -speed));
    if (ctx->IsKeyDown('D')) targetVel = XMVectorAdd(targetVel, XMVectorScale(right,    speed));
    if (ctx->IsKeyDown('Q')) targetVel = XMVectorAdd(targetVel, XMVectorScale(up,       -speed));
    if (ctx->IsKeyDown('E')) targetVel = XMVectorAdd(targetVel, XMVectorScale(up,        speed));

    // Smooth velocity with lerp
    XMVECTOR vel = XMLoadFloat3(&m_velocity);
    vel = XMVectorLerp(vel, targetVel, std::min(1.0f, dt * 12.0f));
    XMStoreFloat3(&m_velocity, vel);

    XMVECTOR pos = XMLoadFloat3(&m_pos);
    pos = XMVectorAdd(pos, XMVectorScale(vel, dt));
    XMStoreFloat3(&m_pos, pos);
}

void Camera::UpdateCB(ID3D11DeviceContext* dxCtx, float aspect, UINT screenW, UINT screenH) {
    XMMATRIX view = GetViewMatrix();
    XMMATRIX proj = GetProjMatrix(aspect);
    XMMATRIX vp   = XMMatrixMultiply(view, proj);

    XMStoreFloat4x4(&m_data.view,       XMMatrixTranspose(view));
    XMStoreFloat4x4(&m_data.proj,       XMMatrixTranspose(proj));
    XMStoreFloat4x4(&m_data.viewProj,   XMMatrixTranspose(vp));
    XMStoreFloat4x4(&m_data.invProj,    XMMatrixTranspose(XMMatrixInverse(nullptr, proj)));
    XMStoreFloat4x4(&m_data.invViewProj,XMMatrixTranspose(XMMatrixInverse(nullptr, vp)));
    m_data.camPos   = m_pos;
    m_data.camDir   = {};
    XMStoreFloat3(&m_data.camDir, GetDirVec());
    m_data.nearPlane    = CAMERA_NEAR;
    m_data.farPlane     = CAMERA_FAR;
    m_data.screenWidth  = (float)screenW;
    m_data.screenHeight = (float)screenH;

    m_cb.Update(dxCtx, m_data);
}
