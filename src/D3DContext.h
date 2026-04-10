#pragma once
#include "Types.h"
#include <imgui.h>

// Forward declare ImGui handler
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

// ---------------------------------------------------------------------------
//  D3DContext
//  Owns: Win32 window, DXGI swap chain, D3D11 device/context, backbuffer RTV.
//  All other systems hold a pointer to the D3DContext to access device/ctx.
// ---------------------------------------------------------------------------
class D3DContext {
public:
    ~D3DContext();

    void Init(int width, int height, const wchar_t* title);

    // Called from WndProc when WM_SIZE fires
    void Resize(UINT w, UINT h);

    // Present the frame. vsync=0 for adaptive/tearing.
    void Present(bool vsync = true);

    // Accessors
    ID3D11Device*           Device()          const { return m_device.Get(); }
    ID3D11DeviceContext*    Context()         const { return m_context.Get(); }
    IDXGISwapChain*         SwapChain()       const { return m_swapChain.Get(); }
    ID3D11RenderTargetView* BackBufferRTV()   const { return m_backBufferRTV.Get(); }
    HWND                    Window()          const { return m_hwnd; }
    UINT                    Width()           const { return m_width; }
    UINT                    Height()          const { return m_height; }
    float                   AspectRatio()     const {
        return m_height ? (float)m_width / (float)m_height : 1.0f;
    }

    // Signals that the window was resized (checked by Application each frame)
    bool WasResized() const { return m_resized; }
    void ClearResizedFlag()   { m_resized = false; }

    // Window quit requested
    bool ShouldClose() const { return m_shouldClose; }
    void RequestClose()       { m_shouldClose = true; }

    // Raw input state (mouse delta for camera)
    void  ConsumeMouseDelta(int& dx, int& dy) { dx = m_mouseDX; dy = m_mouseDY; m_mouseDX = m_mouseDY = 0; }
    bool  IsKeyDown(int vk) const { return (GetAsyncKeyState(vk) & 0x8000) != 0; }
    bool  IsRMBDown()       const { return m_rmbDown; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    void CreateBackBufferRTV();
    void ReleaseBackBufferRTV();

    HWND m_hwnd    = nullptr;
    UINT m_width   = 0;
    UINT m_height  = 0;
    bool m_resized     = false;
    bool m_shouldClose = false;
    bool m_rmbDown     = false;
    int  m_mouseDX = 0, m_mouseDY = 0;
    POINT m_lastMouse = {};

    ComPtr<IDXGISwapChain>          m_swapChain;
    ComPtr<ID3D11Device>            m_device;
    ComPtr<ID3D11DeviceContext>     m_context;
    ComPtr<ID3D11RenderTargetView>  m_backBufferRTV;
};
