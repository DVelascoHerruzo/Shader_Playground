#include "UI.h"
#include "RayTracer.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <algorithm>
#include <vector>

// -----------------------------------------------------------------------
void UI::Init(void* hwnd, ID3D11Device* device, ID3D11DeviceContext* ctx)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigWindowsMoveFromTitleBarOnly = true;

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.FrameRounding  = 4.0f;
    style.WindowRounding = 6.0f;
    style.GrabRounding   = 4.0f;

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(device, ctx);
}

void UI::Shutdown()
{
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

void UI::BeginFrame()
{
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // Full-viewport dockspace
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);
    ImGuiWindowFlags dsFlags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDocking;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0,0));
    ImGui::Begin("DockspaceHost", nullptr, dsFlags);
    ImGui::PopStyleVar();
    ImGui::DockSpace(ImGui::GetID("MainDockspace"), ImVec2(0,0),
                     ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End();
}

void UI::EndFrame(ID3D11DeviceContext* /*ctx*/)
{
    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

// -----------------------------------------------------------------------
bool UI::Draw(Camera& cam, Sun& sun,
              TerrainGenerator& terrainGen,
              SSAO& ssao, PostProcess& post,
              Water& water, Foliage& foliage, RayTracer& rayTracer,
              float fps, float frameMs,
              bool& requestHotReload, bool& requestScreenshot,
              bool& requestRegenTerrain, bool& requestWireframe,
              bool& requestRebuildFoliage,
              bool& vsync)
{
    bool changed = false;

    // --- Toolbar -----------------------------------------------------------
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x, 36), ImGuiCond_Always);
    ImGuiWindowFlags tbFlags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoNav;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 4));
    ImGui::Begin("##Toolbar", nullptr, tbFlags);

    if (ImGui::Button("Reload Shaders [F5]"))  requestHotReload = true;
    ImGui::SameLine();
    if (ImGui::Button("Screenshot [F12]"))     requestScreenshot = true;
    ImGui::SameLine();
    if (ImGui::Checkbox("Wireframe", &requestWireframe)) changed = true;
    ImGui::SameLine(0, 20);
    ImGui::TextDisabled(
        "%.1f FPS  %.2f ms  |  RMB=look  WASD/QE=move  Shift=fast  Ctrl=slow",
        fps, frameMs);

    ImGui::End();
    ImGui::PopStyleVar();

    // --- Side panels -------------------------------------------------------
    ImGui::SetNextWindowPos(ImVec2(0, 40), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(310, 580), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Scene Controls")) {
        if (ImGui::CollapsingHeader("Environment", ImGuiTreeNodeFlags_DefaultOpen))
            DrawEnvironmentPanel(sun, water);

        if (ImGui::CollapsingHeader("Terrain", ImGuiTreeNodeFlags_DefaultOpen))
            DrawTerrainPanel(terrainGen, requestRegenTerrain);

        if (ImGui::CollapsingHeader("Camera"))
            DrawCameraPanel(cam);

        if (ImGui::CollapsingHeader("Post-Process"))
            DrawPostProcessPanel(ssao, post, vsync);

        if (ImGui::CollapsingHeader("Foliage"))
            DrawFoliagePanel(foliage, requestRebuildFoliage);

        if (ImGui::CollapsingHeader("Ray Tracing"))
            DrawRayTracerPanel(rayTracer);

        if (ImGui::CollapsingHeader("ReShade Guide"))
            DrawReshadeGuidePanel();
    }
    ImGui::End();

    // --- Performance overlay -----------------------------------------------
    DrawPerformancePanel(fps, frameMs);

    // --- Loading overlay (shown during terrain generation) -----------------
    DrawLoadingOverlay(terrainGen);

    m_frameHistory.push_back(frameMs);
    if (m_frameHistory.size() > 120)
        m_frameHistory.pop_front();

    return changed;
}

// -----------------------------------------------------------------------
void UI::DrawEnvironmentPanel(Sun& sun, Water& water)
{
    // Time of day (updates sun via direct field + Update())
    if (ImGui::SliderFloat("Time of Day", &sun.timeOfDay, 0.0f, 24.0f, "%.2f h"))
        sun.Update(sun.timeOfDay);

    ImGui::SliderFloat("Time Speed", &sun.timeSpeed, 0.0f, 100.0f, "%.2fx");
    ImGui::SameLine();
    ImGui::TextDisabled("(1 = 1h/min)");

    if (ImGui::SliderFloat("Sun Intensity", &sun.sunIntensity, 0.0f, 3.0f, "%.2f"))
        sun.Update(sun.timeOfDay);

    ImGui::Spacing();
    ImGui::TextDisabled("Fog");
    if (ImGui::SliderFloat("Fog Density", &sun.fogDensity, 0.0f, 0.005f, "%.5f"))
        sun.Update(sun.timeOfDay);

    ImGui::Spacing();
    ImGui::TextDisabled("Water");
    ImGui::SliderFloat("Wave Speed",    &water.waveSpeed,    0.0f, 2.0f,  "%.2f");
    ImGui::SliderFloat("Wave Strength", &water.waveStrength, 0.0f, 0.05f, "%.3f");
}

