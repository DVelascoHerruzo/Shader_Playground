#include "Application.h"
#include <algorithm>

// ---------------------------------------------------------------------------
Application::Application()
    : m_ctx()
    , m_renderer(m_ctx)
{
    m_ctx.Init(1280, 720, L"Shader Playground \u2014 DX11");
    m_renderer.Init();

    m_config.Load("shader_playground.ini");

    QueryPerformanceFrequency(&m_freq);
    QueryPerformanceCounter(&m_lastTime);
}

Application::~Application()
{
    m_config.Save("shader_playground.ini");
}

// ---------------------------------------------------------------------------
int Application::Run()
{
    MSG msg = {};
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);

            // Handle resize once the WM_SIZE message is processed
            if (m_ctx.WasResized()) {
                m_renderer.OnResize(m_ctx.Width(), m_ctx.Height());
                m_ctx.ClearResizedFlag();
            }
        } else {
            float dt = ComputeDeltaTime();
            dt = std::min(dt, 0.1f);   // clamp spiral-of-death

            // Global keyboard shortcuts
            if (GetAsyncKeyState(VK_F5)  & 0x0001) m_renderer.HotReloadShaders();
            if (GetAsyncKeyState(VK_F12) & 0x0001) m_renderer.TakeScreenshot();

            m_renderer.Update(dt);
            m_renderer.Render();
        }

        if (m_ctx.ShouldClose()) break;
    }
    return static_cast<int>(msg.wParam);
}

// ---------------------------------------------------------------------------
float Application::ComputeDeltaTime()
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    float dt = static_cast<float>(now.QuadPart - m_lastTime.QuadPart)
             / static_cast<float>(m_freq.QuadPart);
    m_lastTime = now;
    return dt;
}
