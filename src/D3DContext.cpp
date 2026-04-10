#include "D3DContext.h"
#include "imgui.h"
#include "imgui_impl_win32.h"

// ImGui Win32 handler (declared in imgui_impl_win32.h but needs explicit inclusion here)

D3DContext::~D3DContext() {
    if (m_context) m_context->ClearState();
    if (m_hwnd)    DestroyWindow(m_hwnd);
    UnregisterClassW(L"ShaderPlaygroundWC", GetModuleHandleW(nullptr));
}

void D3DContext::Init(int width, int height, const wchar_t* title) {
    m_width  = (UINT)width;
    m_height = (UINT)height;

    // ---- Register window class ----
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.hIcon         = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"ShaderPlaygroundWC";
    RegisterClassExW(&wc);

    // ---- Compute window rect so client area == requested size ----
    RECT wr = { 0, 0, (LONG)m_width, (LONG)m_height };
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);

    m_hwnd = CreateWindowExW(0, L"ShaderPlaygroundWC", title,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        wr.right - wr.left, wr.bottom - wr.top,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);

    // Store this pointer in window userdata so WndProc can access it
    SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    ShowWindow(m_hwnd, SW_SHOWDEFAULT);
    UpdateWindow(m_hwnd);

    // ---- Create DXGI swap chain + D3D11 device ----
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferDesc.Width            = m_width;
    sd.BufferDesc.Height           = m_height;
    sd.BufferDesc.RefreshRate      = { 0, 1 };
    sd.BufferDesc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
    sd.BufferDesc.Scaling          = DXGI_MODE_SCALING_UNSPECIFIED;
    sd.SampleDesc                  = { 1, 0 };  // No MSAA on backbuffer (required for flip model)
    sd.BufferUsage                 = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount                 = 2;           // Flip model requires >= 2
    sd.OutputWindow                = m_hwnd;
    sd.Windowed                    = TRUE;
    sd.SwapEffect                  = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.Flags                       = 0;

    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL gotLevel;
    UINT deviceFlags = 0;
#ifdef _DEBUG
    deviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    HR(D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        deviceFlags, featureLevels, 1,
        D3D11_SDK_VERSION, &sd,
        &m_swapChain, &m_device, &gotLevel, &m_context));

    CreateBackBufferRTV();
}

void D3DContext::CreateBackBufferRTV() {
    ComPtr<ID3D11Texture2D> bb;
    HR(m_swapChain->GetBuffer(0, IID_PPV_ARGS(&bb)));
    HR(m_device->CreateRenderTargetView(bb.Get(), nullptr, &m_backBufferRTV));
}

void D3DContext::ReleaseBackBufferRTV() {
    m_backBufferRTV.Reset();
}

void D3DContext::Resize(UINT w, UINT h) {
    if (w == 0 || h == 0) return;
    if (w == m_width && h == m_height) return;

    m_context->ClearState();
    ReleaseBackBufferRTV();

    HR(m_swapChain->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0));

    m_width  = w;
    m_height = h;
    m_resized = true;

    CreateBackBufferRTV();
}

void D3DContext::Present(bool vsync) {
    m_swapChain->Present(vsync ? 1 : 0, 0);
}

LRESULT CALLBACK D3DContext::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    // Forward to ImGui first — guard against ImGui context already being destroyed
    // (happens when DestroyWindow() is called from D3DContext::~D3DContext after UI::Shutdown)
    if (ImGui::GetCurrentContext() && ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp))
        return true;

    D3DContext* ctx = reinterpret_cast<D3DContext*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_SIZE:
        if (ctx && wp != SIZE_MINIMIZED) {
            UINT w = LOWORD(lp), h = HIWORD(lp);
            ctx->Resize(w, h);
        }
        return 0;

    case WM_CLOSE:
        if (ctx) {
            ctx->m_shouldClose = true;
            if (ctx->m_rmbDown) {
                ctx->m_rmbDown = false;
                ReleaseCapture();
                ShowCursor(TRUE);
            }
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_RBUTTONDOWN:
        if (ctx && !ImGui::GetIO().WantCaptureMouse) {
            ctx->m_rmbDown = true;
            SetCapture(hwnd);
            ShowCursor(FALSE);
            GetCursorPos(&ctx->m_lastMouse);
        }
        return 0;

    case WM_RBUTTONUP:
        if (ctx) {
            ctx->m_rmbDown = false;
            ReleaseCapture();
            ShowCursor(TRUE);
            ctx->m_mouseDX = ctx->m_mouseDY = 0;
        }
        return 0;

    case WM_MOUSEMOVE:
        if (ctx && ctx->m_rmbDown) {
            POINT cur;
            GetCursorPos(&cur);
            ctx->m_mouseDX += cur.x - ctx->m_lastMouse.x;
            ctx->m_mouseDY += cur.y - ctx->m_lastMouse.y;
            // Reset cursor to last position for infinite FPS-style look
            SetCursorPos(ctx->m_lastMouse.x, ctx->m_lastMouse.y);
        }
        return 0;

    case WM_KEYDOWN:
        if (wp == VK_ESCAPE && ctx) {
            ctx->m_shouldClose = true;
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