// -----------------------------------------------------------------------
void UI::DrawTerrainPanel(TerrainGenerator& gen, bool& requestRegen)
{
    // First-time prompt: no terrain has been generated yet
    if (!gen.HasTerrain() && !gen.IsRunning()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.2f, 1.0f));
        ImGui::TextWrapped("No terrain loaded. Press the button below to generate one.");
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }

    auto& p = gen.params;
    bool dirty = false;
    dirty |= ImGui::InputInt ("Seed",    &p.seed);
    dirty |= ImGui::SliderInt("Octaves", &p.octaves, 1, 10);
    dirty |= ImGui::SliderFloat("Frequency",   &p.frequency, 0.0005f, 0.01f, "%.5f");
    dirty |= ImGui::SliderFloat("Lacunarity",  &p.lacunarity, 1.5f, 3.5f,    "%.2f");
    dirty |= ImGui::SliderFloat("Gain",        &p.gain, 0.2f, 0.8f,          "%.2f");
    dirty |= ImGui::SliderInt ("Erosion Iters",&p.erosionIters, 5000, 100000);
    dirty |= ImGui::SliderInt ("Sim Scale",    &p.simScale, 1, 10);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(
        "Generate at N x SimScale resolution, then downsample.\n"
        "Higher values = sharper erosion channels.\n"
        "Peak extra RAM: ~%.0f MB (freed after generation)",
        (float)(TerrainGenerator::N * p.simScale) * (TerrainGenerator::N * p.simScale) * 4.0f / (1024 * 1024));
    ImGui::Separator();
    dirty |= ImGui::SliderFloat("Detail Amp",   &p.detailAmplitude, 0.0f, 0.08f, "%.4f");
    dirty |= ImGui::SliderFloat("Detail Freq x",&p.detailFreqScale, 2.0f, 20.0f, "%.1f");

    ImGui::Spacing();
    if (gen.IsRunning()) {
        float pr = gen.Progress();
        ImGui::ProgressBar(pr, ImVec2(-1, 0),
            pr < 0.5f ? "Generating noise..." : "Hydraulic erosion...");
    } else if (ImGui::Button("Regenerate Terrain", ImVec2(-1, 0))) {
        gen.Generate(p);
        requestRegen = true;
    }
}

// -----------------------------------------------------------------------
void UI::DrawCameraPanel(Camera& cam)
{
    ImGui::SliderFloat("Move Speed",  &cam.moveSpeed,   1.0f, 500.0f, "%.1f");
    ImGui::SliderFloat("Sensitivity", &cam.sensitivity, 0.05f, 0.5f,  "%.3f");

    float fov = cam.GetFOV();
    if (ImGui::SliderFloat("FOV", &fov, 30.0f, 110.0f, "%.0f deg"))
        cam.SetFOV(fov);

    auto pos = cam.GetPosition();
    ImGui::LabelText("Position",   "%.1f  %.1f  %.1f",  pos.x, pos.y, pos.z);
    ImGui::LabelText("Yaw/Pitch",  "%.1f  /  %.1f", cam.GetYaw(), cam.GetPitch());
}

// -----------------------------------------------------------------------
void UI::DrawPostProcessPanel(SSAO& ssao, PostProcess& post, bool& vsync)
{
    ImGui::TextDisabled("Tone-mapping");
    PostProcessData& pp = post.Params();
    ImGui::SliderFloat("Exposure", &pp.exposure, 0.1f, 4.0f, "%.2f");
    ImGui::Checkbox("FXAA", reinterpret_cast<bool*>(&pp.fxaaEnabled));

    ImGui::Spacing();
    ImGui::TextDisabled("Performance");
    ImGui::Checkbox("VSync", &vsync);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Disable to uncap frame rate (may cause tearing).");
}

// -----------------------------------------------------------------------
void UI::DrawFoliagePanel(Foliage& foliage, bool& requestRebuild)
{
    ImGui::Checkbox("Enabled###FoliageEnabled", &foliage.enabled);

    ImGui::Spacing();
    ImGui::TextDisabled("Wind");
    ImGui::SliderFloat("Wind Strength",  &foliage.windStrength,  0.0f, 3.0f,  "%.2f");
    ImGui::SliderFloat("Wind Frequency", &foliage.windFrequency, 0.1f, 5.0f,  "%.2f");

    ImGui::Spacing();
    ImGui::TextDisabled("Visibility");
    ImGui::SliderFloat("Draw Distance",  &foliage.drawDistance,  20.0f, 300.0f, "%.0f m");

    ImGui::Spacing();
    ImGui::TextDisabled("Density (press Rebuild to apply)");
    ImGui::SliderInt("Max Clumps", &foliage.maxInstances, 1000, 100000, "%d");
    if (ImGui::Button("Rebuild Foliage", ImVec2(-1, 0)))
        requestRebuild = true;

    ImGui::Spacing();
    ImGui::Text("Active: %u clumps", foliage.GetInstanceCount());
}

