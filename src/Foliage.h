#pragma once
#include "Types.h"
#include "Shader.h"
#include "ConstantBuffer.h"
#include "TerrainGenerator.h"

// ---------------------------------------------------------------------------
//  Foliage
//  Renders instanced grass clumps (two crossed quads per clump) placed over
//  the terrain grass biome.  Positions are generated on the CPU from the
//  terrain heightmap every time the terrain is (re)generated; instance data
//  is uploaded as an immutable D3D11 vertex buffer.
//
//  Draw order: after terrain, before water, using alpha-discard (no sorting).
// ---------------------------------------------------------------------------
class Foliage {
public:
    void Create(ID3D11Device* device);

    // Rebuild instance positions from the current terrain heightmap.
    // Call this after TerrainGenerator::UploadToGPU().
    void Rebuild(ID3D11Device* device, const TerrainGenerator& gen,
                 float tSandGrass, float tGrassRock, float slopeThreshold);

    // Set grass billboard textures loaded by Renderer (not owned by Foliage).
    // albedo = base_grass5.png (sRGB, has alpha cutout)
    // normal = base_grass5n.png (linear tangent-space normal map)
    void SetGrassTextures(ID3D11ShaderResourceView* albedo,
                          ID3D11ShaderResourceView* normal)
    {
        m_texAlbedo = albedo;
        m_texNormal = normal;
    }

    // Draw all instances.  Camera and Sun CBs must already be bound at b0/b1.
    void Draw(ID3D11DeviceContext* ctx, float time,
              float windStrength = 0.30f, float windFrequency = 1.0f);

    void HotReloadShaders(ID3D11Device* device);

    bool  enabled       = true;
    int   maxInstances  = 5000000;  // no cap — fill entire biome at 0.5m spacing
    float drawDistance  = 120.0f;   // fade-out distance in world metres
    float windStrength  = 0.30f;    // passed to Draw each frame
    float windFrequency = 1.00f;

    UINT GetInstanceCount() const { return m_instanceCount; }

private:
    void BuildUnitMesh(ID3D11Device* device);

    ShaderProgram m_shader;

    ComPtr<ID3D11Buffer> m_unitVB;      // 12-vertex cross shape (immutable)
    ComPtr<ID3D11Buffer> m_instanceVB;  // per-clump transforms (immutable, may be null)
    UINT                 m_instanceCount = 0;

    ConstantBuffer<FoliageData> m_foliageCB;

    ComPtr<ID3D11RasterizerState> m_rsCullNone;   // two-sided grass blades

    // Grass billboard textures (set by Renderer, not owned — lifetime managed by Renderer)
    ID3D11ShaderResourceView* m_texAlbedo = nullptr;  // t0: albedo + alpha cutout
    ID3D11ShaderResourceView* m_texNormal = nullptr;  // t1: tangent-space normal map
};
