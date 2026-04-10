# Shader Playground — DirectX 11 Terrain & Water Renderer

A real-time forward renderer built in **C++ / DirectX 11 / HLSL** as a graphics portfolio project.  Showcases physically-based rendering techniques: GPU-tessellated terrain, screen-space heightmap ray tracing for water reflections/refractions, Beer-Lambert water absorption, cascaded shadow maps, procedural sky, post-process effects, and a live ImGui editor.

![screenshot placeholder](screenshots/.gitkeep)

---

## Feature Highlights

| System | Details |
|---|---|
| **Terrain** | Async domain-warped FBM + hydraulic erosion; GPU hull/domain shader tessellation; Florinsky-based procedural biome blending (sand → grass → rock → snow); height-based rock band; PCF cascaded shadow maps; supersampled generation |
| **Water** | Reflection + refraction pre-render passes; Gerstner wave vertex displacement; perturbed ripple normals; Beer-Lambert depth absorption; GGX specular; shore foam; Fresnel blend |
| **RT Reflections** | Screen-space heightmap ray marching (raytrace_ps) overlaid on water surface; chromatic-aberration refraction; Fresnel-weighted alpha blend; sky miss fallback |
| **Sky** | Equirectangular day/night panorama blend; procedural sun disc + corona; time-of-day arc |
| **Shadows** | 3-cascade CSM; 2048 × 2048 per cascade; slope-scaled depth bias; 3×3 PCF |
| **Post-process** | SSGI (screen-space GI); god rays (radial blur); 4-level Kawase bloom; FXAA; ACES filmic tonemap; exposure; underwater tint |
| **Foliage** | Instanced grass billboards over the grass biome; wind animation; alpha-discard no-sort |
| **SSAO** | Half-res hemisphere SSAO; bilateral depth-aware blur (kept in code, toggleable) |
| **UI** | ImGui dockspace; live sliders for every parameter; hot shader reload (F5); screenshot (F12) |
| **ReShade** | Depth buffer bound at Present; forward-Z (near=0.3, far=3000); GenericDepth compatible |

---

## Download (pre-built)

> Just want to run it? Grab the latest zip from [**Releases**](../../releases/latest).

1. Download `ShaderPlayground-vX.Y-win64.zip`
2. Unzip anywhere — no installer needed
3. Double-click `ShaderPlayground.exe`

**Requirements:** Windows 10/11 x64 · DirectX 11 GPU (any GPU from ~2010 onward)

---

## Requirements (build from source)

- **OS**: Windows 10 or 11 (x64)
- **GPU**: DirectX 11 Feature Level 11.0 (any GPU from ~2010 onward)
- **Visual Studio 2022** with the **Desktop development with C++** workload  
  (installs MSVC compiler, CMake, and Windows SDK automatically)
- **Git** (for cloning and FetchContent dependency downloads during configure)
- No other dependencies — everything else is downloaded by CMake at configure time

---

## Quick Start

```cmd
git clone https://github.com/<you>/shader-playground.git
cd shader-playground

:: Configure (downloads imgui, glm, stb, FastNoiseLite, tinyexr via FetchContent)
cmake -B build -S .

:: Build Debug
cmake --build build --config Debug

:: Run
build\Debug\ShaderPlayground.exe
```

> **Or use `build.bat`** in the root — it calls the same cmake commands.

The executable is placed in `build/Debug/` (or `build/Release/`).  
Shaders and textures are automatically copied next to the exe at build time.

---

## Controls

| Input | Action |
|---|---|
| **RMB + drag** | Look around |
| **W A S D** | Fly forward / back / left / right |
| **Q / E** | Move down / up |
| **Shift** | 5× speed |
| **Ctrl** | 0.2× precision |
| **F5** | Hot-reload all HLSL shaders without restarting |
| **F12** | Save `screenshots/screenshot_XXXX.png` |
| **Esc** | Exit |

---

## Project Structure