// -----------------------------------------------------------------------
void UI::DrawRayTracerPanel(RayTracer& rt)
{
    ImGui::Checkbox("Enable Ray Tracing###RTEnabled", &rt.enabled);

    if (!rt.enabled) {
        ImGui::Spacing();
        ImGui::TextDisabled("When enabled: replaces shadow maps with");
        ImGui::TextDisabled("ray-marched soft shadows + water reflections.");
        return;
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Sun Shadows");
    ImGui::SliderInt("Shadow Steps##rtsh", &rt.shadowSteps, 4, 64);
    ImGui::TextDisabled("Higher = softer penumbra, more accurate.");

    ImGui::Spacing();
    ImGui::TextDisabled("Water Effects");
    ImGui::Checkbox("Water Reflections###RTRefl",  &rt.reflectEnabled);
    ImGui::Checkbox("Water Refraction###RTRefr",   &rt.refractEnabled);

    ImGui::Spacing();
    ImGui::TextDisabled("Quality");
    ImGui::SliderInt("Reflection Steps##rtmarch", &rt.numSteps,     16, 256);
    ImGui::SliderFloat("Max Distance##rtdist",    &rt.maxDistance, 50.0f, 600.0f, "%.0f m");
    ImGui::Spacing();
    ImGui::TextDisabled("Tip: F5 hot-reloads the ray march shader.");
}

// -----------------------------------------------------------------------
void UI::DrawPerformancePanel(float fps, float frameMs)
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 220, 44), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(216, 90), ImGuiCond_Always);
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoNav;
    ImGui::SetNextWindowBgAlpha(0.65f);
    if (ImGui::Begin("##Perf", nullptr, flags)) {
        ImGui::Text("%.1f FPS  /  %.2f ms", fps, frameMs);
        if (!m_frameHistory.empty()) {
            std::vector<float> v(m_frameHistory.begin(), m_frameHistory.end());
            float maxV = *std::max_element(v.begin(), v.end());
            ImGui::PlotLines("##ft", v.data(), (int)v.size(),
                             0, nullptr, 0.0f, std::max(maxV, 33.0f), ImVec2(-1, 40));
        }
    }
    ImGui::End();
}

// -----------------------------------------------------------------------
void UI::DrawReshadeGuidePanel()
{
    ImGui::TextWrapped("To test your own ReShade .fx shaders:");
    ImGui::Spacing();
    ImGui::BulletText("Build project (cmake --build).");
    ImGui::BulletText("Run ReShade installer, select ShaderPlayground.exe.");
    ImGui::BulletText("Choose 'Direct3D 11'.");
    ImGui::BulletText("Place .fx files in reshade-shaders/Shaders/");
    ImGui::BulletText("Press Home in-game to open the ReShade overlay.");
    ImGui::Spacing();
    ImGui::TextDisabled("Depth buffer:");
    ImGui::TextWrapped(
        "Scene depth is bound at Present-time on the default DSV.\n"
        "Select the matching resolution in ReShade's generic_depth addon.\n"
        "Forward-Z: near=0.3, far=3000.");
}

// -----------------------------------------------------------------------
void UI::DrawLoadingOverlay(const TerrainGenerator& gen)
{
    if (!gen.IsRunning()) return;

    ImGuiIO& io = ImGui::GetIO();
    const float w = 340.0f, h = 72.0f;
    ImGui::SetNextWindowPos(
        ImVec2((io.DisplaySize.x - w) * 0.5f, (io.DisplaySize.y - h) * 0.5f),
        ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.88f);
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoDocking    | ImGuiWindowFlags_NoNav  |
        ImGuiWindowFlags_NoInputs     | ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("##LoadingOverlay", nullptr, flags)) {
        float pr = gen.Progress();
        const char* stage = pr < 0.2f ? "Building noise\xe2\x80\xa6"
                          : pr < 0.9f ? "Hydraulic erosion\xe2\x80\xa6"
                          :             "Finalizing terrain\xe2\x80\xa6";
        ImGui::SetCursorPosX((w - ImGui::CalcTextSize("Generating Terrain").x) * 0.5f);
        ImGui::TextUnformatted("Generating Terrain");
        ImGui::Spacing();
        ImGui::ProgressBar(pr, ImVec2(-1.0f, 0.0f), stage);
    }
    ImGui::End();
}

// -----------------------------------------------------------------------
bool UI::IsFocused() const
{
    return ImGui::GetIO().WantCaptureMouse ||
           ImGui::GetIO().WantCaptureKeyboard;
}
