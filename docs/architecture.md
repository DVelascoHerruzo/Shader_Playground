# Rendering Architecture

This document describes the full per-frame render pipeline, how each pass feeds the next, and the GBuffer layout.

---

## Frame Overview

```
Renderer::Render()
 │
 ├─ 1. DoShadowPass()           — fill 3 cascade depth maps
 │
 ├─ 2. DoWaterReflectionPass()  — render scene (above waterline) into half-res reflectionRT
 │
 ├─ 3. DoWaterRefractionPass()  — render scene (below waterline) into half-res refractionRT
 │
 ├─ 4. DoMainPass()
 │    ├─ Sky
 │    ├─ Terrain (tessellated, PCF shadows)
 │    ├─ Foliage (instanced grass)
 │    └─ Water surface  ← reads reflectionRT + refractionRT + sceneDepth
 │       (writes to SceneHDRRT + NormalsRT; read-only DSV)
 │
 ├─ 5. DoRayTracePass()         — heightmap ray march; blends onto SceneHDRRT
 │
 └─ 6. PostProcess::Execute()
      ├─ SSGI pass              → ssgiRT
      ├─ God rays               → goldraysRT
      ├─ Bloom (4-level Kawase) → bloomRT
      ├─ FXAA                   → fxaaRT
      └─ Tonemap (ACES)         → backbuffer (with sceneDepth DSV live for ReShade)
```

---

## Render Targets

| Name | Format | Size | Purpose |
|---|---|---|---|
| `SceneHDRRT` | `R16G16B16A16_FLOAT` | full | HDR scene colour |
| `NormalsRT` | `R16G16B16A16_FLOAT` | full | GBuffer normals (w=1 flags water pixels for RT pass) |
| `SceneDepthRT` | `R32_TYPELESS` | full | Depth: DSV=`D32_FLOAT`, SRV=`R32_FLOAT` |
| `Water::reflectionRT` | `R16G16B16A16_FLOAT` | ½ | Scene above waterline (mirrored camera) |
| `Water::refractionRT` | `R16G16B16A16_FLOAT` | ½ | Scene below waterline (same camera, clip above) |
| `Water::reflDepthRT` | depth | ½ | Depth for reflection pass |
| `Water::refrDepthRT` | depth | ½ | Depth for refraction pass |
| `ShadowMap` (×3) | depth | 2048×2048 | Cascade shadow maps |
| `ssgiRT` | `R16G16B16A16_FLOAT` | full | Screen-space GI contribution |
| `godRaysRT` | `R16G16B16A16_FLOAT` | full | Radial blur sun shafts |
| `bloomRT` (×4) | `R16G16B16A16_FLOAT` | ½…⅛ | Kawase downsample/upsample chain |
| `fxaaRT` | `R8G8B8A8_UNORM` | full | Anti-aliased output before tonemap |

---

## Pass Details

### 1 · Shadow Pass (`DoShadowPass`)
- Updates 3 cascade light view-projection matrices based on camera frustum + sun direction.
- For each cascade: bind cascade DSV, draw terrain with `shadow_ds.hlsl` only (no PS).
- Output: 3 × 2048² depth maps at PS texture slots `t8`, `t9`, `t10`.

### 2 · Water Reflection Pass (`DoWaterReflectionPass`)
- Flips the camera vertically around `SEA_LEVEL` (position: `y' = 2*sea - y`, pitch: `-pitch`).
- Binds `g_clipPlane = (0, 1, 0, -seaLevel+0.1)` → clips fragments below water.
- Renders: sky + terrain into `reflectionRT` (half resolution).
- Output: `Water::m_reflectionRT` bound at water PS slot `t0`.

### 3 · Water Refraction Pass (`DoWaterRefractionPass`)
- Same camera, clips fragments **above** water: `g_clipPlane = (0, -1, 0, seaLevel+0.15)`.
- Renders: sky + terrain into `refractionRT`.
- Output: `Water::m_refractionRT` bound at water PS slot `t1`.

### 4 · Main Pass (`DoMainPass`)
- Full-res viewport; `BeginSceneCapture`: OM = `{SceneHDRRT, NormalsRT}` + `SceneDepthRT.DSV()`.
- Clears HDR, normals, and depth.
- **Sky**: fullscreen triangle; depth write OFF.
- **Terrain**: hull/domain tessellation; PCF 3×3 shadow sampling; biome PBR blending.
  - Terrain PS writes: `SV_Target0` = HDR colour, `SV_Target1` = packed world normal (w=0).