```
shader-playground/
│
├── CMakeLists.txt          # Build system: FetchContent deps, sources, post-build copies
├── build.bat               # Convenience wrapper around cmake configure + build
├── .gitignore
├── README.md
│
├── src/                    # C++ source (all subsystems)
│   ├── Types.h             # Shared structs: all CB layouts, vertex types, constants
│   ├── ConstantBuffer.h    # Typed D3D11 constant buffer wrapper (templated)
│   ├── D3DContext.h/cpp    # Win32 window, DX11 device/swapchain, raw input
│   ├── RenderTarget.h/cpp  # Colour RT + depth/stencil RT helpers
│   ├── Shader.h/cpp        # VS/PS/HS/DS compiler wrapper, hot-reload (F5)
│   ├── Camera.h/cpp        # FPS-style fly camera with smooth velocity
│   ├── Sun.h/cpp           # Directional light, sky colour presets, time-of-day
│   ├── TerrainGenerator.h/cpp  # CPU async heightmap: domain-warp FBM + erosion
│   ├── Terrain.h/cpp       # Tessellated patch grid VB/IB, main + shadow shaders
│   ├── ShadowMap.h/cpp     # 3-cascade CSM: frustum split, light matrices
│   ├── Sky.h/cpp           # Equirectangular panorama + sun disc pass
│   ├── Water.h/cpp         # Reflection/refraction RT capture + water mesh
│   ├── Foliage.h/cpp       # Instanced grass billboards from terrain heightmap
│   ├── SSAO.h/cpp          # Half-res hemisphere SSAO + bilateral blur
│   ├── RayTracer.h/cpp     # SS heightmap ray marcher (water reflections/refractions)
│   ├── PostProcess.h/cpp   # SSGI, god rays, Kawase bloom, FXAA, ACES tonemap
│   ├── Renderer.h/cpp      # Per-frame pass orchestrator
│   ├── Application.h/cpp   # Win32 message loop, timing, resize handling
│   ├── Config.h/cpp        # INI key/value persistence (auto-saves on exit)
│   ├── UI.h/cpp            # ImGui panels for every subsystem
│   ├── EXRLoader.cpp       # tinyexr single-TU implementation (EXR normal maps)
│   └── main.cpp            # wWinMain entry point
│
├── shaders/                # HLSL — compiled at runtime by D3DCompiler (no precompile)
│   ├── Common.hlsli        # All CB declarations + utility functions (LinearDepth, fog, etc.)
│   ├── terrain_vs.hlsl     # Terrain: patch grid vertex shader
│   ├── terrain_hs.hlsl     # Terrain: hull shader (screen-space LOD)
│   ├── terrain_ds.hlsl     # Terrain: domain shader (heightmap displacement + clip plane)
│   ├── terrain_ps.hlsl     # Terrain: PBR biome blending, CSM shadows, sand/snow PBR
│   ├── shadow_ds.hlsl      # Shadow pass: simplified domain shader (no PS)
│   ├── sky_ps.hlsl         # Sky: equirectangular sample + sun disc/glow
│   ├── water_vs.hlsl       # Water: Gerstner wave vertex displacement
│   ├── water_ps.hlsl       # Water: Beer-Lambert, GGX specular, foam, GBuffer normal flag
│   ├── raytrace_ps.hlsl    # Water RT: heightmap ray march (reflect + chromatic refract)
│   ├── grass_vs.hlsl       # Foliage: instanced billboard vertex shader
│   ├── grass_ps.hlsl       # Foliage: alpha discard + wind tint
│   ├── ssao_ps.hlsl        # SSAO: hemisphere sampling in view space
│   ├── ssao_blur_ps.hlsl   # SSAO: bilateral Gaussian blur
│   ├── ssgi_ps.hlsl        # SSGI: screen-space indirect illumination
│   ├── godrays_ps.hlsl     # God rays: radial blur from sun disc
│   ├── bloom_ps.hlsl       # Bloom: 4-level Kawase downsample + upsample
│   ├── fxaa_ps.hlsl        # FXAA: fast approximate anti-aliasing
│   ├── tonemap_ps.hlsl     # Tonemap: ACES filmic + exposure
│   └── fullscreen_vs.hlsl  # Shared no-VB fullscreen triangle (SV_VertexID trick)
│
├── assets/
│   ├── textures/           # Base terrain/sky textures (grass, dirt, mud, rock, snow, sky)
│   ├── sand_textures/      # 4K PBR sand set: diff, rough, disp, nor_gl (EXR)
│   └── snow_textures/      # 4K PBR snow set: diff, rough, disp, nor_gl (EXR), translucent
│
├── reshade-shaders/        # Drop .fx files here; ReShade picks them up automatically
├── screenshots/            # F12 output (gitignored, folder tracked)
└── docs/
    ├── architecture.md     # Rendering pipeline, pass order, CB layout
    ├── shaders.md          # Per-shader documentation
    └── setup.md            # Detailed build + ReShade setup guide
```

---

## Constant Buffer Slots

| Slot | Struct | Bound in |
|---|---|---|
| b0 | `CameraData` | View/proj, invViewProj, camPos, near/far, screen size |
| b1 | `SunLightData` | Sun dir+color, ambient, fog, sky gradients, time-of-day |
| b2 | `ShadowData` | 3 light VP matrices, cascade split depths |
| b3 | `TerrainData` | Height scale, world size, biome thresholds |
| b4 | `WaterData` | Sea level, wave speed/strength, IOR, foam |
| b5 | `SSAOData` | 64 hemisphere samples, radius, bias, intensity |
| b6 | `PostProcessData` | Exposure, bloom, god rays, FXAA, SSAO, SSGI toggles |
| b7 | `ClipPlaneData` | Reflection/refraction clip plane (water pre-passes) |
| b9 | `RayTraceData` | RT enabled, reflect/refract flags, steps, max distance |

> Struct layouts are in `src/Types.h`.  
> All shader CB declarations are in `shaders/Common.hlsli`.  
> The two must match exactly (no padding surprises — verified by C++ `static_assert`).

---

## ReShade Integration

1. Build the project.
2. Run the [ReShade installer](https://reshade.me/), point it at `build/Debug/ShaderPlayground.exe`, choose **Direct3D 11**.
3. Drop `.fx` files into `reshade-shaders/Shaders/` and textures into `reshade-shaders/Textures/`.
4. Press **Home** in-game to open the ReShade overlay.
5. In the **Add-ons** tab → enable **Generic Depth** → select the depth buffer (`R32_FLOAT`, same resolution as the window).

Depth format: `R32_TYPELESS` (DSV = `D32_FLOAT`, SRV = `R32_FLOAT`).  
The depth buffer is bound alongside the backbuffer at Present time for full compatibility.

---

## Docs

| File | Contents |
|---|---|
| [docs/architecture.md](docs/architecture.md) | Full frame render order, how each pass feeds the next |
| [docs/shaders.md](docs/shaders.md) | What every HLSL file does, inputs/outputs |
| [docs/setup.md](docs/setup.md) | Step-by-step setup including prerequisites and troubleshooting |

---

## License

MIT — see [LICENSE](LICENSE) if present, otherwise use freely with attribution.
