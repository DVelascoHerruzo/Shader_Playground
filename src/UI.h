#pragma once
#include "Types.h"
#include "Camera.h"
#include "Sun.h"
#include "TerrainGenerator.h"
#include "SSAO.h"
#include "PostProcess.h"
#include "Water.h"
#include "Foliage.h"
#include "RayTracer.h"
#include <string>
#include <deque>

// ---------------------------------------------------------------------------
//  UI  â€”  ImGui-based user interface. Owns no D3D resources beyond ImGui's.
// ---------------------------------------------------------------------------
class UI {
public:
    void Init(void* hwnd, ID3D11Device* device, ID3D11DeviceContext* ctx);
    void Shutdown();

    void BeginFrame();
    void EndFrame(ID3D11DeviceContext* ctx);

    // Draw all panels. Call between BeginFrame / EndFrame.
    // Returns true if any parameter was changed.
    bool Draw(Camera& cam, Sun& sun,
              TerrainGenerator& terrainGen,
              SSAO& ssao, PostProcess& post,
              Water& water, Foliage& foliage, RayTracer& rayTracer,
              float fps, float frameMs,
              bool& requestHotReload, bool& requestScreenshot,
              bool& requestRegenTerrain, bool& requestWireframe,
              bool& requestRebuildFoliage,
              bool& vsync);

    bool IsFocused() const;

private:
    void DrawEnvironmentPanel (Sun& sun, Water& water);
    void DrawTerrainPanel     (TerrainGenerator& gen, bool& requestRegen);
    void DrawCameraPanel      (Camera& cam);
    void DrawPostProcessPanel (SSAO& ssao, PostProcess& post, bool& vsync);
    void DrawFoliagePanel     (Foliage& foliage, bool& requestRebuild);
    void DrawRayTracerPanel   (RayTracer& rt);
    void DrawPerformancePanel (float fps, float frameMs);
    void DrawReshadeGuidePanel();
    void DrawLoadingOverlay   (const TerrainGenerator& gen);

    std::deque<float> m_frameHistory;  // last 120 frame-times for sparkline
};