- **Foliage**: instanced grass billboards; alpha discard; wind offset.
- **Water**:
  - Switches to read-only DSV so `SceneDepthRT` can be sampled as SRV simultaneously.
  - OM = `{SceneHDRRT, NormalsRT}` + read-only DSV.
  - Water PS writes: `SV_Target0` = water colour, `SV_Target1` = perturbed normal with **w=1** (water flag).
  - Refraction colour: `g_refractionTex` (t1) × Beer-Lambert absorption.
  - GGX sun specular, subsurface scatter, shore foam.

### 5 · Ray Trace Pass (`DoRayTracePass`)
- Reads `NormalsRT` (t1). GBuffer `w=1` flag identifies water pixels.
- For each water pixel: reconstructs world pos (overrides Y to `g_seaLevel`); reads perturbed normal.
- **Reflection ray**: marches terrain heightmap; sky fallback on miss; Fresnel-weighted.
- **Refraction ray**: Snell's law `air→water`; chromatic aberration (separate R/G/B IOR); Beer-Lambert.
- Output: blended onto `SceneHDRRT` via `SRC_ALPHA / INV_SRC_ALPHA` hardware blend.
- **Critical ordering**: `OMSetRenderTargets` is called *before* `PSSetShaderResources` to release `NormalsRT` from RTV binding — D3D11 silently nulls conflicting SRV/RTV bindings.

### 6 · Post-Process Chain
- **SSGI** (`ssgi_ps`): samples scene normals + depth → screen-space GI bounce colour.
- **God rays** (`godrays_ps`): radial blur centered on projected sun disc.
- **Bloom** (`bloom_ps`): 4-level Kawase downsample then upsample; threshold-gated.
- **FXAA** (`fxaa_ps`): luma-based edge detection + sub-pixel smoothing.
- **Tonemap** (`tonemap_ps`): ACES filmic curve + exposure; writes to backbuffer.
  - SceneDepth DSV is kept bound during tonemap so ReShade GenericDepth can read it.

---

## GBuffer Layout

| RT | Channel | Contents |
|---|---|---|
| `NormalsRT` | `.xyz` | Packed world-space normal: `N * 0.5 + 0.5` |
| `NormalsRT` | `.w` | `0.0` = terrain/foliage, **`1.0` = water surface** (flag for RT pass) |

Only two output targets are used. SSAO was removed; the GBuffer normal RT serves both the RT pass and SSGI.

---

## Constant Buffer Binding Summary

Every shader stage that needs a resource must have the CB bound to that stage.  
The helpers are: `AllBind(ctx, slot)` (VS+HS+DS+PS), `PSBind(ctx, slot)`, `DSBind`, etc.

| Slot | Bound When | Consumers |
|---|---|---|
| b0 `CameraData` | Every pass (persistent) | All shaders |
| b1 `SunLightData` | Every pass (persistent) | Terrain, sky, water, RT, post |
| b2 `ShadowData` | Shadow + main pass | Terrain DS, terrain PS |
| b3 `TerrainData` | Terrain passes | Terrain HS/DS/PS, RT PS |
| b4 `WaterData` | Main + RT + post | Water VS/PS, RT PS, tonemap PS |
| b5 `SSAOData` | SSAO compute | SSAO PS |
| b6 `PostProcessData` | Post chain | FXAA, tonemap, SSGI, bloom |
| b7 `ClipPlaneData` | Reflection + refraction | Terrain DS |
| b9 `RayTraceData` | RT pass | RT PS; terrain PS reads `g_rtEnabled` for shadow |

---

## Water Depth Absorption

Water uses Beer-Lambert extinction to tint refracted colour:

```
depth_under = max(0, sea_level - terrain_world_y)     // vertical metres
extinction  = exp(-depth_under * vec3(0.38, 0.14, 0.06))
final_refr  = refr_col * extinction + deep_col * (1 - extinction)
```

`deep_col = (0.01, 0.06, 0.18)` — dark ocean blue.  
Coefficients approximate clear ocean (R absorbed fastest, B slowest).

---

## Terrain Biome Blending

Height thresholds are normalised: `hNorm = worldPos.y / g_heightScale`, range ≈ `-0.3..0.7`.

| Zone | hNorm range | Notes |
|---|---|---|
| Sand | `< thresholds.x (0.12)` | PBR: sand normal + roughness from EXR |
| Grass | `0.12 → 0.35` | Procedural FBM dirt patches |
| Rock | `0.35 → 0.45` | Height-based band between grass and snow |
| Snow | `> 0.45` | PBR: snow normal + roughness from EXR |

Florinsky curvature maps (plan + profile) drive slope-based rock overlays.

---

## Shader Hot-Reload (F5)

`Shader::HotReload` re-calls `D3DCompile` on the `.hlsl` source.  
On success the new shader objects replace the old ones atomically.  
On failure a MessageBox shows the HLSL error; the old shader stays active.  
All subsystems expose `HotReloadShaders(device)` which `Renderer::HotReloadShaders` calls.
