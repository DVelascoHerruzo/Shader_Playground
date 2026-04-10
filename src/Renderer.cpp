#include "Renderer.h"
#include <algorithm>
#include <vector>
#include <DirectXPackedVector.h>

// stb_image for loading texture files
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
// stb_image_write for screenshots
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
// tinyexr for loading EXR normal maps (implementation in EXRLoader.cpp)
#include "tinyexr.h"

// ---------------------------------------------------------------------------
// Internal helper: load a PNG/JPG file as an sRGB texture with full mip chain.
// Requires BIND_RENDER_TARGET + RESOURCE_MISC_GENERATE_MIPS for GenerateMips().
// ---------------------------------------------------------------------------
// srgb=true  → DXGI_FORMAT_R8G8B8A8_UNORM_SRGB  (albedo / colour textures)
// srgb=false → DXGI_FORMAT_R8G8B8A8_UNORM         (normal maps, linear data)
static ComPtr<ID3D11ShaderResourceView>
LoadTextureSRGB(ID3D11Device* device, ID3D11DeviceContext* ctx, const char* path,
                bool srgb = true)
{
    int w, h, ch;
    unsigned char* data = stbi_load(path, &w, &h, &ch, 4);  // force RGBA
    if (!data) {
        char buf[256]; snprintf(buf, sizeof(buf), "LoadTexture: cannot open %s", path);
        OutputDebugStringA(buf);
        OutputDebugStringA("\n");
        return nullptr;
    }

    D3D11_TEXTURE2D_DESC td{};
    td.Width     = w;   td.Height = h;
    td.MipLevels = 0;   // full mip chain
    td.ArraySize = 1;
    td.Format    = srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc = { 1, 0 };
    td.Usage     = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

    ComPtr<ID3D11Texture2D> tex;
    HR(device->CreateTexture2D(&td, nullptr, &tex));

    ComPtr<ID3D11ShaderResourceView> srv;
    HR(device->CreateShaderResourceView(tex.Get(), nullptr, &srv));

    ctx->UpdateSubresource(tex.Get(), 0, nullptr, data, (UINT)(w * 4), 0);
    ctx->GenerateMips(srv.Get());

    stbi_image_free(data);
    return srv;
}

// ---------------------------------------------------------------------------
// Internal helper: load an EXR file as a float16 normal-map texture with mips.
// EXR is assumed to store tangent-space normals in OpenGL convention [0,1] range
// (i.e. Polyhaven _nor_gl_ files).  The G-channel flip to DX convention is done
// in the shader's TriplanarNormal() function so the raw data is loaded as-is.
// ---------------------------------------------------------------------------
static ComPtr<ID3D11ShaderResourceView>
LoadEXRNormalMap(ID3D11Device* device, ID3D11DeviceContext* ctx, const char* path)
{
    float* rgba = nullptr;
    int w = 0, h = 0;
    const char* err = nullptr;

    if (LoadEXR(&rgba, &w, &h, path, &err) != TINYEXR_SUCCESS) {
        if (err) {
            char buf[512]; snprintf(buf, sizeof(buf), "LoadEXRNormalMap: %s: %s\n", path, err);
            OutputDebugStringA(buf);
            FreeEXRErrorMessage(err);
        } else {
            char buf[256]; snprintf(buf, sizeof(buf), "LoadEXRNormalMap: cannot open %s\n", path);
            OutputDebugStringA(buf);
        }
        return nullptr;
    }

    // Pack float32 RGBA → float16 to halve VRAM usage
    using namespace DirectX::PackedVector;
    const size_t pixelCount = static_cast<size_t>(w) * h * 4;
    std::vector<HALF> half4(pixelCount);
    for (size_t i = 0; i < pixelCount; ++i)
        half4[i] = XMConvertFloatToHalf(rgba[i]);
    free(rgba);

    D3D11_TEXTURE2D_DESC td{};
    td.Width      = static_cast<UINT>(w);
    td.Height     = static_cast<UINT>(h);
    td.MipLevels  = 0;   // generate full mip chain
    td.ArraySize  = 1;
    td.Format     = DXGI_FORMAT_R16G16B16A16_FLOAT;
    td.SampleDesc = { 1, 0 };
    td.Usage      = D3D11_USAGE_DEFAULT;
    td.BindFlags  = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    td.MiscFlags  = D3D11_RESOURCE_MISC_GENERATE_MIPS;

    ComPtr<ID3D11Texture2D> tex;
    HR(device->CreateTexture2D(&td, nullptr, &tex));

    ComPtr<ID3D11ShaderResourceView> srv;
    HR(device->CreateShaderResourceView(tex.Get(), nullptr, &srv));

    ctx->UpdateSubresource(tex.Get(), 0, nullptr,
        half4.data(),
        static_cast<UINT>(w * 4 * sizeof(HALF)), 0);
    ctx->GenerateMips(srv.Get());
    return srv;
}

