# Setup Guide

Step-by-step instructions for building and running Shader Playground from source.

---

## Prerequisites

| Tool | Version | Notes |
|------|---------|-------|
| Visual Studio 2022 | 17.x | Install with **Desktop development with C++** workload |
| Windows SDK | 10.0.22000+ | Included with VS 2022 workload |
| CMake | 3.20+ | Bundled with VS 2022 or download from cmake.org |
| Git | Any | Required for FetchContent to pull dependencies |
| Internet access | — | CMake downloads imgui, glm, stb, FastNoiseLite, tinyexr on first configure |

DirectX 11 runtime is built into Windows 10/11 — no separate SDK install needed.

---

## 1. Clone

```bash
git clone https://github.com/<user>/shader-playground.git
cd shader-playground
```

---

## 2. Configure (CMake)

### Option A — Visual Studio CMake integration (recommended)
1. Open Visual Studio 2022.
2. **File → Open → Folder…** — select the repo root.
3. VS detects `CMakeLists.txt` automatically and runs configure.
4. Wait for "CMake generation finished" in the Output panel.

### Option B — Command line
```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
```

Both options download all dependencies (imgui, glm, stb, FastNoiseLite, tinyexr) via CMake FetchContent on first configure — internet access is required.

---

## 3. Build

### Option A — Visual Studio
- Select the **ShaderPlayground** target from the startup target dropdown.
- Press **F7** (Build) or **F5** (Build + Run).
- Build output: `build/Debug/ShaderPlayground.exe`

### Option B — `build.bat`
```bat
build.bat
```
Runs `cmake --build build --config Debug`.

### Option C — Command line
```bat
cmake --build build --config Debug
cmake --build build --config Release
```

---

## 4. Run

Double-click `build/Debug/ShaderPlayground.exe` or press F5 from Visual Studio.

The application reads `build/Debug/shader_playground.ini` for saved settings (created automatically on first run).

**Working directory must be `build/Debug/`** — the exe expects `../../shaders/` and `../../assets/` relative paths.

---

## 5. Controls

| Key / Input | Action |
|------------|--------|
| W A S D | Move camera |
| Right-click + drag | Look around |
| Q / E | Move down / up |
| Shift | Hold to move faster |
| F5 | Hot-reload all shaders |
| Esc | Quit |

All render settings (exposure, SSAO, bloom, RT reflections, time-of-day, etc.) are exposed in the **ImGui sidebar** at run time.

---

## 6. Shader Hot-Reload

Press **F5** at runtime.  The application calls `D3DCompileFromFile` on every `.hlsl` in `shaders/` and swaps in the new shaders with no restart.  Compile errors are printed to the debug console and the old shaders remain active.

---

## 7. ReShade (optional post-process injection)

ReShade-compatible `.fx` shaders are included in `reshade-shaders/`.

1. Download **ReShade** from [reshade.me](https://reshade.me).
2. Run the installer and point it at `build/Debug/ShaderPlayground.exe`.
3. Select **DirectX 10/11/12**.
4. When prompted for the shader search path, add the repo's `reshade-shaders/` directory.
5. Launch the game; press **Home** to open the ReShade overlay.

---

## 8. Assets

| Path | Contents |
|------|---------|
| `assets/heightmap.png` | 2048×2048 terrain height (R16) |
| `assets/curvature_plan.png` | Florinsky plan curvature map |
| `assets/curvature_slope.png` | Florinsky slope map |
| `assets/sky_day.jpg` | 8K equirectangular day sky |
| `assets/sky_night.jpg` | 8K equirectangular night sky (60MB) |
| `assets/sand_textures/` | 4K PBR: diff, rough, disp, nor_gl (.exr) |
| `assets/snow_textures/` | 4K PBR: diff, rough, disp, nor_gl (.exr), translucent |

All assets are committed to the repository.  The large HDRI `.exr` files (`DaySkyHDRI056B_16K/`, `NightSkyHDRI002_16K/`) in the working tree are **not** used by the runtime and are excluded from git via `.gitignore`.

---

## 9. Troubleshooting

### "Device removed / DXGI_ERROR_DEVICE_REMOVED"
Run with the **D3D11 Debug Layer** enabled:
1. Install **Graphics Tools** optional feature (Windows Settings → Apps → Optional features → Graphics Tools).
2. In `src/D3DContext.cpp`, set `createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG`.
3. Rebuild and check the VS Output window for the detailed D3D error.

### Shader compile errors on startup
- Check that the working directory is `build/Debug/` (relative paths are resolved from there).
- Ensure `shaders/Common.hlsli` exists — it is `#include`d by every shader.

### Black screen / missing textures
- Verify `assets/` is present next to `shaders/`.  Run from `build/Debug/` where the CMake install step copies both folders.

### CMake configure fails / missing dependencies
- Confirm internet access and that Git is in `PATH`.
- Delete `build/_deps/` and re-run CMake to force a clean FetchContent download.

### FetchContent hangs or times out
- Check corporate proxy settings; CMake respects `HTTP_PROXY` / `HTTPS_PROXY` environment variables.

---

## 10. Project Layout

```
shader-playground/
├── CMakeLists.txt        # Build definition, FetchContent deps
├── build.bat             # Convenience build script
├── assets/               # Textures & heightmaps (committed)
├── shaders/              # HLSL source (hot-reloadable at runtime)
├── src/                  # C++ application source
├── reshade-shaders/      # Optional ReShade .fx shaders
├── screenshots/          # Placeholder for screenshots
└── docs/
    ├── architecture.md   # Rendering pipeline reference
    ├── shaders.md        # Per-shader documentation
    └── setup.md          # This file
```
