#pragma once
#include "Types.h"
#include "Renderer.h"
#include "Config.h"

// ---------------------------------------------------------------------------
//  Application
//  Win32 message loop, timing, F5 hot-reload, F12 screenshot.
// ---------------------------------------------------------------------------
class Application {
public:
    explicit Application();
    ~Application();

    int Run();   // returns process exit code

private:
    D3DContext m_ctx;
    Renderer   m_renderer;
    Config     m_config;

    // High-resolution timer
    LARGE_INTEGER m_freq     = {};
    LARGE_INTEGER m_lastTime = {};

    float ComputeDeltaTime();
};