// ---------------------------------------------------------------------------
Renderer::Renderer(D3DContext& ctx)
    : m_ctx(ctx)
{}

Renderer::~Renderer()
{
    m_ui.Shutdown();
}

// ---------------------------------------------------------------------------
void Renderer::Init()
{
    ID3D11Device*        dev  = m_ctx.Device();
    ID3D11DeviceContext* dctx = m_ctx.Context();
    UINT W = m_ctx.Width();
    UINT H = m_ctx.Height();

    // Camera
    m_camera.Init();
    m_camera.CreateCB(dev);

    // Sun (light + sky colors)
    m_sun.Init();
    m_sun.CreateCB(dev);

    // Terrain generation is NOT started automatically — user presses "Regenerate Terrain"
    // in the Scene Controls panel to kick it off the first time.

    // Rendering sub-systems
    m_terrain.Create(dev);
    m_shadowMap.Create(dev);
    m_sky.Create(dev);
    m_water.Create(dev, W, H);
    m_ssao.Create(dev, W, H);
    m_post.Create(dev, W, H);
    m_foliage.Create(dev);
    m_rayTracer.Create(dev);

    // Upload initial terrain CB data
    {
        TerrainData td{};
        td.heightScale     = TERRAIN_HEIGHT;
        td.terrainSize     = TERRAIN_WORLD_SIZE;
        td.gridSize        = TERRAIN_GRID_SIZE;
        td.uvTile          = 20.0f;
        // Thresholds are heightNorm = worldPos.y / heightScale in range [-0.3 .. 0.7]
        // x=sand/coastal, y=grass/rock, z=rock/snow (now 0.45→snow starts lower), w=full snowcap
        td.thresholds      = { 0.12f, 0.35f, 0.45f, 0.70f };
        td.slopeThreshold  = 0.65f;
        m_terrain.UpdateTerrainCB(dctx, td);
    }

    // Global samplers:  s0=linearWrap, s1=shadowCmp, s2=linearClamp, s3=pointClamp
    {
        D3D11_SAMPLER_DESC sd{};
        sd.Filter         = D3D11_FILTER_ANISOTROPIC;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        sd.MaxAnisotropy  = 8;
        sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sd.MaxLOD         = D3D11_FLOAT32_MAX;
        HR(dev->CreateSamplerState(&sd, &m_samplers[0]));   // linearWrap / aniso

        sd.Filter         = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
        sd.BorderColor[0] = sd.BorderColor[1] = sd.BorderColor[2] = sd.BorderColor[3] = 1.0f;
        sd.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
        HR(dev->CreateSamplerState(&sd, &m_samplers[1]));   // shadow comparison

        sd.Filter         = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
        HR(dev->CreateSamplerState(&sd, &m_samplers[2]));   // linearClamp

        sd.Filter         = D3D11_FILTER_MIN_MAG_MIP_POINT;
        HR(dev->CreateSamplerState(&sd, &m_samplers[3]));   // pointClamp
    }

    // Disabled clip plane CB (Y > -10000, always passes)
    {
        m_disabledClipCB.Create(dev);
        ClipPlaneData disabled{};
        disabled.clipPlane = { 0.0f, 1.0f, 0.0f, 10000.0f };  // always positive — passes all geometry
        m_disabledClipCB.Update(dctx, disabled);
    }

    // UI
    m_ui.Init(m_ctx.Window(), dev, dctx);

    // Terrain albedo textures (copied to build/Debug/textures/ by post-build)
    m_texGrass  = LoadTextureSRGB(dev, dctx, "textures/grass.png");
    m_texDirt   = LoadTextureSRGB(dev, dctx, "textures/dirt.jpg");
    m_texMud    = LoadTextureSRGB(dev, dctx, "textures/mud.png");
    m_texRock   = LoadTextureSRGB(dev, dctx, "textures/rock.png");
    m_texSnow   = LoadTextureSRGB(dev, dctx, "textures/snow_diff.jpg");
    m_texDaySky   = LoadTextureSRGB(dev, dctx, "textures/sky_day.jpg");
    m_texNightSky = LoadTextureSRGB(dev, dctx, "textures/sky_night.jpg");
    m_sky.SetSkybox     (m_texDaySky.Get());
    m_sky.SetNightSkybox(m_texNightSky.Get());

    // Sand PBR textures (copied from "assets/sand textures/" by CMake post-build)
    m_texSandNor   = LoadEXRNormalMap(dev, dctx, "textures/sand_nor.exr");
    m_texSandDiff  = LoadTextureSRGB(dev, dctx, "textures/sand_diff.jpg",   true);   // sRGB albedo
    m_texSandRough = LoadTextureSRGB(dev, dctx, "textures/sand_rough.jpg",  false);  // linear
    m_texSandDisp  = LoadTextureSRGB(dev, dctx, "textures/sand_disp.png",   false);  // linear

    // Snow PBR textures
    m_texSnowNor   = LoadEXRNormalMap(dev, dctx, "textures/snow_nor.exr");
    m_texSnowRough = LoadTextureSRGB(dev, dctx, "textures/snow_rough.jpg",  false);  // linear

    // Grass billboard textures (reference: GarrettGunnell/Grass base_grass5.png / base_grass5n.png)
    m_texGrassBlade  = LoadTextureSRGB(dev, dctx, "textures/grass_blade.png",   true);  // sRGB albedo
    m_texGrassBladeN = LoadTextureSRGB(dev, dctx, "textures/grass_blade_n.png",  false); // linear normals
    m_foliage.SetGrassTextures(m_texGrassBlade.Get(), m_texGrassBladeN.Get());
}

