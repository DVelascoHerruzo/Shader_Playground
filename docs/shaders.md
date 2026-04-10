# Shader Reference

Every HLSL file in the `shaders/` directory, what it does, and its inputs/outputs.

---

## Shared Header

### `Common.hlsli`
Included by every shader.  Contains:
- All constant buffer declarations (`CameraData` b0 through `RayTraceData` b9).
- Utility functions: `LinearDepth`, `ReconstructViewPos`, `ReconstructWorldPos`, `ApplyFog`.
- `static const float PI`.
- Must stay byte-identical to the C++ structs in `src/Types.h`.

---

## Fullscreen Utilities

### `fullscreen_vs.hlsl`
- **Entry**: `VSMain` (vs_5_0)
- Uses `SV_VertexID` (0/1/2) to generate a covering triangle — **no vertex buffer needed**.
- Outputs `SV_Position` + `TEXCOORD0` UV.
- Shared by: sky, SSAO, SSGI, god rays, bloom, FXAA, tonemap, RT pass.

---

## Terrain

### `terrain_vs.hlsl`
- **Entry**: `VSMain`
- Passes patch corner world XZ positions (and UVs) through to the hull stage.
- No per-vertex displacement here — that happens in the domain shader.

### `terrain_hs.hlsl`
- **Entry**: `HSMain` (constant function: `HSConstant`)
- Screen-space adaptive LOD: tessellation factor ∝ patch size on screen.
- Clamps to `[1, 64]` segments per edge.

### `terrain_ds.hlsl` — Domain Shader
- **Entry**: `DSMain`
- Samples heightmap `g_heightmap` (t0) to displace vertex Y.
- Computes analytical terrain normal from central differences on the heightmap.
- Outputs: `SV_Position`, `worldPos`, `normal`, `shadowUV[3]` (for CSM), `SV_ClipDistance0` (water clip plane).

### `terrain_ps.hlsl` — Terrain Pixel Shader
- **Entry**: `PSMain` → `PSOut { SV_Target0 hdrColor, SV_Target1 gbNormal }`
- **Biome blending**: sand (near sea level), grass (FBM dirt patches), height-based rock band, snow.
- **PBR sand/snow**: samples `sand_nor.exr` + `sand_rough.jpg` / `snow_nor.exr` + `snow_rough.jpg`.
- **Florinsky maps** (t1, t2): plan curvature → rock/wet overlays.
- **CSM shadows**: PCF 3×3 across 3 cascade depth maps (t8, t9, t10).
- **RT shadow mode**: when `g_rtEnabled`, terrain does heightmap ray march to sun instead of CSM.
- Writes `gbNormal.w = 0.0` (terrain, not water).

### `shadow_ds.hlsl` — Shadow-Pass Domain Shader
- Simplified version of `terrain_ds` that outputs only `SV_Position` projected into light space.
- No colour output (no PS bound for shadow passes).

---

## Sky

### `sky_ps.hlsl`
- **Entry**: `PSMain`
- Samples `g_skyboxDay` (panoramic equirec JPEG) and `g_skyboxNight` separately.
- Blends day/night via `g_timeOfDay` (sun elevation).
- Overlays sun disc and multi-ring glow keyed on `dot(viewDir, g_dirToSun)`.
- Writes infinite depth (`SV_Depth = 1.0`) so terrain always renders on top.

---

## Water

### `water_vs.hlsl`
- **Entry**: `VSMain`
- Applies **Gerstner wave** displacement (8 waves, 4 each in two wave trains) to vertex Y (and X/Z for rolling motion).
- Computes per-vertex normal from wave derivatives.
- Outputs: `worldPos`, `waveNormal`, screen UV for refraction/reflection sampling.

### `water_ps.hlsl`
- **Entry**: `PSMain` → `PSOut { SV_Target0 hdrColor, SV_Target1 gbNormal }`
- **Refraction**: samples `g_refractionTex` (t1) at screen UV + wave distortion.
- **Beer-Lambert absorption**: `depth_under = seaLevel - terrainWorld.y` (vertical column, not view-space Z).
- **GGX specular** (Cook-Torrance) with roughness driven by wave strength.
- **Subsurface scatter** approximation: teal tint at lit shallow edges.
- **Shore foam**: animated alpha blend near shoreline.
- **Fog**: atmospheric perspective.
- Writes `gbNormal.w = 1.0` — the **water flag** read by `raytrace_ps` to detect water pixels.

---

## Ray Tracer

### `raytrace_ps.hlsl`
- **Entry**: `PSMain` (return `SV_Target` only — blended via hardware over `SceneHDRRT`)
- **Reads**: `g_rtDepth` (t0), `g_rtNormals` (t1), `g_rtHeightmap` (t2).
- **Water detection**: reads GBuffer normal's `.w`; skips non-water pixels (returns alpha=0).
- **Reflection**:
  - Computes `reflect(viewDir, N)`.
  - `MarchTerrain`: linear scan + 8-step binary refinement on `g_rtHeightmap`.
  - Hit: shade with `TerrainAlbedo` + diffuse NdotL; fade to sky at max range.
  - Miss: `SkyColor(reflDir)`.
  - Weight: Schlick Fresnel (F0=0.02).
- **Refraction** (chromatic aberration):
  - Three refracted rays: IOR±0.005 for R/G/B channels.
  - Each independently marches terrain; fallback is deep ocean blue.
  - Weight: `cos²(angle) * 0.65` (normal incidence = transparent).
- Output alpha = combined Fresnel + refraction weight.  Hardware blend: `src.rgb * src.a + dst * (1-src.a)`.

---

## Foliage

### `grass_vs.hlsl`
- **Entry**: `VSMain`
- Instanced: reads per-blade position + size from instance buffer.
- Wind: sine-wave offset applied to top vertices.
- Outputs billboard quad in world space.

### `grass_ps.hlsl`
- **Entry**: `PSMain`
- Alpha-tests `g_grassBladeTex` (threshold 0.15); discards transparent pixels.
- Tints by NdotL + ambient; slight wind-driven colour shift.

---

## SSAO

### `ssao_ps.hlsl`
- **Entry**: `PSMain`
- Reads `g_ssaoDepth` (t0) and `g_ssaoNormals` (t1).
- 64 hemisphere samples (from `SSAOData` b5); `g_ssaoRadius`, `g_ssaoBias`.
- Outputs single-channel occlusion factor (0=occluded, 1=open).

### `ssao_blur_ps.hlsl`
- **Entry**: `PSMain`
- Bilateral Gaussian 5×5; weights by depth difference to avoid leaking across edges.

---

## Post-Process

### `ssgi_ps.hlsl` — Screen-Space Global Illumination
- Samples screen normals + depth to estimate indirect bounce from surrounding surfaces.
- Outputs additive GI colour contribution.

### `godrays_ps.hlsl` — God Rays / Light Shafts
- Radial blur from projected sun disc position.
- Intensity scales with `g_godRayIntensity` and sun-sky alignment.

### `bloom_ps.hlsl` — Bloom
- 4-level Kawase threshold + downsample + upsample chain.
- Threshold: `g_bloomThreshold`; blend strength: `g_bloomIntensity`.

### `fxaa_ps.hlsl` — FXAA
- Luma-based edge detection; sub-pixel blending.
- Enabled by `g_fxaaEnabled` (b6); skipped at full quality or FXAA Extreme.

### `tonemap_ps.hlsl` — Tonemap
- ACES filmic curve on `SceneHDRRT`.
- Exposure multiplier `g_exposure`.
- Underwater tint: when `g_ppUnderwater==1`, blends a deep blue desaturate.
- Writes final LDR colour to the backbuffer.
