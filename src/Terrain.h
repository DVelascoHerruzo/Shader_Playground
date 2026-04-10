#pragma once
#include "Types.h"
#include "Shader.h"
#include "ConstantBuffer.h"
#include "TerrainGenerator.h"

// ---------------------------------------------------------------------------
//  Terrain
//  Manages the tessellated patch grid vertex/index buffers and the
//  terrain shader programs (main pass + shadow pass).
// ---------------------------------------------------------------------------
class Terrain {
public:
    void Create(ID3D11Device* device);

    // Bind VB/IB and draw patches
    void DrawPatches(ID3D11DeviceContext* ctx) const;

    ShaderProgram& MainShader()   { return m_mainShader; }
    ShaderProgram& ShadowShader() { return m_shadowShader; }

    void HotReloadShaders(ID3D11Device* device);

    // State toggles (for UI)
    bool wireframe = false;
    bool debugNormals = false;

    // Terrain CB (heightScale, size, etc.)
    ConstantBuffer<TerrainData>& GetTerrainCB() { return m_terrainCB; }
    void UpdateTerrainCB(ID3D11DeviceContext* ctx, const TerrainData& d) { m_terrainCB.Update(ctx, d); }

    // Rasterizer states (toggled by wireframe flag)
    void SetWireframe(ID3D11DeviceContext* ctx, bool enable) const;

private:
    void BuildGrid(ID3D11Device* device);

    static constexpr int GRID = TERRAIN_GRID_SIZE;  // 128 patches per side

    ComPtr<ID3D11Buffer>           m_vb;
    ComPtr<ID3D11Buffer>           m_ib;
    UINT                           m_indexCount = 0;

    ShaderProgram m_mainShader;
    ShaderProgram m_shadowShader;

    ConstantBuffer<TerrainData>    m_terrainCB;

    ComPtr<ID3D11RasterizerState>  m_rsWireframe;
    ComPtr<ID3D11RasterizerState>  m_rsSolid;
};