// ---------------------------------------------------------------------------
void Renderer::OnResize(UINT width, UINT height)
{
    if (width == 0 || height == 0) return;
    ID3D11Device* dev = m_ctx.Device();
    m_water.Resize(dev, width, height);
    m_ssao.Resize(dev, width, height);
    m_post.Resize(dev, width, height);
}

// ---------------------------------------------------------------------------
void Renderer::Update(float dt)
{
    m_time += dt;

    ID3D11DeviceContext* ctx = m_ctx.Context();

    // Camera input (handles WASD + RMB look internally from D3DContext)
    m_camera.Update(dt, &m_ctx);
    m_camera.UpdateCB(ctx, m_ctx.AspectRatio(), m_ctx.Width(), m_ctx.Height());
    m_camera.AllBindCB(ctx, 0);

    // Sun — auto-advance time of day (speed=1 → 1 game-hour per real minute)
    m_sun.timeOfDay += dt * m_sun.timeSpeed / 60.0f;
    if (m_sun.timeOfDay >= 24.0f) m_sun.timeOfDay -= 24.0f;
    m_sun.Update(m_sun.timeOfDay);
    m_sun.UpdateCB(ctx);
    m_sun.AllBindCB(ctx, 1);

    // Water animation
    m_water.Update(m_time, m_water.waveSpeed, m_water.waveStrength, m_water.ior);

    // Terrain generation poll
    if (m_terrainGen.IsReady()) {
        m_terrainGen.UploadToGPU(m_ctx.Device(), ctx);
        // Rebuild foliage positions from the new heightmap
        // (thresholds match those set in Init)
        m_foliage.Rebuild(m_ctx.Device(), m_terrainGen,
            0.12f, 0.35f, 0.65f);
    }

    // UI (builds draw lists; hot/screenshot latches handled inside)
    m_ui.BeginFrame();
    bool hotReload        = false;
    bool screenshot       = false;
    bool regenTerrain     = false;
    bool wireframe        = m_wireframe;
    bool rebuildFoliage   = false;

    float fps    = 1.0f / std::max(dt, 1e-6f);
    float frameMs = dt * 1000.0f;

    m_ui.Draw(m_camera, m_sun, m_terrainGen,
              m_ssao, m_post, m_water, m_foliage, m_rayTracer,
              fps, frameMs,
              hotReload, screenshot,
              regenTerrain, wireframe,
              rebuildFoliage, m_vsync);

    m_wireframe = wireframe;
    if (hotReload)       HotReloadShaders();
    if (screenshot)      m_screenshotPending = true;
    if (regenTerrain)    RegenerateTerrain();
    if (rebuildFoliage)  m_foliage.Rebuild(m_ctx.Device(), m_terrainGen, 0.12f, 0.35f, 0.65f);
}

