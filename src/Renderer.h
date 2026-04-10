#pragma once
#include "Types.h"
#include "D3DContext.h"
#include "Camera.h"
#include "Sun.h"
#include "TerrainGenerator.h"
#include "Terrain.h"
#include "ShadowMap.h"
#include "Sky.h"
#include "Water.h"
#include "SSAO.h"
#include "PostProcess.h"
#include "UI.h"
#include "ConstantBuffer.h"
#include "Foliage.h"
#include "RayTracer.h"

// ---------------------------------------------------------------------------
//  Renderer
//  Owns all sub-systems and drives the per-frame render pipeline.
//
//  Frame order:
//    1. Shadow pass  (3 cascade depth-only draws)
//    2. Water reflection pass  (scene into reflection RT)
//    3. Water refraction pass  (scene into refraction RT)
//    4. Main scene:
//         a. BeginSceneCapture   (bind HDR RT + GBuffer normals + depth)
//         b. Sky draw
//         c. Terrain draw
//         d. Water surface draw
//    5. SSAO
//    6. Ray-trace pass  (screen-space reflection + refraction blended onto scene HDR)
//    7. PostProcess  (FXAA + tonemap → backbuffer, depth kept bound)
//    8. ImGui
//    9. Present
// ---------------------------------------------------------------------------
class Renderer {
public:
    explicit Renderer(D3DContext& ctx);
    ~Renderer();

    void Init();
    void OnResize(UINT width, UINT height);

    void Update(float dt);
    void Render();

    void HotReloadShaders();
    void RegenerateTerrain();
    void TakeScreenshot();

    // Accessors for UI
    Camera&           GetCamera()          { return m_camera;     }
    Sun&              GetSun()             { return m_sun;        }
    TerrainGenerator& GetTerrainGenerator(){ return m_terrainGen; }
    SSAO&             GetSSAO()            { return m_ssao;       }
    PostProcess&      GetPostProcess()     { return m_post;       }
    Water&            GetWater()           { return m_water;      }
    UI&               GetUI()              { return m_ui;         }

private:
    void DoShadowPass();
    void DoWaterReflectionPass();
    void DoWaterRefractionPass();
    void DoMainPass();
    void DoRayTracePass();

    D3DContext& m_ctx;

    Camera           m_camera;
    Sun              m_sun;
    TerrainGenerator m_terrainGen;
    Terrain          m_terrain;
    ShadowMap        m_shadowMap;
    Sky              m_sky;
    Water            m_water;
    SSAO             m_ssao;
    PostProcess      m_post;
    UI               m_ui;
    Foliage          m_foliage;
    RayTracer        m_rayTracer;

    // Global samplers: [0]=linearWrap/aniso, [1]=shadowCmp, [2]=linearClamp, [3]=pointClamp
    std::array<ComPtr<ID3D11SamplerState>, 4> m_samplers;

    // Disabled clip plane (Y > -10000) bound at b7 during main pass
    ConstantBuffer<ClipPlaneData> m_disabledClipCB;

    // Terrain albedo textures (stb_image / DX11 with full mipchain)
    ComPtr<ID3D11ShaderResourceView> m_texGrass;
    ComPtr<ID3D11ShaderResourceView> m_texDirt;
    ComPtr<ID3D11ShaderResourceView> m_texMud;
    ComPtr<ID3D11ShaderResourceView> m_texRock;
    ComPtr<ID3D11ShaderResourceView> m_texSnow;
    // Equirectangular HDRI sky panoramas
    ComPtr<ID3D11ShaderResourceView> m_texDaySky;    // sky_day.jpg  (t12)
    ComPtr<ID3D11ShaderResourceView> m_texNightSky;  // sky_night.jpg (t13)
    // Sand PBR texture maps (bound at t11-t14 during terrain passes)
    ComPtr<ID3D11ShaderResourceView> m_texSandNor;    // sand_nor.exr   (linear, float16)
    ComPtr<ID3D11ShaderResourceView> m_texSandDiff;   // sand_diff.jpg
    ComPtr<ID3D11ShaderResourceView> m_texSandRough;  // sand_rough.jpg (linear)
    ComPtr<ID3D11ShaderResourceView> m_texSandDisp;   // sand_disp.png  (linear)
    // Snow PBR texture maps (bound at t15-t16 during terrain passes)
    ComPtr<ID3D11ShaderResourceView> m_texSnowNor;    // snow_nor.exr   (linear, float16)
    ComPtr<ID3D11ShaderResourceView> m_texSnowRough;  // snow_rough.jpg (linear)
    // Grass billboard textures (from GarrettGunnell/Grass reference)
    ComPtr<ID3D11ShaderResourceView> m_texGrassBlade;   // base_grass5.png  (sRGB albedo+alpha)
    ComPtr<ID3D11ShaderResourceView> m_texGrassBladeN;  // base_grass5n.png (linear normal map)

    bool  m_wireframe         = false;
    bool  m_vsync             = true;   // toggled via UI (Post-Process panel)
    float m_time              = 0.0f;
    bool  m_screenshotPending = false;
};
