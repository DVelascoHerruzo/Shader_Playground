# ReShade Integration Guide — Shader Playground DX11

## Quick Start

1. **Build the project** (see root README):
   ```cmd
   cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
   cmake --build build --config Release
   ```

2. **Download ReShade** from [reshade.me](https://reshade.me/)

3. **Run the ReShade installer** and select `ShaderPlayground.exe` in your build output folder.

4. **Select "Direct3D 10/11/12"** as the rendering API.

5. **Copy your `.fx` shader files** into `reshade-shaders/Shaders/` (this directory).

6. **Launch ShaderPlayground.exe** and press **Home** to open the ReShade overlay.

---

## Depth Buffer Access

The scene depth buffer is kept OM-bound at `Present()` time, making it visible to ReShade's depth provider add-on:

| Property         | Value          |
|-----------------|----------------|
| Depth precision | R32_FLOAT      |
| Direction       | Forward-Z      |
| Near plane      | **0.3 units**  |
| Far plane       | **3000 units** |
| Format          | DXGI_FORMAT_R32_TYPELESS (DSV: D32_FLOAT, SRV: R32_FLOAT) |

In ReShade's **generic_depth** add-on, look for the entry matching the window resolution (e.g. 1280×720) and select it.

For depth linearisation in your `.fx` shaders:
```hlsl
float linearDepth = (0.3 * 3000.0) / (3000.0 - depth * (3000.0 - 0.3));
```

---

## Shader Search Paths (reshade.ini)

```ini
EffectSearchPaths=.\reshade-shaders\Shaders
TextureSearchPaths=.\reshade-shaders\Textures
```

---

## Directory Structure

```
reshade-shaders/
  Shaders/      ← place your .fx files here
  Textures/     ← place textures referenced by your shaders here
  README.md     ← this file
```

---

## Tips

- The scene renders to an **R16G16B16A16_FLOAT** HDR render target.
  After ACES tone-mapping the backbuffer is **R8G8B8A8_UNORM** (sRGB gamma applied).
- Swap-chain uses **DXGI_SWAP_EFFECT_FLIP_DISCARD** with 2 buffers (no MSAA).
- Press **F5** in the playground to hot-reload HLSL shaders.
- Press **F12** to save a screenshot to `screenshots/`.