// ---------------------------------------------------------------------------
void Renderer::Render()
{
    ID3D11DeviceContext* ctx = m_ctx.Context();
    UINT W = m_ctx.Width(), H = m_ctx.Height();

    // Bind global samplers to PS, DS, HS, VS stages
    ID3D11SamplerState* samps[4] = {
        m_samplers[0].Get(), m_samplers[1].Get(),
        m_samplers[2].Get(), m_samplers[3].Get()
    };
    ctx->PSSetSamplers(0, 4, samps);
    ctx->DSSetSamplers(0, 4, samps);

    // Update RayTraceData CB once per frame and bind to all stages at b9.
    // terrain_ps reads g_rtEnabled during every geometry pass (main, reflection, refraction).
    m_rayTracer.BindCB(ctx);

    // ------- Shadow cascades -----------------------------------------------
    if (m_terrainGen.HasTerrain()) DoShadowPass();

    // ------- Water reflection / refraction passes --------------------------
    if (m_terrainGen.HasTerrain()) DoWaterReflectionPass();
    if (m_terrainGen.HasTerrain()) DoWaterRefractionPass();

    // ------- Main HDR scene pass -------------------------------------------
    DoMainPass();

    // ------- Ray-trace pass (reflection + refraction on water) -----------
    if (m_terrainGen.HasTerrain()) DoRayTracePass();

    // ------- Post-process (FXAA + tonemap → backbuffer) -------------------
    // Ensure CameraData (b0) and WaterData (b4) are live for the tonemap
    // underwater effect (g_camPos, g_seaLevel, g_time all needed).
    m_camera.AllBindCB(ctx, 0);
    m_water.BindWaterCB(ctx, 4);
    // Detect underwater and set flag *before* UpdateCB
    {
        float camY = m_camera.GetPosition().y;
        m_post.Params().underwaterEnabled = (camY < SEA_LEVEL + 0.5f) ? 1 : 0;
    }
    m_post.UpdateCB(ctx);
    m_post.Execute(ctx, m_ctx.BackBufferRTV(), W, H);

    // ------- ImGui overlay + Present --------------------------------------
    {
        ID3D11RenderTargetView* rtv = m_ctx.BackBufferRTV();
        ctx->OMSetRenderTargets(1, &rtv, nullptr);
    }
    m_ui.EndFrame(ctx);

    m_ctx.Present(m_vsync);

    if (m_screenshotPending) {
        m_screenshotPending = false;
        TakeScreenshot();
    }
}

// ---------------------------------------------------------------------------
void Renderer::DoShadowPass()
{
    ID3D11DeviceContext* ctx = m_ctx.Context();

    m_shadowMap.Update(m_camera, m_ctx.AspectRatio(), m_sun.GetData().dirToSun);

    auto* heightSRV = m_terrainGen.HeightmapSRV();

    for (int c = 0; c < NUM_CASCADES; ++c) {
        m_shadowMap.BeginCascade(ctx, c);
        m_shadowMap.UpdateCB(ctx, c);
        m_shadowMap.AllBindCB(ctx, 2);

        m_terrain.GetTerrainCB().AllBind(ctx, 3);

        // Heightmap in DS slot 0
        ctx->DSSetShaderResources(0, 1, &heightSRV);

        m_terrain.ShadowShader().Bind(ctx);
        ctx->PSSetShader(nullptr, nullptr, 0);   // depth-only pass
        m_terrain.DrawPatches(ctx);

        m_shadowMap.EndCascade(ctx);
    }

    // Restore topology for tessellation in next pass
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST);

    // Bind shadow SRVs to PS t8-t10
    m_shadowMap.BindSRVs(ctx, 8);
    m_shadowMap.AllBindCB(ctx, 2);
}

// ---------------------------------------------------------------------------
void Renderer::DoWaterReflectionPass()
{
    ID3D11DeviceContext* ctx  = m_ctx.Context();
    ID3D11Device*        dev  = m_ctx.Device();

    // Bind reflection RT + depth and set half-res viewport
    {
        RenderTarget& rt = m_water.ReflectionRT();
        RenderTarget& dt = m_water.ReflDepthRT();
        D3D11_VIEWPORT vp{};
        vp.Width = (float)rt.Width();  vp.Height = (float)rt.Height();  vp.MaxDepth = 1.0f;
        ctx->RSSetViewports(1, &vp);
        ID3D11RenderTargetView* rtv = rt.RTV();
        ctx->OMSetRenderTargets(1, &rtv, dt.DSV());
        float clearCol[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        rt.Clear(ctx, clearCol);
        dt.ClearDepth(ctx, 1.0f);
    }

    // Build reflection camera
    XMFLOAT3 reflPos; float reflYaw, reflPitch;
    m_water.GetReflectionCamera(m_camera, reflPos, reflYaw, reflPitch);

    Camera reflCam;
    reflCam.Init();
    reflCam.CreateCB(dev);
    reflCam.SetPositionAndAngles(reflPos, reflYaw, reflPitch);
    reflCam.UpdateCB(ctx, m_ctx.AspectRatio(), m_ctx.Width(), m_ctx.Height());
    reflCam.AllBindCB(ctx, 0);

    m_water.BeginReflectionPass(ctx);
    m_water.BindClipPlaneCB(ctx, 7);   // clip: cull below water

    auto* heightSRV = m_terrainGen.HeightmapSRV();
    ctx->DSSetShaderResources(0, 1, &heightSRV);
    ctx->PSSetShaderResources(0, 1, &heightSRV);   // RT shadow in terrain_ps
    m_terrain.GetTerrainCB().AllBind(ctx, 3);
    m_shadowMap.AllBindCB(ctx, 2);

    // Terrain albedo + Florinsky for reflection pass
    {
        ID3D11ShaderResourceView* morphSRVs[2] = { m_terrainGen.MorphoSRV1(), m_terrainGen.MorphoSRV2() };
        ctx->PSSetShaderResources(1, 2, morphSRVs);
        ID3D11ShaderResourceView* albedoSRVs[5] = {
            m_texGrass.Get(), m_texDirt.Get(), m_texMud.Get(), m_texRock.Get(), m_texSnow.Get() };
        ctx->PSSetShaderResources(3, 5, albedoSRVs);
    }

    // Sky + terrain
    m_sky.Draw(ctx);

    // Sand+snow PBR normal and material maps at t11-t16
    {
        auto* sandNorSRV = m_texSandNor.Get();
        ctx->PSSetShaderResources(11, 1, &sandNorSRV);
        ID3D11ShaderResourceView* sandSRVs[3] = {
            m_texSandDiff.Get(), m_texSandRough.Get(), m_texSandDisp.Get()
        };
        ctx->PSSetShaderResources(12, 3, sandSRVs);
        ID3D11ShaderResourceView* snowPBRs[2] = {
            m_texSnowNor.Get(), m_texSnowRough.Get()
        };
        ctx->PSSetShaderResources(15, 2, snowPBRs);
    }

    m_terrain.MainShader().Bind(ctx);
    m_terrain.SetWireframe(ctx, false);
    m_terrain.DrawPatches(ctx);

    // Unbind PS t0..t7 before unbinding RT
    {
        ID3D11ShaderResourceView* nullSRVs[8] = {};
        ctx->PSSetShaderResources(0, 8, nullSRVs);
    }
    // Unbind terrain PBR maps PS t11-t16
    {
        ID3D11ShaderResourceView* nullSRVs[6] = {};
        ctx->PSSetShaderResources(11, 6, nullSRVs);
    }

    // Unbind RT
    {
        ID3D11RenderTargetView* nullRTV = nullptr;
        ctx->OMSetRenderTargets(1, &nullRTV, nullptr);
    }
    m_water.EndReflectionPass(ctx);

    // Restore main camera
    m_camera.AllBindCB(ctx, 0);
}

// ---------------------------------------------------------------------------
void Renderer::DoWaterRefractionPass()
{
    ID3D11DeviceContext* ctx = m_ctx.Context();

    // Bind refraction RT + depth and set half-res viewport
    {
        RenderTarget& rt = m_water.RefractionRT();
        RenderTarget& dt = m_water.RefrDepthRT();
        D3D11_VIEWPORT vp{};
        vp.Width = (float)rt.Width();  vp.Height = (float)rt.Height();  vp.MaxDepth = 1.0f;
        ctx->RSSetViewports(1, &vp);
        ID3D11RenderTargetView* rtv = rt.RTV();
        ctx->OMSetRenderTargets(1, &rtv, dt.DSV());
        float clearCol[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        rt.Clear(ctx, clearCol);
        dt.ClearDepth(ctx, 1.0f);
    }

    m_water.BeginRefractionPass(ctx);
    m_water.BindClipPlaneCB(ctx, 7);   // clip: cull above water

    auto* heightSRV = m_terrainGen.HeightmapSRV();
    ctx->DSSetShaderResources(0, 1, &heightSRV);
    ctx->PSSetShaderResources(0, 1, &heightSRV);   // RT shadow in terrain_ps
    m_terrain.GetTerrainCB().AllBind(ctx, 3);
    m_shadowMap.AllBindCB(ctx, 2);

    // Terrain albedo + Florinsky for refraction pass
    {
        ID3D11ShaderResourceView* morphSRVs[2] = { m_terrainGen.MorphoSRV1(), m_terrainGen.MorphoSRV2() };
        ctx->PSSetShaderResources(1, 2, morphSRVs);
        ID3D11ShaderResourceView* albedoSRVs[5] = {
            m_texGrass.Get(), m_texDirt.Get(), m_texMud.Get(), m_texRock.Get(), m_texSnow.Get() };
        ctx->PSSetShaderResources(3, 5, albedoSRVs);
    }

    // Sky must be drawn first so refracted view above the waterline shows sky, not black
    m_sky.Draw(ctx);

    // Sand+snow PBR normal and material maps at t11-t16
    {
        auto* sandNorSRV = m_texSandNor.Get();
        ctx->PSSetShaderResources(11, 1, &sandNorSRV);
        ID3D11ShaderResourceView* sandSRVs[3] = {
            m_texSandDiff.Get(), m_texSandRough.Get(), m_texSandDisp.Get()
        };
        ctx->PSSetShaderResources(12, 3, sandSRVs);
        ID3D11ShaderResourceView* snowPBRs[2] = {
            m_texSnowNor.Get(), m_texSnowRough.Get()
        };
        ctx->PSSetShaderResources(15, 2, snowPBRs);
    }

    m_terrain.MainShader().Bind(ctx);
    m_terrain.SetWireframe(ctx, false);
    m_terrain.DrawPatches(ctx);

    // Unbind PS t0..t7
    {
        ID3D11ShaderResourceView* nullSRVs[8] = {};
        ctx->PSSetShaderResources(0, 8, nullSRVs);
    }
    // Unbind terrain PBR maps PS t11-t16
    {
        ID3D11ShaderResourceView* nullSRVs[6] = {};
        ctx->PSSetShaderResources(11, 6, nullSRVs);
    }

    // Unbind RT
    {
        ID3D11RenderTargetView* nullRTV = nullptr;
        ctx->OMSetRenderTargets(1, &nullRTV, nullptr);
    }
    m_water.EndRefractionPass(ctx);
}

// ---------------------------------------------------------------------------
void Renderer::DoMainPass()
{
    ID3D11DeviceContext* ctx = m_ctx.Context();
    UINT W = m_ctx.Width(), H = m_ctx.Height();

    // Restore full-res viewport (shadow/water passes changed it)
    {
        D3D11_VIEWPORT vp{};
        vp.Width = (float)W;  vp.Height = (float)H;  vp.MaxDepth = 1.0f;
        ctx->RSSetViewports(1, &vp);
    }

    // Bind "disabled" clip plane so terrain is fully drawn
    m_disabledClipCB.AllBind(ctx, 7);

    m_post.BeginSceneCapture(ctx);

    // Clear HDR + normals RTs and depth for this frame
    {
        float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        m_post.SceneHDRRT().Clear(ctx, black);
        m_post.NormalsRT().Clear(ctx, black);
        m_post.SceneDepthRT().ClearDepth(ctx, 1.0f);
    }

    {
        // Sky
        m_sky.Draw(ctx);

        // Terrain + foliage + water (only when heightmap is available)
        if (m_terrainGen.HasTerrain())
        {
        // Terrain
        auto* heightSRV = m_terrainGen.HeightmapSRV();
        ctx->DSSetShaderResources(0, 1, &heightSRV);
        // Also bind to PS t0 so terrain_ps can ray-march shadows via the heightmap (g_hmPS)
        ctx->PSSetShaderResources(0, 1, &heightSRV);

        // Florinsky morphometric maps at PS t1, t2
        {
            ID3D11ShaderResourceView* morphSRVs[2] = {
                m_terrainGen.MorphoSRV1(),
                m_terrainGen.MorphoSRV2()
            };
            ctx->PSSetShaderResources(1, 2, morphSRVs);
        }
        // Terrain albedo textures at PS t3..t7
        {
            ID3D11ShaderResourceView* albedoSRVs[5] = {
                m_texGrass.Get(), m_texDirt.Get(), m_texMud.Get(),
                m_texRock.Get(),  m_texSnow.Get()
            };
            ctx->PSSetShaderResources(3, 5, albedoSRVs);
        }

        // Sand normal map at t11 (replaces SSAO slot)
        auto* sandNorSRV = m_texSandNor.Get();
        ctx->PSSetShaderResources(11, 1, &sandNorSRV);

        // Bind water CB at b4 now so terrain_ps has g_seaLevel for the sand zone
        m_water.BindWaterCB(ctx, 4);

        // Sand PBR maps at t12-t14 (sky pass unbinds t12-t13 before returning)
        {
            ID3D11ShaderResourceView* sandSRVs[3] = {
                m_texSandDiff.Get(), m_texSandRough.Get(), m_texSandDisp.Get()
            };
            ctx->PSSetShaderResources(12, 3, sandSRVs);
        }
        // Snow PBR normal + roughness at t15-t16
        {
            ID3D11ShaderResourceView* snowPBRs[2] = {
                m_texSnowNor.Get(), m_texSnowRough.Get()
            };
            ctx->PSSetShaderResources(15, 2, snowPBRs);
        }

        m_terrain.GetTerrainCB().AllBind(ctx, 3);
        m_terrain.MainShader().Bind(ctx);
        m_terrain.SetWireframe(ctx, m_wireframe);
        m_terrain.DrawPatches(ctx);

        // Unbind PS t0..t7 — raytrace_ps later uses t0 for scene depth
        {
            ID3D11ShaderResourceView* nullSRVs[8] = {};
            ctx->PSSetShaderResources(0, 8, nullSRVs);  // clear t0..t7
        }
        // Unbind terrain PBR maps PS t11-t16
        {
            ID3D11ShaderResourceView* nullSRVs[6] = {};
            ctx->PSSetShaderResources(11, 6, nullSRVs);
        }

        // Foliage (grass clumps) — drawn after terrain while depth write is active
        m_foliage.Draw(ctx, m_time, m_foliage.windStrength, m_foliage.windFrequency);

        // Water surface
        // Switch to read-only DSV so the depth texture can be simultaneously sampled
        // as SRV in the water PS (otherwise DX11 silently NULLs the SRV → depthUnder=0 → full foam)
        {
            ID3D11RenderTargetView* rtvs[2] = {
                m_post.SceneHDRRT().RTV(),
                m_post.NormalsRT().RTV()
            };
            ctx->OMSetRenderTargets(2, rtvs, m_post.SceneDepthRT().DSVReadOnly());
        }
        m_water.BindWaterCB(ctx, 4);
        auto* sceneDepthSRV = m_post.SceneDepthRT().SRV();
        m_water.Draw(ctx, sceneDepthSRV, nullptr);
        } // end if (m_terrainGen.HasTerrain())
    }

    // Unbind depth/terrain SRVs before ray-trace pass
    {
        ID3D11ShaderResourceView* nullSRVs[6] = {};
        ctx->PSSetShaderResources(11, 6, nullSRVs);  // t11-t16
        ID3D11ShaderResourceView* nullSRV = nullptr;
        ctx->DSSetShaderResources(0, 1, &nullSRV);
    }
}

// ---------------------------------------------------------------------------
void Renderer::DoRayTracePass()
{
    if (!m_rayTracer.enabled) return;

    ID3D11DeviceContext* ctx = m_ctx.Context();
    UINT W = m_ctx.Width(), H = m_ctx.Height();

    // Ensure camera, sun, terrain, and water CBs are bound (they persist from
    // the main pass, but rebind for safety)
    m_camera.AllBindCB(ctx, 0);
    m_sun.AllBindCB(ctx, 1);
    m_terrain.GetTerrainCB().AllBind(ctx, 3);
    m_water.BindWaterCB(ctx, 4);

    m_rayTracer.Execute(ctx, W, H,
        m_post.SceneHDRRT().RTV(),
        m_post.SceneDepthRT().SRV(),
        m_post.NormalsRT().SRV(),
        m_terrainGen.HeightmapSRV());
}

// ---------------------------------------------------------------------------
void Renderer::HotReloadShaders()
{
    ID3D11Device* dev = m_ctx.Device();
    m_terrain.HotReloadShaders(dev);
    m_sky.HotReloadShaders(dev);
    m_water.HotReloadShaders(dev);
    m_ssao.HotReloadShaders(dev);
    m_post.HotReloadShaders(dev);
    m_foliage.HotReloadShaders(dev);
    m_rayTracer.HotReloadShaders(dev);
}

// ---------------------------------------------------------------------------
void Renderer::RegenerateTerrain()
{
    m_terrainGen.Generate(m_terrainGen.params);
}

// ---------------------------------------------------------------------------
void Renderer::TakeScreenshot()
{
    ID3D11Device*        dev  = m_ctx.Device();
    ID3D11DeviceContext* ctx  = m_ctx.Context();
    UINT W = m_ctx.Width();
    UINT H = m_ctx.Height();

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = W; desc.Height = H;
    desc.MipLevels = 1; desc.ArraySize = 1;
    desc.Format  = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage   = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(dev->CreateTexture2D(&desc, nullptr, &staging))) return;

    {
        ComPtr<ID3D11Texture2D> bb;
        m_ctx.SwapChain()->GetBuffer(0, IID_PPV_ARGS(&bb));
        ctx->CopyResource(staging.Get(), bb.Get());
    }

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return;

    // Find unique filename
    char path[256]; int idx = 0;
    do { snprintf(path, sizeof(path), "screenshots/screenshot_%04d.png", idx++); }
    while (idx < 9999 && GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES);

    stbi_write_png(path, (int)W, (int)H, 4, mapped.pData, (int)mapped.RowPitch);
    ctx->Unmap(staging.Get(), 0);
}
